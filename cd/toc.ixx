module;
#include <cctype>
#include <cstdint>
#include <format>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

export module cd.toc;

import cd.cd;
import cd.subcode;
import crc.crc16_gsm;
import scsi.mmc;
import utils.endian;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export struct TOC
{
	struct CDText
	{
		std::string title;
		std::string performer;
		std::string songwriter;
		std::string composer;
		std::string arranger;
		std::string message;
		std::string closed_info;
		std::string mcn_isrc;
	};

	struct Session
	{
		uint32_t session_number;
		struct Track
		{
			uint32_t track_number;
			int32_t lba_start;
			int32_t lba_end;
			uint8_t control;

			std::vector<int32_t> indices;
			std::string isrc;
			std::vector<CDText> cd_text;

			// supplemental
			uint8_t data_mode;
			bool cdi;
		};
		std::vector<Track> tracks;
	};

	std::vector<Session> sessions;

	bool qtoc;

	// supplemental
	std::string mcn;
	std::vector<CDText> cd_text;
	std::vector<uint8_t> cd_text_lang;


	TOC(const std::vector<uint8_t> &toc_buffer, bool full_toc)
		: qtoc(false)
	{
		if(full_toc)
			initFullTOC(toc_buffer);
		else
			initTOC(toc_buffer);
	}


	TOC(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start)
		: qtoc(true)
	{
		bool track_active = false;
		for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
		{
			int32_t lba = lba_start + lba_index;

			auto &Q = subq[lba_index];

			if(Q.isValid())
			{
				if(Q.adr == 1)
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
							t.control = Q.control;
							t.lba_start = lba;
							t.lba_end = lba;
							t.data_mode = 0;
							t.cdi = false;
						}

						Session::Track &t = s.tracks.back();

						uint8_t index = bcd_decode(Q.mode1.point_index);
						if(index == t.indices.size() + 1)
							t.indices.push_back(lba);

						track_active = true;
					}
					// lead-in
					else
						track_active = false;
				}
				// MCN & ISRC
				else if(Q.adr == 2 || Q.adr == 3)
				{
					;
				}
				// CD-R / CD-RW
				else if(Q.adr == 5)
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


	void deriveINDEX(const TOC &toc)
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


	void updateQ(ChannelQ *subq, const uint8_t *subp, uint32_t sectors_count, int32_t lba_start, bool legacy_subs)
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
				if(!Q.isValid())
					continue;

				if(Q.adr == 1)
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
				if(!Q.isValid())
					continue;

				if(Q.adr == 1)
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
				auto &t = s.tracks[i - 1];
				auto &t_next = s.tracks[i];

				int32_t lba = t.indices.front();
				int32_t lba_next = lba;
				for(; lba_next < std::min(t_next.indices.front(), lba_start + (int32_t)sectors_count); ++lba_next)
				{
					auto &Q = subq[lba_next - lba_start];
					if(!Q.isValid())
						continue;

					if(Q.adr == 1)
					{
						uint8_t tno = bcd_decode(Q.mode1.tno);
						if(tno == t.track_number)
							lba = lba_next + 1;
						else if(tno == t_next.track_number)
						{
							if(!legacy_subs || tno == bcd_decode(CD_LEADOUT_TRACK_NUMBER))
							{
								uint8_t index = bcd_decode(Q.mode1.point_index);

								// no gap, preserve TOC configuration
								if(index)
									lba = lba_next = t_next.lba_start;
							}

							break;
						}
					}
				}

				// if track boundary is not precise (MCN/ISRC), use P
				for(; lba < lba_next; ++lba)
				{
					uint32_t lba_index = lba - lba_start;

					// P is always 1 sector delayed
					if(lba_index + 1 < sectors_count && !subp[lba_index] && subp[lba_index + 1])
						break;
				}

				t.lba_end = lba;
				t_next.lba_start = lba;
			}
		}

		updateINDEX(subq, sectors_count, lba_start);
	}


	void updateMCN(const ChannelQ *subq, uint32_t sectors_count)
	{
		constexpr char ISRC_TABLE[] =
		{
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_', '_', '_', '_', '_', '_',
			'_', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
			'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '_', '_', '_', '_', '_',
			'_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_'
		};

		// build linear track array
		std::vector<Session::Track *> tracks;
		for(auto &s : sessions)
			for(auto &t : s.tracks)
				tracks.push_back(&t);
		int32_t track_index = -1;

		for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
		{
			auto &Q = subq[lba_index];

			if(!Q.isValid())
				continue;

			switch(Q.adr)
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
					for(uint32_t i = 0; i < sizeof(Q.mode23.mcn); ++i)
						mcn += std::format("{:02}", bcd_decode(Q.mode23.mcn[i]));

					// remove trailing zero
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
							bit_copy(&c, 2, Q.mode23.isrc, i * 6, 6);
							tracks[track_index]->isrc += ISRC_TABLE[c];
						}

						// padding 2 bits

						// 7 BCD 4-bit numbers
						for(uint32_t i = 4; i < 8; ++i)
							tracks[track_index]->isrc += std::format("{:02}", bcd_decode(Q.mode23.isrc[i]));
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


	bool updateCDTEXT(const std::vector<uint8_t> &cdtext_buffer)
	{
		bool success = true;

		auto descriptors_count = (uint16_t)((cdtext_buffer.size() - sizeof(CMD_ParameterListHeader)) / sizeof(CD_TEXT_Descriptor));
		auto descriptors = (CD_TEXT_Descriptor *)(cdtext_buffer.data() + sizeof(CMD_ParameterListHeader));

		// load first available block size information structure and block number statistics
		BlockSizeInfo block_size_info;
		bool bsi_found = false;
		std::set<uint8_t> blocks;
		for(uint16_t i = 0; i < descriptors_count; ++i)
		{
			auto &pack_data = descriptors[i];

			//DEBUG
//			LOG("{:02X} {:02} {:b} {:02} {:02} {:01} {:b} {}",
//									 pack_data.pack_type, pack_data.track_number, pack_data.extension_flag, pack_data.sequence_number, pack_data.character_position, pack_data.block_number, pack_data.unicode,
//									 DescriptorText(pack_data));

			auto crc = CRC16_GSM().update((uint8_t *)&pack_data, sizeof(pack_data) - sizeof(uint16_t)).final();
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
			if(isTextPack(pack_type))
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
					CDText *cdt = getCDText(blocks_map[pack_data.block_number], j ? first_track + j - 1 : 0);

					if(!cdt)
						continue;

					auto decoded_text = decodeText(track_texts[j], pack_data.unicode, cd_text_lang[blocks_map[pack_data.block_number]], character_code);

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


	void generateIndex0()
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


	void print(std::ostream &os) const
	{
		bool multisession = sessions.size() > 1;

		for(auto const &s : sessions)
		{
			if(multisession)
				os << std::format("{}session {}", std::string(2, ' '), s.session_number) << std::endl;

			for(auto const &t : s.tracks)
			{
				std::string flags(t.control & (uint8_t)ChannelQ::Control::DATA ? " data" : "audio");
				if(t.control & (uint8_t)ChannelQ::Control::FOUR_CHANNEL)
					flags += ", four-channel";
				if(t.control & (uint8_t)ChannelQ::Control::DIGITAL_COPY)
					flags += ", dcp";
				if(t.control & (uint8_t)ChannelQ::Control::PRE_EMPHASIS)
					flags += ", pre-emphasis";

				os << std::format("{}track {} {{ {} }}", std::string(multisession ? 4 : 2, ' '), getTrackString(t.track_number), flags) << std::endl;

				auto indices = t.indices;
				indices.insert(indices.begin(), t.lba_start);
				indices.push_back(t.lba_end);

				for(uint32_t i = 1; i < (uint32_t)indices.size(); ++i)
				{
					int32_t index_start = indices[i - 1];
					int32_t index_end = indices[i];

					int32_t index_length = index_end - index_start;

					// skip empty index 0
					if(i == 1 && index_length <= 0)
						continue;

					std::string index_properties;

					MSF msf_start = LBA_to_MSF(index_start);
					if(index_length > 0)
					{
						MSF msf_end = LBA_to_MSF(index_end - 1);
						index_properties = std::format("LBA: [{:6} .. {:6}], length: {:6}, MSF: {:02}:{:02}:{:02}-{:02}:{:02}:{:02}",
													   index_start, index_end - 1, index_length,
													   msf_start.m, msf_start.s, msf_start.f, msf_end.m, msf_end.s, msf_end.f);
					}
					else
						index_properties = std::format("LBA: {:6}, MSF: {:02}:{:02}:{:02}", index_start, msf_start.m, msf_start.s, msf_start.f);

					os << std::format("{}index {:02} {{ {} }}", std::string(multisession ? 6 : 4, ' '), i - 1, index_properties) << std::endl;
				}
			}
		}
	}


	std::ostream &printCUE(std::ostream &os, const std::string &image_name, uint32_t cd_text_index) const
	{
		bool multisession = sessions.size() > 1;

		std::string mcn_print(getMCN(mcn, cd_text, cd_text_index));

		// make sure to prepend 0 to 12-digit MCN, total length should always be 13 digits
		if(mcn_print.length() == 12)
		{
			auto mcn_value = str_to_uint64(mcn_print);
			if(mcn_value)
				mcn_print = std::format("{:013}", *mcn_value);
		}

		if(!mcn_print.empty())
			os << std::format("CATALOG {}", mcn_print) << std::endl;

		if(cd_text_index < cd_text.size())
			printCDTextCUE(os, cd_text[cd_text_index], 0);

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
					os << std::format("REM LEAD-OUT {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
				}
				os << std::format("REM SESSION {:02}", s.session_number) << std::endl;
				if(j)
				{
					msf = LBA_to_MSF(CD_LEADIN_MIN_SIZE + MSF_LBA_SHIFT);
					os << std::format("REM LEAD-IN {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
					msf = LBA_to_MSF(CD_PREGAP_SIZE + MSF_LBA_SHIFT);
					os << std::format("REM PREGAP {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
				}
			}

			for(auto const &t : s.tracks)
			{
				// skip lead-in and lead-out tracks
				if(t.track_number == 0x00 || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER))
					continue;

				os << std::format("FILE \"{}{}.bin\" BINARY", image_name, getTracksCount() > 1 ? std::format(" (Track {})", getTrackString(t.track_number)) : "") << std::endl;

				std::string track_type;
				if(t.control & (uint8_t)ChannelQ::Control::DATA)
				{
					std::string track_mode;
					if(t.cdi)
						track_mode = "CDI";
					else
						track_mode = std::format("MODE{}", t.data_mode);
					track_type = std::format("{}/2352", track_mode);
				}
				else
					track_type = "AUDIO";

				os << std::format("  TRACK {:02} {}", t.track_number, track_type) << std::endl;
				if(cd_text_index < t.cd_text.size())
					printCDTextCUE(os, t.cd_text[cd_text_index], 4);

				std::string isrc_print(getMCN(t.isrc, t.cd_text, cd_text_index));
				if(!isrc_print.empty())
					os << std::format("    ISRC {}", isrc_print) << std::endl;

				std::string flags;
				if(t.control & (uint8_t)ChannelQ::Control::FOUR_CHANNEL)
					flags += " 4CH";
				if(t.control & (uint8_t)ChannelQ::Control::DIGITAL_COPY)
					flags += " DCP";
				if(t.control & (uint8_t)ChannelQ::Control::PRE_EMPHASIS)
					flags += " PRE";
				if(!flags.empty())
					os << std::format("    FLAGS{}", flags) << std::endl;

				if(!t.indices.empty())
				{
					for(uint32_t i = 0; i <= t.indices.size(); ++i)
					{
						if(!i && t.indices[i] == t.lba_start)
							continue;
						MSF msf = LBA_to_MSF((i == 0 ? 0 : t.indices[i - 1] - t.lba_start) + MSF_LBA_SHIFT);
						os << std::format("    INDEX {:02} {:02}:{:02}:{:02}", i, msf.m, msf.s, msf.f) << std::endl;
					}
				}
			}
		}

		return os;
	}


	std::string getTrackString(uint8_t track_number) const
	{
		std::string track_string;

		auto width = getTrackNumberWidth();
		if(track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER))
			track_string = std::string(width, 'A');
		else
			track_string = std::vformat(std::format("{{:0{}}}", width), std::make_format_args(track_number));

		return track_string;
	}



	uint32_t getTracksCount() const
	{
		uint32_t tracks_count = 0;

		for(auto &s : sessions)
			for(auto &t : s.tracks)
				if(t.track_number != 00 && t.track_number != bcd_decode(CD_LEADOUT_TRACK_NUMBER))
					++tracks_count;

		return tracks_count;
	}

private:
	enum class PackType : uint8_t
	{
		TITLE = 0x80,
		PERFORMER,
		SONGWRITER,
		COMPOSER,
		ARRANGER,
		MESSAGE,
		DISC_ID,
		GENRE_ID,
		TOC,
		TOC2,
		RESERVED1,
		RESERVED2,
		RESERVED3,
		CLOSED_INFO,
		MCN_ISRC,
		SIZE_INFO
	};

	enum class CharacterCode
	{
		ISO_8859_1,
		ASCII,
		MS_JIS = 0x80  // (Japanese Kanji, double byte characters)
	};

	struct BlockSizeInfo
	{
		uint8_t character_code;
		uint8_t first_track;
		uint8_t last_track;
		uint8_t copyright;
		uint8_t pack_count[16];
		uint8_t sequence_number[8];
		uint8_t language_code[8];
	};


	void initTOC(const std::vector<uint8_t> &toc_buffer)
	{
		// don't rely on anything and sort by session number / track number
		std::map<uint8_t, Session::Track> tracks;

		auto descriptors_count = (uint16_t)((toc_buffer.size() - sizeof(CMD_ParameterListHeader)) / sizeof(TOC_Descriptor));
		auto descriptors = (TOC_Descriptor *)(toc_buffer.data() + sizeof(CMD_ParameterListHeader));

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


	void initFullTOC(const std::vector<uint8_t> &toc_buffer)
	{
		// don't rely on anything and sort by session number / track number
		std::map<uint8_t, std::map<uint8_t, Session::Track>> tracks;

		auto descriptors_count = (uint16_t)((toc_buffer.size() - sizeof(CMD_ParameterListHeader)) / sizeof(FULL_TOC_Descriptor));
		auto descriptors = (FULL_TOC_Descriptor *)(toc_buffer.data() + sizeof(CMD_ParameterListHeader));

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


	void updateINDEX(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start)
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

					if(!Q.isValid())
						continue;

					if(Q.adr == 1)
					{
						uint8_t tno = bcd_decode(Q.mode1.tno);
						uint8_t index = bcd_decode(Q.mode1.point_index);
						if(tno == t.track_number && index == t.indices.size() + 1)
							t.indices.push_back(lba);
					}
				}
			}
		}
	}


	static std::ostream &printCDTextCUE(std::ostream &os, const CDText &cdt, uint32_t indent_level)
	{
		if(!cdt.title.empty())
			os << std::format("{}TITLE \"{}\"", std::string(indent_level, ' '), cdt.title) << std::endl;
		if(!cdt.performer.empty())
			os << std::format("{}PERFORMER \"{}\"", std::string(indent_level, ' '), cdt.performer) << std::endl;
		if(!cdt.songwriter.empty())
			os << std::format("{}SONGWRITER \"{}\"", std::string(indent_level, ' '), cdt.songwriter) << std::endl;
//		if(!cdt.composer.empty())
//			os << std::format("{}REM COMPOSER \"{}\"", std::string(indent_level, ' '), cdt.composer) << std::endl;
//		if(!cdt.arranger.empty())
//			os << std::format("{}REM ARRANGER \"{}\"", std::string(indent_level, ' '), cdt.arranger) << std::endl;

		return os;
	}


	static bool isTextPack(PackType pack_type)
	{
		return pack_type == PackType::TITLE || pack_type == PackType::PERFORMER || pack_type == PackType::SONGWRITER || pack_type == PackType::COMPOSER || pack_type == PackType::ARRANGER ||
			pack_type == PackType::MESSAGE || pack_type == PackType::CLOSED_INFO || pack_type == PackType::MCN_ISRC;
	}


	CDText *getCDText(uint8_t index, uint8_t track_number)
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


	std::string getMCN(const std::string &qtoc_mcn, const std::vector<CDText> &toc_cd_text, uint32_t cd_text_index) const
	{
		std::string toc_mcn(cd_text_index < toc_cd_text.size() ? toc_cd_text[cd_text_index].mcn_isrc : "");

		if(qtoc_mcn.empty())
			return toc_mcn;
		else if(toc_mcn.empty())
			return qtoc_mcn;
		else
			return qtoc ? qtoc_mcn : toc_mcn;
	}


	uint32_t getTrackNumberWidth() const
	{
		uint8_t track_number = 0;
		for(auto &s : sessions)
			for(auto &t : s.tracks)
				if(t.track_number != bcd_decode(CD_LEADOUT_TRACK_NUMBER) && track_number < t.track_number)
					track_number = t.track_number;

		return (track_number ? log10(track_number) : 0) + 1;
	}


	std::string decodeText(const char *text, bool unicode, uint8_t language_code, CharacterCode character_code) const
	{
		//FIXME: codecvt is deprecated
		// sometimes it's not UTF-16
//		return unicode ? std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(std::u16string((char16_t*)text)) : std::string(text);

		// for now copy verbatim
		return std::string(text);
	}


	std::string getDescriptorText(const CD_TEXT_Descriptor &descriptor) const
	{
		std::string text;

		if(descriptor.unicode)
		{
			uint16_t *text_unicode = (uint16_t *)descriptor.text;
			for(uint32_t i = 0; i < sizeof(descriptor.text) / sizeof(uint16_t); ++i)
			{
				text += text_unicode[i] ? std::format("\\U{:04X}", text_unicode[i]) : "|";
			}
		}
		else
		{
			for(uint32_t i = 0; i < sizeof(descriptor.text); ++i)
			{
				if(isprint(descriptor.text[i]))
					text += (char)descriptor.text[i];
				else
					text += descriptor.text[i] ? std::format("\\x{:02X}", descriptor.text[i]) : "|";
			}
		}

		return text;
	}
};

}
