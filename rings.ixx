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
import utils.win32;



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

    auto toc = toc_choose(toc_read(*ctx.sptd), toc_full_read(*ctx.sptd));

    for(auto &s : toc.sessions)
    {
        for(uint32_t i = 0; i + 1 < s.tracks.size(); ++i)
        {
            auto &t = s.tracks[i];

            if(!(t.control & (uint8_t)ChannelQ::Control::DATA) || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER) || t.indices.empty())
                continue;

            // auto data_reader = std::make_unique<Disc_READ_CDDA_Reader>(*ctx.sptd, ctx.drive_config, t.indices.front());
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

            // Datel V2 (Crazy Taxi based)
            iso9660::PrimaryVolumeDescriptor pvd;
            if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
            {
                auto volume_identifier = iso9660::identifier_to_string(pvd.volume_identifier);
                if(volume_identifier == "CRAZY_TAXI")
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
                            ctx.protection.emplace_back(sample_start, sample_end);

                            break;
                        }
                    }
                }
                // Codebreaker
                else if(volume_identifier == "CODEBREAKER" || volume_identifier == "ZONEFREE_DVD")
                {
                    auto path_table_m_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::PATH_TABLE_M; });
                    auto zero_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::FILE_EXTENT && area.name == "/0.BIN"; });
                    auto z_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::FILE_EXTENT && area.name == "/Z.BIN"; });
                    auto volume_end_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::VOLUME_END_MARKER; });

                    if(path_table_m_it != area_map.end() && zero_it != area_map.end() && z_it != area_map.end() && volume_end_it != area_map.end())
                    {
                        int32_t lba1_start = path_table_m_it->lba + scale_up(path_table_m_it->size, FORM1_DATA_SIZE);
                        int32_t lba1_end = zero_it->lba;
                        int32_t sample1_start = find_sample_offset(*ctx.sptd, ctx.drive_config, lba1_start);
                        int32_t sample1_end = find_sample_offset(*ctx.sptd, ctx.drive_config, lba1_end);

                        int32_t lba2_start = z_it->lba + scale_up(z_it->size, FORM1_DATA_SIZE);
                        int32_t sample2_start = find_sample_offset(*ctx.sptd, ctx.drive_config, lba2_start);

                        // make sure not to read past the end of the disc
                        int32_t sample2_end = lba_to_sample(volume_end_it->lba + LEADOUT_OVERREAD_COUNT, 0);

                        LOG("protection: PS2/CodeBreaker Ring, range: {}-{}:{}-{}", lba1_start, lba1_end, lba2_start, volume_end_it->lba);
                        ctx.protection.emplace_back(sample1_start, sample1_end);
                        ctx.protection.emplace_back(sample2_start, sample2_end);

                        break;
                    }
                }
                // Datel EXE Ring
                else if(volume_identifier == "PSPGAMESHARK")
                {
                    auto setup_exe_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::FILE_EXTENT && area.name == "/SETUP.EXE"; });
                    auto ico_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::FILE_EXTENT && area.name == "/SHARKLOGOS.ICO"; });

                    if(setup_exe_it != area_map.end() && ico_it != area_map.end())
                    {
                        std::vector<uint8_t> data(FORM1_DATA_SIZE);
                        if(data_reader->read(data.data(), setup_exe_it->lba, 1) == 1)
                        {
                            uint64_t exe_size = get_pe_executable_extent(data);
                            if(exe_size)
                            {
                                int32_t lba_start = setup_exe_it->lba + scale_up(exe_size, FORM1_DATA_SIZE);
                                int32_t sample_start = find_sample_offset(*ctx.sptd, ctx.drive_config, lba_start);
                                int32_t sample_end = find_sample_offset(*ctx.sptd, ctx.drive_config, ico_it->lba);

                                LOG("protection: PC/Datel EXE Ring, range: {}-{}", lba_start, ico_it->lba);
                                ctx.protection.emplace_back(sample_start, sample_end);

                                break;
                            }
                        }
                    }
                }
                // Blaze (Wild Wild Racing based)
                else if(volume_identifier.empty())
                {
                    auto movies_it =
                        std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::FILE_EXTENT && area.name == "/MOVIES/CREDITS1.PSS"; });
                    auto exe_it = std::find_if(area_map.begin(), area_map.end(), [](const auto &area) { return area.type == iso9660::Area::Type::FILE_EXTENT && area.name == "/SLES_500.09"; });

                    if(exe_it != area_map.end() && movies_it != area_map.end())
                    {
                        int32_t sample_start = find_sample_offset(*ctx.sptd, ctx.drive_config, movies_it->lba);
                        int32_t sample_end = find_sample_offset(*ctx.sptd, ctx.drive_config, exe_it->lba);

                        int32_t movies_offset = sample_start - lba_to_sample(movies_it->lba, 0);
                        int32_t exe_offset = sample_end - lba_to_sample(exe_it->lba, 0);

                        std::string offset_message;
                        if(movies_offset != exe_offset)
                            offset_message = std::format(", offset: {:+}", exe_offset);

                        LOG("protection: PS2/WWR Ring, range: {}-{}{}", movies_it->lba, exe_it->lba, offset_message);
                        ctx.protection.emplace_back(sample_start, sample_end);

                        break;
                    }
                }
            }
        }
    }

    return exit_code;
}

}
