module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include "systems/system.hh"
#include "throw_line.hh"

export module cd.fix_msf;

import cd.cd;
import cd.cdrom;
import cd.common;
import cd.ecc;
import cd.edc;
import common;
import filesystem.iso9660;
import options;
import systems.systems;
import utils.hex_bin;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

void regenerate_data_sector(Sector &sector, int32_t lba)
{
    std::copy_n(CD_DATA_SYNC, sizeof(CD_DATA_SYNC), sector.sync);
    sector.header.address = LBA_to_BCDMSF(lba);

    if(sector.header.mode == 1)
    {
        std::fill_n(sector.mode1.intermediate, sizeof(sector.mode1.intermediate), 0x00);

        Sector::ECC ecc = ECC().Generate((uint8_t *)&sector.header);
        std::copy_n(ecc.p_parity, sizeof(ecc.p_parity), sector.mode1.ecc.p_parity);
        std::copy_n(ecc.q_parity, sizeof(ecc.q_parity), sector.mode1.ecc.q_parity);

        sector.mode1.edc = EDC().update((uint8_t *)&sector, offsetof(Sector, mode1.edc)).final();
    }
    else if(sector.header.mode == 2)
    {
        sector.mode2.xa.sub_header_copy = sector.mode2.xa.sub_header;

        // Form2
        if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
        {
            // can be zeroed, regenerate only if it was set
            if(sector.mode2.xa.form2.edc)
                sector.mode2.xa.form2.edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header, offsetof(Sector, mode2.xa.form2.edc) - offsetof(Sector, mode2.xa.sub_header)).final();
        }
        // Form1
        else
        {
            sector.mode2.xa.form1.edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header, offsetof(Sector, mode2.xa.form1.edc) - offsetof(Sector, mode2.xa.sub_header)).final();

            // modifies sector, make sure sector data is not used after ECC calculation, otherwise header has to be restored
            Sector::Header header = sector.header;
            std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0x00);

            Sector::ECC ecc = ECC().Generate((uint8_t *)&sector.header);
            std::copy_n(ecc.p_parity, sizeof(ecc.p_parity), sector.mode2.xa.form1.ecc.p_parity);
            std::copy_n(ecc.q_parity, sizeof(ecc.q_parity), sector.mode2.xa.form1.ecc.q_parity);

            // restore modified sector header
            sector.header = header;
        }
    }
}


std::pair<uint8_t *, uint32_t> data_sector_get_data(Sector &sector)
{
    if(sector.header.mode == 0)
        return std::pair(sector.mode2.user_data, MODE0_DATA_SIZE);
    else if(sector.header.mode == 1)
        return std::pair(sector.mode1.user_data, FORM1_DATA_SIZE);
    else if(sector.header.mode == 2)
    {
        if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
            return std::pair(sector.mode2.xa.form2.user_data, FORM2_DATA_SIZE);
        else
            return std::pair(sector.mode2.xa.form1.user_data, FORM1_DATA_SIZE);
    }

    return std::pair(nullptr, 0);
}


void fill_data_sector_data(Sector &sector, uint8_t fill_byte)
{
    auto d = data_sector_get_data(sector);
    std::fill_n(d.first, d.second, fill_byte);
}


bool data_sector_is_dummy(Sector &sector)
{
    auto d = data_sector_get_data(sector);
    return std::all_of(d.first, d.first + d.second, [](uint8_t value) { return value == 0x55; });
}


