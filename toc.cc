#include <fmt/format.h>
#include <map>
#include <set>
#include <cctype>
#include <codecvt>
#include "cd.hh"
#include "common.hh"
#include "crc16_gsm.hh"
#include "endian.hh"
#include "logger.hh"
#include "toc.hh"



namespace gpsxre
{

const char TOC::_ISRC_TABLE[] =
{
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_', '_', '_', '_', '_', '_',
	'_', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '_', '_', '_', '_', '_',
	'_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_'
};


TOC::TOC(const std::vector<uint8_t> &toc_buffer, bool full_toc)
	: disc_type(DiscType::UNKNOWN)
{
	if(full_toc)
		InitFullTOC(toc_buffer);
	else
		InitTOC(toc_buffer);
}


TOC::TOC(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start)
	: disc_type(DiscType::UNKNOWN)
{
	bool track_active = false;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		int32_t lba = lba_start + lba_index;

		auto &Q = subq[lba_index];

		if(Q.Valid())
		{
			uint8_t adr = Q.control_adr & 0x0F;
			if(adr == 1)
			{
				if(Q.mode1.tno)
				{
					uint8_t tno = bcd_decode(Q.mode1.tno);

					// new session
					if(sessions.empty() || !sessions.back().tracks.empty() && sessions.back().tracks.back().track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER) && tno != bcd_decode(CD_LEADOUT_TRACK_NUMBER))
					{
						uint8_t session_number = sessions.empty() ? 1 : sessions.back().session_number + 1;
						sessions.emplace_back().session_number = session_number;
					}

					Session &s = sessions.back();

					// new track
					if(s.tracks.empty() || s.tracks.back().track_number != tno && (s.tracks.back().track_number + 1 == tno || tno == bcd_decode(CD_LEADOUT_TRACK_NUMBER)))
					{
						if(!s.tracks.empty())
							s.tracks.back().lba_end = lba;

						auto &t = s.tracks.emplace_back();

						t.track_number = tno;
						t.control = Q.control_adr >> 4;
						t.lba_start = lba;
						t.lba_end = lba;
						t.data_mode = 0;
						t.cdi = false;
					}

					Session::Track &t = s.tracks.back();

					uint8_t index = bcd_decode(Q.mode1.index);
					if(index == t.indices.size() + 1)
						t.indices.push_back(lba);

					track_active = true;
				}
				// lead-in
				else
					track_active = false;
			}
			// MCN & ISRC
			else if(adr == 2 || adr == 3)
			{
				;
			}
			// CD-R / CD-RW
			else if(adr == 5)
			{
				track_active = false;
			}
			else
			{
				track_active = false;
			}
		}

		if(track_active)
			sessions.back().tracks.back().lba_end = lba + 1;
	}

	// if pre-gap is missing a few first sectors (LG/ASUS for instance)
	// extend it to the default size, it will be handled later during split
	uint32_t pregap_count = sessions.front().tracks.front().lba_start - MSF_LBA_SHIFT;
	for(auto &s : sessions)
		s.tracks.front().lba_start -= pregap_count;
}


void TOC::DeriveINDEX(const TOC &toc)
{
	for(uint32_t i = 0; i < sessions.size(); ++i)
	{
		auto &s = sessions[i];
		for(auto &t : s.tracks)
		{
			// don't update intermediate lead-in
			if(t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER) && i + 1 < sessions.size())
				continue;

			for(auto const &toc_s : toc.sessions)
				for(auto const &toc_t : toc_s.tracks)
				{
					if(t.track_number == toc_t.track_number)
					{
						t.indices = toc_t.indices;
						break;
					}
				}
		}
	}


}


