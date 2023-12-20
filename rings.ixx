module;
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>
#include "throw_line.hh"

export module rings;

import cd.cd;
import cd.cdrom;
import cd.scrambler;
import cd.subcode;
import dump;
import filesystem.iso9660;
import options;
import readers.disc_read_form1_reader;
import readers.sector_reader;
import scsi.cmd;
import utils.logger;
import utils.misc;



namespace gpsxre
{


export void redumper_rings(Context &ctx, Options &options)
{
	if(!profile_is_cd(ctx.current_profile))
		throw_line("rings command requires CD profile (current profile: 0x{:02X})", (uint16_t)ctx.current_profile);

	std::vector<uint8_t> toc_buffer = cmd_read_toc(*ctx.sptd);
	std::vector<uint8_t> full_toc_buffer = cmd_read_full_toc(*ctx.sptd);
	auto toc = choose_toc(toc_buffer, full_toc_buffer);

	std::optional<int32_t> write_offset;

	std::vector<iso9660::Area> area_map;
	for(auto &s : toc.sessions)
	{
		for(uint32_t i = 0; i + 1 < s.tracks.size(); ++i)
		{
			auto &t = s.tracks[i];

			if(!(t.control & (uint8_t)ChannelQ::Control::DATA) || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER) || t.indices.empty())
				continue;

			uint32_t track_start = t.indices.front();
			uint32_t sectors_count = s.tracks[i + 1].lba_start - track_start;

			if(!write_offset)
				write_offset = track_offset_by_sync(ctx, track_start, std::min(iso9660::SYSTEM_AREA_SIZE, sectors_count));

			std::unique_ptr<SectorReader> sector_reader = std::make_unique<Disc_READ_Reader>(*ctx.sptd, track_start);

			auto am = iso9660::area_map(sector_reader.get(), track_start, sectors_count);
			area_map.insert(area_map.end(), am.begin(), am.end());
		}
	}
	if(area_map.empty())
		return;

	LOG("ISO9660 map: ");
	std::for_each(area_map.cbegin(), area_map.cend(), [](const iso9660::Area &area)
	{
		auto count = scale_up(area.size, FORM1_DATA_SIZE);
		LOG("LBA: [{:6} .. {:6}], count: {:6}, type: {}{}",
			area.offset, area.offset + count - 1, count, enum_to_string(area.type, iso9660::AREA_TYPE_STRING),
			area.name.empty() ? "" : std::format(", name: {}", area.name));
	});
	LOG("");

	std::vector<std::pair<int32_t, int32_t>> sector_rings;
	for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
	{
		auto &a = area_map[i];
		
		uint32_t gap_start = a.offset + scale_up(a.size, FORM1_DATA_SIZE);
		if(gap_start < area_map[i + 1].offset)
			sector_rings.emplace_back(gap_start, area_map[i + 1].offset);
	}

	if(!sector_rings.empty())
	{
		LOG("ISO9660 rings: ");
		for(auto r : sector_rings)
			LOG("  [{:6}, {:6})", r.first, r.second);
		LOG("");
	}

	std::vector<std::pair<int32_t, int32_t>> rings;
	for(auto r : sector_rings)
		rings.emplace_back(lba_to_sample(r.first, *write_offset), lba_to_sample(r.second, *write_offset));

	ctx.rings = rings;
}

}
