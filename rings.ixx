module;
#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>
#include "throw_line.hh"

export module rings;

import cd.cd;
import cd.cdrom;
import cd.common;
import cd.scrambler;
import cd.subcode;
import cd.toc;
import common;
import drive;
import filesystem.iso9660;
import options;
import readers.disc_read_reader;
import scsi.cmd;
import scsi.sptd;
import utils.logger;
import utils.misc;



namespace gpsxre
{

int32_t read_scrambled(SPTD &sptd, const DriveConfig &drive_config, uint8_t *sector, uint32_t lba)
{
    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
    std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
    std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);

    constexpr uint32_t sectors_count = 2;
    std::vector<uint8_t> sectors(CD_DATA_SIZE * sectors_count);
    for(uint32_t i = 0; i < sectors_count; ++i)
    {
        int32_t lba_current = lba + i;
        bool unscrambled = false;
        SPTD::Status status = read_sector_new(sptd, sector_buffer.data(), unscrambled, drive_config, lba_current);
        if(status.status_code)
            throw_line("SCSI error (LBA: {}, status: {})", lba_current, SPTD::StatusMessage(status));
        if(unscrambled)
            throw_line("unscrambled read (LBA: {})", lba_current);
        auto c2_bits = c2_bits_count(sector_c2);
        if(c2_bits)
            throw_line("C2 error (LBA: {}, bits: {})", lba_current, c2_bits);

        std::span<uint8_t> sectors_out(sectors.begin() + i * CD_DATA_SIZE, CD_DATA_SIZE);
        std::copy(sector_data.begin(), sector_data.end(), sectors_out.begin());
    }

    auto it = std::search(sectors.begin(), sectors.end(), std::begin(CD_DATA_SYNC), std::end(CD_DATA_SYNC));
    if(it == sectors.end())
        throw_line("sync not found (LBA: {})", lba);

    // there might be an incomplete sector followed by a complete sector (another sync)
    auto it2 = std::search(it + sizeof(CD_DATA_SYNC), sectors.end(), std::begin(CD_DATA_SYNC), std::end(CD_DATA_SYNC));
    if(it2 != sectors.end() && std::distance(it, it2) < CD_DATA_SIZE)
        it = it2;

    auto sync_index = (uint32_t)std::distance(sectors.begin(), it);
    if(sync_index + CD_DATA_SIZE > sectors.size())
        throw_line("not enough data (LBA: {})", lba);

    std::span<uint8_t> s(&sectors[sync_index], CD_DATA_SIZE);
    Scrambler::process(sector, &s[0], 0, s.size());

    return lba_to_sample(lba, -drive_config.read_offset + sync_index / CD_SAMPLE_SIZE);
}


int32_t find_sample_offset(SPTD &sptd, const DriveConfig &drive_config, int32_t lba)
{
    int32_t sample_offset = 0;

    int32_t index_shift = 0;
    std::set<int32_t> history;
    history.insert(index_shift);
    for(;;)
    {
        Sector sector;
        sample_offset = read_scrambled(sptd, drive_config, (uint8_t *)&sector, lba + index_shift);
        int32_t sector_lba = BCDMSF_to_LBA(sector.header.address);

        int32_t shift = lba - sector_lba;
        if(shift)
        {
            index_shift += shift;
            if(!history.insert(index_shift).second)
                throw_line("infinite loop detected (LBA: {}, shift: {:+})", sector_lba, index_shift);
        }
        else
            break;
    }

    return sample_offset;
}


export int redumper_rings(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    std::vector<uint8_t> toc_buffer = cmd_read_toc(*ctx.sptd);
    std::vector<uint8_t> full_toc_buffer = cmd_read_full_toc(*ctx.sptd);
    auto toc = toc_choose(toc_buffer, full_toc_buffer);

    for(auto &s : toc.sessions)
    {
        for(uint32_t i = 0; i + 1 < s.tracks.size(); ++i)
        {
            auto &t = s.tracks[i];

            if(!(t.control & (uint8_t)ChannelQ::Control::DATA) || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER) || t.indices.empty())
                continue;

            auto data_reader = std::make_unique<Disc_READ_Reader>(*ctx.sptd, t.indices.front());

            auto area_map = iso9660::area_map(data_reader.get(), s.tracks[i + 1].lba_start - t.indices.front());
            if(area_map.empty())
                continue;

            LOG("ISO9660 map: ");
            for(auto const &area : area_map)
            {
                auto count = scale_up(area.size, FORM1_DATA_SIZE);
                LOG("LBA: [{:6} .. {:6}), count: {:6}, size: {:9}, type: {}{}", area.lba, area.lba + count, count, area.size, iso9660::area_type_to_string(area.type),
                    area.name.empty() ? "" : std::format(", name: {}", area.name));
            }
            LOG("");

            // Datel V2
            iso9660::PrimaryVolumeDescriptor pvd;
            if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
            {
                if(iso9660::identifier_to_string(pvd.volume_identifier) == "CRAZY_TAXI")
                {
                    for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
                    {
                        if(area_map[i].type != iso9660::Area::Type::DIRECTORY_EXTENT || area_map[i + 1].type != iso9660::Area::Type::FILE_EXTENT)
                            continue;

                        int32_t lba_start = area_map[i].lba + scale_up(area_map[i].size, FORM1_DATA_SIZE);
                        int32_t lba_end = area_map[i + 1].lba;
                        if(lba_start < lba_end)
                        {
                            int32_t sample_start = find_sample_offset(*ctx.sptd, ctx.drive_config, lba_start);
                            int32_t sample_end = find_sample_offset(*ctx.sptd, ctx.drive_config, lba_end);

                            int32_t directory_offset = sample_start - lba_to_sample(lba_start, 0);
                            int32_t file_offset = sample_end - lba_to_sample(lba_end, 0);

                            std::string offset_message;
                            if(directory_offset != file_offset)
                                offset_message = std::format(", offset: {:+}", file_offset);

                            LOG("protection: PS2/Datel Ring, range: {}-{}{}", lba_start, lba_end, offset_message);
                            ctx.protection_hard.emplace_back(sample_start, sample_end);

                            break;
                        }
                    }
                }
            }
        }
    }

    return exit_code;
}

}
