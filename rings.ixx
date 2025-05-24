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
import readers.sector_reader;
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

    std::vector<iso9660::Area> area_map;
    for(auto &s : toc.sessions)
    {
        for(uint32_t i = 0; i + 1 < s.tracks.size(); ++i)
        {
            auto &t = s.tracks[i];

            if(!(t.control & (uint8_t)ChannelQ::Control::DATA) || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER) || t.indices.empty())
                continue;

            auto sector_reader = std::make_unique<Disc_READ_CDDA_Reader>(*ctx.sptd, ctx.drive_config, t.indices.front());

            auto am = iso9660::area_map(sector_reader.get(), s.tracks[i + 1].lba_start - t.indices.front());
            area_map.insert(area_map.begin(), am.begin(), am.end());
        }
    }
    if(area_map.empty())
        return exit_code;

    LOG("ISO9660 map: ");
    for(auto const &area : area_map)
    {
        auto count = scale_up(area.size, FORM1_DATA_SIZE);
        LOG("LBA: [{:6} .. {:6}), count: {:6}, sample: [{:9} .. {:9}), size: {:9}, type: {}{}", area.lba, area.lba + count, count, area.sample_start, area.sample_end,
            area.size, iso9660::area_type_to_string(area.type), area.name.empty() ? "" : std::format(", name: {}", area.name));
    }

    LOG("");
    LOG("ISO9660 sample rings: ");
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

    return exit_code;
}

}