export int redumper_fix_msf(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::list<std::filesystem::path> tracks;
    if(std::filesystem::exists(image_prefix + ".cue"))
        for(auto const &t : cue_get_entries(image_prefix + ".cue"))
            if(track_type_is_data_raw(t.second))
                tracks.push_back(std::filesystem::path(options.image_path) / t.first);

    if(tracks.empty())
        throw_line("no files to process");

    uint32_t errors = 0;

    for(auto const &t : tracks)
    {
        uint32_t sectors_count = std::filesystem::file_size(t) / sizeof(Sector);
        std::fstream fs(t, std::fstream::binary | std::fstream::in | std::fstream::out);

        std::optional<int32_t> lba_base;
        Sector sector_last;

        for(uint32_t s = 0; s < sectors_count; ++s)
        {
            Sector sector;
            fs.read((char *)&sector, sizeof(sector));
            if(fs.fail())
                throw_line("read failed");

            bool sector_correct = true;
            if(memcmp(sector.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
                sector_correct = false;
            if(lba_base && *lba_base + s != BCDMSF_to_LBA(sector.header.address))
                sector_correct = false;

            if(sector_correct)
            {
                // store base LBA address once
                if(!lba_base)
                    lba_base = BCDMSF_to_LBA(sector.header.address);

                // always cache last good sector to be used as a repair template
                if(!data_sector_is_dummy(sector))
                    sector_last = sector;
            }
            else if(lba_base)
            {
                fill_data_sector_data(sector_last, 0x00);
                regenerate_data_sector(sector_last, *lba_base + s);

                fs.seekp(s * sizeof(Sector));
                if(fs.fail())
                    throw_line("read failed");
                fs.write((char *)&sector_last, sizeof(sector_last));
                if(fs.fail())
                    throw_line("write failed");
                fs << std::flush;

                ++errors;
            }
        }
    }

    LOG("corrected {} sectors", errors);

    return exit_code;
}


export int redumper_fix_msf_shift(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    if(!std::filesystem::exists(image_prefix + ".cue"))
        throw_line("no files to process");

    auto tracks = cue_get_entries(image_prefix + ".cue");
    if(tracks.size() != 1
        || (tracks.front().second != TrackType::MODE1_2352 && tracks.front().second != TrackType::MODE2_2352 && tracks.front().second != TrackType::CDI_2352
            && tracks.front().second != TrackType::MODE0_2352))
        throw_line("no files to process");

    auto t = std::filesystem::path(options.image_path) / tracks.front().first;
    uint32_t sectors_count = std::filesystem::file_size(t) / sizeof(Sector);
    std::fstream fs(t, std::fstream::binary | std::fstream::in);
    std::fstream fs_out(t.string() + ".fixed", std::fstream::binary | std::fstream::out);

    int32_t lba_base;

    for(uint32_t s = 0; s < sectors_count; ++s)
    {
        Sector sector;
        fs.read((char *)&sector, sizeof(sector));
        if(fs.fail())
            throw_line("read failed");

        int32_t lba = BCDMSF_to_LBA(sector.header.address);
        if(!s)
            lba_base = lba;

        fs_out.seekp((lba - lba_base) * sizeof(Sector));
        if(fs_out.fail())
            throw_line("read failed");
        fs_out.write((char *)&sector, sizeof(sector));
        if(fs_out.fail())
            throw_line("write failed");
    }

    return exit_code;
}


export int redumper_trim(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    if(!std::filesystem::exists(image_prefix + ".cue"))
        throw_line("no files to process");

    auto tracks = cue_get_entries(image_prefix + ".cue");
    if(tracks.size() != 1
        || (tracks.front().second != TrackType::MODE1_2352 && tracks.front().second != TrackType::MODE2_2352 && tracks.front().second != TrackType::CDI_2352
            && tracks.front().second != TrackType::MODE0_2352))
        throw_line("no files to process");

    auto t = std::filesystem::path(options.image_path) / tracks.front().first;
    uint32_t sectors_count = std::filesystem::file_size(t) / sizeof(Sector);
    std::fstream fs(t, std::fstream::binary | std::fstream::in);
    std::fstream fs_out(t.string() + ".fixed", std::fstream::binary | std::fstream::out);

    bool pvd_found = false;

    for(uint32_t s = 0; s < sectors_count; ++s)
    {
        Sector sector;
        fs.read((char *)&sector, sizeof(sector));
        if(fs.fail())
            throw_line("read failed");

        auto [data, data_size] = data_sector_get_data(sector);
        if(!pvd_found && data_size >= sizeof(iso9660::PrimaryVolumeDescriptor))
        {
            auto const &pvd = *(iso9660::PrimaryVolumeDescriptor *)data;
            auto identifier = to_string_view(pvd.standard_identifier);

            if(pvd.type == iso9660::VolumeDescriptorType::PRIMARY && (identifier == iso9660::DESCRIPTOR_ID_CD || identifier == iso9660::DESCRIPTOR_ID_CDI))
            {
                sectors_count = pvd.volume_space_size.lsb;
                pvd_found = true;
            }
        }

        fs_out.write((char *)&sector, sizeof(sector));
        if(fs_out.fail())
            throw_line("write failed");
    }

    if(!pvd_found)
        throw_line("primary volume descriptor not found");

    return exit_code;
}

}