void TOC::UpdateQ(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start)
{
	// pre-gap
	for(uint32_t i = 0; i < sessions.size(); ++i)
	{
		Session &s = sessions[i];
		Session::Track &t = s.tracks.front();

		for(int32_t lba = i ? sessions[i - 1].tracks.back().lba_end : lba_start; lba <= t.indices.front(); ++lba)
		{
			uint32_t lba_index = lba - lba_start;
			if(lba_index >= sectors_count)
				break;

			auto &Q = subq[lba_index];
			if(!Q.Valid())
				continue;

			uint8_t adr = Q.control_adr & 0x0F;
			if(adr == 1)
			{
				uint8_t tno = bcd_decode(Q.mode1.tno);
				if(tno == t.track_number)
				{
					t.lba_start = lba;
					break;
				}
			}
		}
	}

	// if pre-gap is missing a few first sectors (LG/ASUS for instance)
	// extend it to the default size, it will be handled later during split
	uint32_t pregap_count = sessions.front().tracks.front().lba_start - MSF_LBA_SHIFT;
	for(auto &s : sessions)
		s.tracks.front().lba_start -= pregap_count;

	// lead-out
	for(uint32_t i = 0; i < sessions.size(); ++i)
	{
		Session &s = sessions[i];
		Session::Track &t = s.tracks.back();

		int32_t lba_end = lba_start + (int32_t)sectors_count;
		if(i + 1 < sessions.size() && lba_end > sessions[i + 1].tracks.front().lba_start)
			lba_end = sessions[i + 1].tracks.front().lba_start;

		int32_t lba = t.lba_start;
		for(; lba < lba_end; ++lba)
		{
			uint32_t lba_index = lba - lba_start;

			auto &Q = subq[lba_index];
			if(!Q.Valid())
				continue;

			uint8_t adr = Q.control_adr & 0x0F;
			if(adr == 1)
			{
				uint8_t tno = bcd_decode(Q.mode1.tno);

				// next session lead-in
				if(!tno)
					break;
			}
		}

		t.lba_end = lba;
	}

	// track boundaries
	for(auto &s : sessions)
	{
		for(uint32_t i = 1; i < (uint32_t)s.tracks.size(); ++i)
		{
			int32_t lba = s.tracks[i - 1].indices.front();
			int32_t lba_end = std::min(s.tracks[i].indices.front(), lba_start + (int32_t)sectors_count);
			for(; lba < lba_end; ++lba)
			{
				uint32_t lba_index = lba - lba_start;

				auto &Q = subq[lba_index];
				if(!Q.Valid())
					continue;

				uint8_t adr = Q.control_adr & 0x0F;
				if(adr == 1)
				{
					uint8_t tno = bcd_decode(Q.mode1.tno);
					if(tno == s.tracks[i].track_number)
					{
						uint8_t index = bcd_decode(Q.mode1.index);

						// no gap, preserve TOC configuration
						if(index)
							lba = s.tracks[i].lba_start;

						break;
					}
				}
			}

			s.tracks[i - 1].lba_end = lba;
			s.tracks[i].lba_start = lba;
		}
	}

	UpdateINDEX(subq, sectors_count, lba_start);
}


void TOC::UpdateMCN(const ChannelQ *subq, uint32_t sectors_count)
{
	// build linear track array
	std::vector<Session::Track *> tracks;
	for(auto &s : sessions)
		for(auto &t : s.tracks)
			tracks.push_back(&t);
	int32_t track_index = -1;

	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		auto &Q = subq[lba_index];

		if(!Q.Valid())
			continue;

		uint8_t adr = Q.control_adr & 0x0F;
		switch(adr)
		{
		case 1:
			// tracks
			if(Q.mode1.tno != 0)
			{
				uint8_t tno = bcd_decode(Q.mode1.tno);
				if(track_index + 1 < (int32_t)tracks.size() && tracks[track_index + 1]->track_number == tno)
					++track_index;
			}
			break;

			// MCN
		case 2:
			if(mcn.empty())
			{
				for(uint32_t i = 0; i < sizeof(Q.mode2.mcn); ++i)
					mcn += fmt::format("{:02}", bcd_decode(Q.mode2.mcn[i]));
				mcn.pop_back();
			}
			break;

			// ISRC
		case 3:
			if(track_index >= 0)
			{
				if(tracks[track_index]->isrc.empty())
				{
					// letters, 5 by 6 bits each
					for(uint32_t i = 0; i < 5; ++i)
					{
						uint8_t c = 0;
						bit_copy(&c, 2, Q.mode3.isrc, i * 6, 6);
						tracks[track_index]->isrc += _ISRC_TABLE[c];
					}

					// padding 2 bits

					// 7 BCD 4-bit numbers
					for(uint32_t i = 4; i < 8; ++i)
						tracks[track_index]->isrc += fmt::format("{:02}", bcd_decode(Q.mode3.isrc[i]));
					tracks[track_index]->isrc.pop_back();
				}
			}
			break;

			// CD-R / CD-RW
		case 5:
			break;

		default:
			;
		}
	}
}


