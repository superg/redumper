module;
#include <algorithm>
#include <memory>
#include <optional>
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
import filesystem.iso9660;
import options;
import readers.disc_read_cdda_reader;
import scsi.cmd;
import utils.logger;
import utils.misc;



namespace gpsxre
{

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

            auto data_reader = std::make_unique<Disc_READ_CDDA_Reader>(*ctx.sptd, ctx.drive_config, t.indices.front());

            auto area_map = iso9660::area_map(data_reader.get(), s.tracks[i + 1].lba_start - t.indices.front());
            if(area_map.empty())
                continue;

            LOG("ISO9660 map: ");
            for(auto const &area : area_map)
            {
                auto count = scale_up(area.size, FORM1_DATA_SIZE);
                LOG("LBA: [{:6} .. {:6}), count: {:6}, sample: [{:9} .. {:9}), size: {:9}, type: {}{}", area.lba, area.lba + count, count, area.sample_start, area.sample_end, area.size,
                    iso9660::area_type_to_string(area.type), area.name.empty() ? "" : std::format(", name: {}", area.name));
            }
            LOG("");

            bool generic_protection = true;

            // Datel V2
            iso9660::PrimaryVolumeDescriptor pvd;
            if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
            {
                if(iso9660::identifier_to_string(pvd.volume_identifier) == "CRAZY_TAXI")
                {
                    for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
                    {
                        const auto &a1 = area_map[i];
                        const auto &a2 = area_map[i + 1];

                        if(a1.sample_end < a2.sample_start && a1.type == iso9660::Area::Type::DIRECTORY_EXTENT && a2.type == iso9660::Area::Type::FILE_EXTENT)
                        {
                            int32_t directory_offset = a1.sample_start - lba_to_sample(a1.lba, 0);
                            int32_t file_offset = a2.sample_start - lba_to_sample(a2.lba, 0);

                            std::string offset_message;
                            if(directory_offset != file_offset)
                                offset_message = std::format(", offset: {:+}", file_offset);

                            LOG("protection: PS2/Datel Ring, range: {}-{}{}", a1.lba + scale_up(a1.size, FORM1_DATA_SIZE), a2.lba, offset_message);
                            ctx.protection_soft.emplace_back(a1.sample_end, a2.sample_start);

                            generic_protection = false;
                            break;
                        }
                    }
                }
            }

            if(generic_protection)
            {
                LOG("ISO9660 rings: ");
                for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
                {
                    const auto &a1 = area_map[i];
                    const auto &a2 = area_map[i + 1];

                    if(a1.sample_end < a2.sample_start)
                    {
                        LOG("  LBA: [{:6}, {:6}), sample: [{:9} .. {:9}]", a1.lba, a2.lba, a1.sample_end, a2.sample_start);
                        ctx.protection_soft.emplace_back(a1.sample_end, a2.sample_start);
                    }
                }
            }
        }
    }

    return exit_code;
}

}