bool TOC::UpdateCDTEXT(const std::vector<uint8_t> &cdtext_buffer)
{
	bool success = true;

	auto descriptors_count = (uint16_t)((cdtext_buffer.size() - sizeof(READ_TOC_Response)) / sizeof(CD_TEXT_Descriptor));
	auto descriptors = (CD_TEXT_Descriptor *)(cdtext_buffer.data() + sizeof(READ_TOC_Response));

	// load first available block size information structure and block number statistics
	BlockSizeInfo block_size_info;
	bool bsi_found = false;
	std::set<uint8_t> blocks;
	for(uint16_t i = 0; i < descriptors_count; ++i)
	{
		auto &pack_data = descriptors[i];

		//DEBUG
//		LOG("{:02X} {:02} {:b} {:02} {:02} {:01} {:b} {}",
//								 pack_data.pack_type, pack_data.track_number, pack_data.extension_flag, pack_data.sequence_number, pack_data.character_position, pack_data.block_number, pack_data.unicode,
//								 DescriptorText(pack_data));

		auto crc = crc16_gsm((uint8_t *)&pack_data, sizeof(pack_data) - sizeof(uint16_t));
		// PLEXTOR PX-W5224TA: crc of last pack is always zeroed
		if(crc != endian_swap(pack_data.crc) && i + 1 != descriptors_count)
		{
			success = false;
			break;
		}

		if(pack_data.extension_flag)
			continue;

		if((PackType)pack_data.pack_type == PackType::SIZE_INFO)
		{
			if(!bsi_found)
			{
				memcpy((char *)&block_size_info + pack_data.track_number * sizeof(pack_data.text), pack_data.text, sizeof(pack_data.text));
				if(pack_data.track_number == 2)
					bsi_found = true;
			}
		}
		else
			blocks.insert(pack_data.block_number);
	}

	if(!success)
		return success;

	// dynamically renumerate blocks and store only what's available
	std::vector<uint8_t> blocks_map(8);
	for(auto const &b : blocks)
	{
		blocks_map[b] = (uint8_t)cd_text_lang.size();
		cd_text_lang.push_back(bsi_found ? block_size_info.language_code[b] : 0xFF - sizeof(block_size_info.language_code) + b);
	}

	uint8_t first_track, tracks_count = 0;
	CharacterCode character_code = CharacterCode::ISO_8859_1;
	if(bsi_found)
	{
		first_track = block_size_info.first_track;
		tracks_count = block_size_info.last_track - block_size_info.first_track + 1;
		character_code = (CharacterCode)block_size_info.character_code;
	}
	// PLEXTOR PX-W5224TA: no block size info for multisession cd-text
	else
	{
		first_track = sessions.front().tracks.front().track_number;
		for(auto const &s : sessions)
			tracks_count += (uint8_t)s.tracks.size();
	}

	cd_text.resize(cd_text_lang.size());
	for(auto &s : sessions)
		for(auto &t : s.tracks)
			t.cd_text.resize(cd_text_lang.size());

	for(uint16_t i = 0; i < descriptors_count;)
	{
		auto &pack_data = descriptors[i];

		if(pack_data.extension_flag)
			continue;

		auto pack_type = (PackType)pack_data.pack_type;
		if(IsTextPack(pack_type))
		{
			std::vector<char> text;
			for(; i < descriptors_count && (PackType)descriptors[i].pack_type == pack_type; ++i)
				text.insert(text.end(), descriptors[i].text, descriptors[i].text + sizeof(descriptors[i].text));
			std::vector<char *> track_texts;

			if(pack_data.unicode)
			{
				uint16_t *text_unicode = (uint16_t *)text.data();
				uint32_t unicode_size = (uint32_t)text.size() / sizeof(uint16_t);
				for(uint32_t j = 0; j < unicode_size; ++j)
				{
					if(!j || text_unicode[j - 1] == 0)
					{
						auto t = (char *)&text_unicode[j];
						if(!strcmp(t, "\t\t") && !track_texts.empty())
							t = track_texts.back();
						track_texts.push_back(t);
					}
				}
			}
			else
			{
				for(uint32_t j = 0; j < text.size(); ++j)
				{
					if(!j || text[j - 1] == '\0')
					{
						auto t = &text[j];
						if(!strcmp(t, "\t") && !track_texts.empty())
							t = track_texts.back();
						track_texts.push_back(t);
					}
				}
			}

			uint32_t count = std::min((uint32_t)tracks_count + 1, (uint32_t)track_texts.size());
			for(uint32_t j = 0; j < count; ++j)
			{
				CDText *cdt = GetCDText(blocks_map[pack_data.block_number], j ? first_track + j - 1 : 0);

				if(!cdt)
					continue;

				auto decoded_text = DecodeText(track_texts[j], pack_data.unicode, cd_text_lang[blocks_map[pack_data.block_number]], character_code);

				if(pack_type == PackType::TITLE)
					cdt->title = decoded_text;
				else if(pack_type == PackType::PERFORMER)
					cdt->performer = decoded_text;
				else if(pack_type == PackType::SONGWRITER)
					cdt->songwriter = decoded_text;
				else if(pack_type == PackType::COMPOSER)
					cdt->composer = decoded_text;
				else if(pack_type == PackType::ARRANGER)
					cdt->arranger = decoded_text;
				else if(pack_type == PackType::MESSAGE)
					cdt->message = decoded_text;
				else if(pack_type == PackType::CLOSED_INFO)
					cdt->closed_info = decoded_text;
				else if(pack_type == PackType::MCN_ISRC)
					cdt->mcn_isrc = decoded_text;
			}
		}
		else
		{
			switch(pack_type)
			{
			case PackType::DISC_ID:
				;
				break;

			case PackType::GENRE_ID:
				;
				break;

			case PackType::TOC:
				;
				break;

			case PackType::TOC2:
				;
				break;

			case PackType::RESERVED1:
			case PackType::RESERVED2:
			case PackType::RESERVED3:
				;
				break;

			default:
				;
			}

			++i;
		}
	}

	return success;
}


void TOC::GenerateIndex0()
{
	for(auto &s : sessions)
	{
		for(uint32_t i = 0; i < s.tracks.size(); ++i)
		{
			auto &t = s.tracks[i];
			if(!t.indices.empty())
				t.lba_start = t.indices.front() + MSF_LBA_SHIFT;
			if(i)
				s.tracks[i - 1].lba_end = t.lba_start;
		}
	}
}


void TOC::Print() const
{
	bool multisession = sessions.size() > 1;

	// disc type
	if(disc_type != DiscType::UNKNOWN)
	{
		std::string disc_type_string("UNKNOWN");
		if(disc_type == DiscType::CD_DA)
			disc_type_string = "CD-DA / CD-DATA";
		else if(disc_type == DiscType::CD_I)
			disc_type_string = "CD-I";
		else if(disc_type == DiscType::CD_XA)
			disc_type_string = "CD-XA";
		LOG("{}disc type: {}", std::string(2, ' '), disc_type_string);
	}

	for(auto const &s : sessions)
	{
		if(multisession)
			LOG("{}session {}", std::string(2, ' '), s.session_number);

		for(auto const &t : s.tracks)
		{
			std::string flags(t.control & (uint8_t)ChannelQ::Control::DATA ? " data" : "audio");
			if(t.control & (uint8_t)ChannelQ::Control::FOUR_CHANNEL)
				flags += ", four-channel";
			if(t.control & (uint8_t)ChannelQ::Control::DIGITAL_COPY)
				flags += ", dcp";
			if(t.control & (uint8_t)ChannelQ::Control::PRE_EMPHASIS)
				flags += ", pre-emphasis";

			LOG("{}track {} {{ {} }}", std::string(multisession ? 4 : 2, ' '), TrackString(t.track_number), flags);

			auto indices = t.indices;
			indices.insert(indices.begin(), t.lba_start);
			indices.push_back(t.lba_end);

			for(uint32_t i = 1; i < (uint32_t)indices.size(); ++i)
			{
				int32_t index_start = indices[i - 1];
				int32_t index_end = indices[i];

				int32_t index_length = index_end - index_start;

				// skip index 0
				if(i == 1 && index_length <= 0)
					continue;

				std::string index_properties;

				MSF msf_start = LBA_to_MSF(index_start);
				if(index_length > 0)
				{
					MSF msf_end = LBA_to_MSF(t.lba_end - 1);
					index_properties = fmt::format("LBA: {:6} .. {:6}, length: {:6}, MSF: {:02}:{:02}:{:02}-{:02}:{:02}:{:02}",
												   index_start, index_end - 1, index_length,
												   msf_start.m, msf_start.s, msf_start.f, msf_end.m, msf_end.s, msf_end.f);
				}
				else
					index_properties = fmt::format("LBA: {:6}, MSF: {:02}:{:02}:{:02}", index_start, msf_start.m, msf_start.s, msf_start.f);

				LOG("{}index {:02} {{ {} }}", std::string(multisession ? 6 : 4, ' '), i - 1, index_properties);
			}
		}
	}
}


std::ostream &TOC::PrintCUE(std::ostream &os, const std::string &image_name, uint32_t cd_text_index) const
{
	bool multisession = sessions.size() > 1;

	std::string mcn_print(mcn);
	if(mcn_print.empty() && cd_text_index < cd_text.size() && !cd_text[cd_text_index].mcn_isrc.empty())
		mcn_print = "0" + cd_text[cd_text_index].mcn_isrc;
	if(!mcn_print.empty())
		os << fmt::format("CATALOG {}", mcn_print) << std::endl;
	if(cd_text_index < cd_text.size())
		PrintCDTextCUE(os, cd_text[cd_text_index], 0);

	for(uint32_t j = 0; j < sessions.size(); ++j)
	{
		auto &s = sessions[j];

		// output standard sizes here for now
		// can be calculated precisely if whole lead-out/toc/pre-gap range is dumped
		if(multisession)
		{
			MSF msf;
			if(j)
			{
				msf = LBA_to_MSF(CD_LEADOUT_MIN_SIZE + MSF_LBA_SHIFT);
				os << fmt::format("REM LEAD-OUT {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
			}
			os << fmt::format("REM SESSION {:02}", s.session_number) << std::endl;
			if(j)
			{
				msf = LBA_to_MSF(CD_LEADIN_MIN_SIZE + MSF_LBA_SHIFT);
				os << fmt::format("REM LEAD-IN {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
				msf = LBA_to_MSF(CD_PREGAP_SIZE + MSF_LBA_SHIFT);
				os << fmt::format("REM PREGAP {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
			}
		}

		for(auto const &t : s.tracks)
		{
			// skip lead-in and lead-out tracks
			if(t.track_number == 0x00 || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER))
				continue;

			os << fmt::format("FILE \"{}{}.bin\" BINARY", image_name, TracksCount() > 1 ? fmt::format(" (Track {})", TrackString(t.track_number)) : "") << std::endl;

			std::string track_type;
			if(t.control & (uint8_t)ChannelQ::Control::DATA)
			{
				std::string track_mode;
				if(t.cdi)
					track_mode = "CDI";
				else
					track_mode = fmt::format("MODE{}", t.data_mode);
				track_type = fmt::format("{}/2352", track_mode);
			}
			else
				track_type = "AUDIO";

			os << fmt::format("  TRACK {:02} {}", t.track_number, track_type) << std::endl;
			if(cd_text_index < t.cd_text.size())
				PrintCDTextCUE(os, t.cd_text[cd_text_index], 4);

			std::string isrc_print(t.isrc);
			if(isrc_print.empty() && cd_text_index < t.cd_text.size() && !t.cd_text[cd_text_index].mcn_isrc.empty())
				isrc_print = t.cd_text[cd_text_index].mcn_isrc;
			if(!isrc_print.empty())
				os << fmt::format("    ISRC {}", isrc_print) << std::endl;

			std::string flags;
			if(t.control & (uint8_t)ChannelQ::Control::FOUR_CHANNEL)
				flags += " 4CH";
			if(t.control & (uint8_t)ChannelQ::Control::DIGITAL_COPY)
				flags += " DCP";
			if(t.control & (uint8_t)ChannelQ::Control::PRE_EMPHASIS)
				flags += " PRE";
			if(!flags.empty())
				os << fmt::format("    FLAGS{}", flags) << std::endl;

			if(!t.indices.empty())
			{
				for(uint32_t i = 0; i <= t.indices.size(); ++i)
				{
					if(!i && t.indices[i] == t.lba_start)
						continue;
					MSF msf = LBA_to_MSF((i == 0 ? 0 : t.indices[i - 1] - t.lba_start) + MSF_LBA_SHIFT);
					os << fmt::format("    INDEX {:02} {:02}:{:02}:{:02}", i, msf.m, msf.s, msf.f) << std::endl;
				}
			}
		}
	}

	return os;
}


void TOC::InitTOC(const std::vector<uint8_t> &toc_buffer)
{
	// don't rely on anything and sort by session number / track number
	std::map<uint8_t, Session::Track> tracks;

	auto descriptors_count = (uint16_t)((toc_buffer.size() - sizeof(READ_TOC_Response)) / sizeof(TOC_Descriptor));
	auto descriptors = (TOC_Descriptor *)(toc_buffer.data() + sizeof(READ_TOC_Response));

	for(uint16_t i = 0; i < descriptors_count; ++i)
	{
		auto &d = descriptors[i];

		if(d.track_number < CD_TRACKS_COUNT || d.track_number == CD_LEADOUT_TRACK_NUMBER)
		{
			// renumerate lead-out track to align it with subchannel
			uint8_t track_number = d.track_number == CD_LEADOUT_TRACK_NUMBER ? bcd_decode(CD_LEADOUT_TRACK_NUMBER) : d.track_number;

			int32_t lba = endian_swap(d.track_start_address);

			auto &t = tracks[track_number];
			t.track_number = track_number;

			// [CDI] Op Jacht naar Vernuft (Netherlands)
			// make sure there are no duplicate entries, always use the latest one
			t.indices.clear();

			t.indices.push_back(lba);
			t.control = d.control;
			t.lba_start = lba;
			t.lba_end = lba;
			t.data_mode = 0;
			t.cdi = false;
		}
	}

	Session &s = sessions.emplace_back();
	s.session_number = 1;
	s.tracks.reserve(tracks.size());
	for(auto const &t : tracks)
		s.tracks.push_back(t.second);
}


void TOC::InitFullTOC(const std::vector<uint8_t> &toc_buffer)
{
	// don't rely on anything and sort by session number / track number
	std::map<uint8_t, std::map<uint8_t, Session::Track>> tracks;

	auto descriptors_count = (uint16_t)((toc_buffer.size() - sizeof(READ_TOC_Response)) / sizeof(FULL_TOC_Descriptor));
	auto descriptors = (FULL_TOC_Descriptor *)(toc_buffer.data() + sizeof(READ_TOC_Response));

	for(uint16_t i = 0; i < descriptors_count; ++i)
	{
		auto &d = descriptors[i];

		// according to the MMC specs, only Q mode 1 and mode 5 are provided here
		if(d.adr == 1)
		{
			switch(d.point)
			{
			// first track
			case 0xA0:
				disc_type = (DiscType)d.p_msf[1];
				break;

			// last track
			case 0xA1:
				break;

			default:
				if(d.point < CD_TRACKS_COUNT || d.point == 0xA2)
				{
					// renumerate lead-out track to align it with subchannel
					uint8_t track_number = d.point == 0xA2 ? bcd_decode(CD_LEADOUT_TRACK_NUMBER) : d.point;

					int32_t lba = MSF_to_LBA(*(MSF *)d.p_msf);

					auto &t = tracks[d.session_number][track_number];
					t.track_number = track_number;

					// make sure there are no duplicate entries, always use the latest one
					t.indices.clear();

					t.indices.push_back(lba);
					t.control = d.control;
					t.lba_start = lba;
					t.lba_end = lba;
					t.data_mode = 0;
					t.cdi = false;
				}
			}
		}
		// CD-R / CD-RW
		else if(d.adr == 5)
		{
			;
		}
	}

	for(auto const &t : tracks)
	{
		Session s;
		s.session_number = t.first;
		s.tracks.reserve(t.second.size());
		for(auto const &tt : t.second)
			s.tracks.push_back(tt.second);

		sessions.push_back(s);
	}
}


void TOC::UpdateINDEX(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start)
{
	for(auto &s : sessions)
	{
		for(auto &t : s.tracks)
		{
			for(int32_t lba = t.lba_start; lba < t.lba_end; ++lba)
			{
				uint32_t lba_index = lba - lba_start;
				if(lba_index >= sectors_count)
					break;

				auto &Q = subq[lba_index];

				if(!Q.Valid())
					continue;

				uint8_t adr = Q.control_adr & 0x0F;
				if(adr == 1)
				{
					uint8_t tno = bcd_decode(Q.mode1.tno);
					uint8_t index = bcd_decode(Q.mode1.index);
					if(tno == t.track_number && index == t.indices.size() + 1)
						t.indices.push_back(lba);
				}
			}
		}
	}
}


std::ostream &TOC::PrintCDTextCUE(std::ostream &os, const CDText &cdt, uint32_t indent_level)
{
	if(!cdt.title.empty())
		os << fmt::format("{}TITLE \"{}\"", std::string(indent_level, ' '), cdt.title) << std::endl;
	if(!cdt.performer.empty())
		os << fmt::format("{}PERFORMER \"{}\"", std::string(indent_level, ' '), cdt.performer) << std::endl;
	if(!cdt.songwriter.empty())
		os << fmt::format("{}SONGWRITER \"{}\"", std::string(indent_level, ' '), cdt.songwriter) << std::endl;
//	if(!cdt.composer.empty())
//		os << fmt::format("{}REM COMPOSER \"{}\"", std::string(indent_level, ' '), cdt.composer) << std::endl;
//	if(!cdt.arranger.empty())
//		os << fmt::format("{}REM ARRANGER \"{}\"", std::string(indent_level, ' '), cdt.arranger) << std::endl;

	return os;
}


bool TOC::IsTextPack(PackType pack_type)
{
	return pack_type == PackType::TITLE || pack_type == PackType::PERFORMER || pack_type == PackType::SONGWRITER || pack_type == PackType::COMPOSER || pack_type == PackType::ARRANGER ||
		pack_type == PackType::MESSAGE || pack_type == PackType::CLOSED_INFO || pack_type == PackType::MCN_ISRC;
}


TOC::CDText *TOC::GetCDText(uint8_t index, uint8_t track_number)
{
	CDText *cdt = nullptr;

	if(track_number)
	{
		for(auto &s : sessions)
			for(auto &t : s.tracks)
				if(t.track_number == track_number)
				{
					cdt = &t.cd_text[index];
					break;
				}
	}
	else
		cdt = &cd_text[index];

	return cdt;
}


std::string TOC::TrackString(uint8_t track_number) const
{
	std::string track_string;

	auto width = TrackNumberWidth();
	if(track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER))
		track_string = std::string(width, 'A');
	else
		track_string = fmt::vformat(fmt::format("{{:0{}}}", width), fmt::make_format_args(track_number));

	return track_string;
}


uint32_t TOC::TracksCount() const
{
	uint32_t tracks_count = 0;

	for(auto &s : sessions)
		for(auto &t : s.tracks)
			if(t.track_number != 00 && t.track_number != bcd_decode(CD_LEADOUT_TRACK_NUMBER))
				++tracks_count;

	return tracks_count;
}


uint32_t TOC::TrackNumberWidth() const
{
	uint8_t track_number = 0;
	for(auto &s : sessions)
		for(auto &t : s.tracks)
			if(t.track_number != bcd_decode(CD_LEADOUT_TRACK_NUMBER) && track_number < t.track_number)
				track_number = t.track_number;

	return (track_number ? log10(track_number) : 0) + 1;
}


std::string TOC::DecodeText(const char *text, bool unicode, uint8_t language_code, CharacterCode character_code) const
{
	//FIXME: codecvt is deprecated

	return unicode ? std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(std::u16string((char16_t*)text)) : std::string(text);
}


std::string TOC::DescriptorText(const CD_TEXT_Descriptor &descriptor) const
{
	std::string text;

	if(descriptor.unicode)
	{
		uint16_t *text_unicode = (uint16_t *)descriptor.text;
		for(uint32_t i = 0; i < sizeof(descriptor.text) / sizeof(uint16_t); ++i)
		{
			text += text_unicode[i] ? fmt::format("\\U{:04X}", text_unicode[i]) : "|";
		}
	}
	else
	{
		for(uint32_t i = 0; i < sizeof(descriptor.text); ++i)
		{
			if(isprint(descriptor.text[i]))
				text += (char)descriptor.text[i];
			else
				text += descriptor.text[i] ? fmt::format("\\x{:02X}", descriptor.text[i]) : "|";
		}
	}

	return text;
}

}
