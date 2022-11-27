#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include "common.hh"
#include "crc32.hh"
#include "ecc_edc.hh"
#include "file_io.hh"
#include "image_browser.hh"
#include "logger.hh"
#include "md5.hh"
#include "scrambler.hh"
#include "sha1.hh"
#include "split.hh"



namespace gpsxre
{

void correct_program_subq(ChannelQ *subq, uint32_t sectors_count)
{
	uint32_t mcn = sectors_count;
	std::map<uint8_t, uint32_t> isrc;
	ChannelQ q_empty;
	memset(&q_empty, 0, sizeof(q_empty));

	uint8_t tno = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!memcmp(&subq[lba_index], &q_empty, sizeof(q_empty)))
			continue;

		if(!subq[lba_index].Valid())
			continue;

		uint8_t adr = subq[lba_index].control_adr & 0x0F;
		if(adr == 1)
			tno = subq[lba_index].mode1.tno;
		else if(adr == 2 && mcn == sectors_count)
			mcn = lba_index;
		else if(adr == 3 && tno && isrc.find(tno) == isrc.end())
			isrc[tno] = lba_index;
	}

	uint32_t q_prev = sectors_count;
	uint32_t q_next = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!memcmp(&subq[lba_index], &q_empty, sizeof(q_empty)))
			continue;

		if(subq[lba_index].Valid())
		{
			uint8_t adr = subq[lba_index].control_adr & 0x0F;
			if(adr == 1)
			{
				if(subq[lba_index].mode1.tno)
					q_prev = lba_index;
				else
					q_prev = sectors_count;
			}
		}
		else
		{
			// find next valid Q
			if(lba_index >= q_next && q_next != sectors_count)
			{
				q_next = lba_index + 1;
				for(; q_next < sectors_count; ++q_next)
					if(subq[q_next].Valid())
					{
						uint8_t adr = subq[q_next].control_adr & 0x0F;
						if(adr == 1)
						{
							if(!subq[q_next].mode1.tno)
								q_next = 0;

							break;
						}
					}
			}

			std::vector<ChannelQ> candidates;
			if(q_prev < lba_index)
			{
				// mode 1
				candidates.emplace_back(subchannel_q_generate_mode1(subq[q_prev], lba_index - q_prev));

				// mode 2
				if(mcn != sectors_count)
					candidates.emplace_back(subchannel_q_generate_mode2(subq[mcn], subq[q_prev], lba_index - q_prev));

				// mode 3
				if(!isrc.empty())
				{
					auto it = isrc.find(subq[q_prev].mode1.tno);
					if(it != isrc.end())
						candidates.emplace_back(subchannel_q_generate_mode3(subq[it->second], subq[q_prev], lba_index - q_prev));
				}
			}

			if(q_next > lba_index && q_next != sectors_count)
			{
				// mode 1
				candidates.emplace_back(subchannel_q_generate_mode1(subq[q_next], lba_index - q_next));

				// mode 2
				if(mcn != sectors_count)
					candidates.emplace_back(subchannel_q_generate_mode2(subq[mcn], subq[q_next], lba_index - q_next));

				// mode 3
				if(!isrc.empty())
				{
					auto it = isrc.find(subq[q_next].mode1.tno);
					if(it != isrc.end())
						candidates.emplace_back(subchannel_q_generate_mode3(subq[it->second], subq[q_next], lba_index - q_next));
				}
			}

			if(!candidates.empty())
			{
				uint32_t c = 0;
				for(uint32_t j = 0; j < (uint32_t)candidates.size(); ++j)
					if(bit_diff((uint8_t *)&subq[lba_index], (uint8_t *)&candidates[j], sizeof(ChannelQ)) < bit_diff((uint8_t *)&subq[lba_index], (uint8_t *)&candidates[c], sizeof(ChannelQ)))
						c = j;

				subq[lba_index] = candidates[c];
			}
		}
	}
}


int32_t track_offset_by_sync(int32_t lba_start, int32_t lba_end, std::fstream &state_fs, std::fstream &scm_fs)
{
	int32_t write_offset = std::numeric_limits<int32_t>::max();

	constexpr uint32_t sectors_to_check = 2;

	std::vector<uint8_t> data(sectors_to_check * CD_DATA_SIZE);
	std::vector<State> state(sectors_to_check * CD_DATA_SIZE_SAMPLES);

	uint32_t groups_count = (lba_end - lba_start) / sectors_to_check;
	for(uint32_t i = 0; i < groups_count; ++i)
	{
		int32_t lba = lba_start + i * sectors_to_check;
		read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba - LBA_START, sectors_to_check, 0, 0);
		read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, sectors_to_check, 0, (uint8_t)State::ERROR_SKIP);

		for(auto const &s : state)
			if(s == State::ERROR_SKIP || s == State::ERROR_C2)
				continue;

		auto it = std::search(data.begin(), data.end(), std::begin(CD_DATA_SYNC), std::end(CD_DATA_SYNC));
		if(it != data.end())
		{
			auto sector_offset = (uint32_t)(it - data.begin());

			// enough data for one sector
			if(data.size() - sector_offset >= CD_DATA_SIZE)
			{
				Sector &sector = *(Sector *)&data[sector_offset];
				Scrambler scrambler;
				scrambler.Descramble((uint8_t *)&sector, nullptr);

				if(BCDMSF_valid(sector.header.address))
				{
					int32_t sector_lba = BCDMSF_to_LBA(sector.header.address);
					write_offset = ((int32_t)sector_offset - (sector_lba - lba) * (int32_t)CD_DATA_SIZE) / (int32_t)CD_SAMPLE_SIZE;

					break;
				}
			}
		}
	}

	return write_offset;
}


int32_t byte_offset_by_magic(int32_t lba_start, int32_t lba_end, std::fstream &state_fs, std::fstream &scm_fs, const std::string &magic)
{
	int32_t write_offset = std::numeric_limits<int32_t>::max();

	const uint32_t sectors_to_check = lba_end - lba_start;

	std::vector<uint8_t> data(sectors_to_check * CD_DATA_SIZE);
	std::vector<State> state(sectors_to_check * CD_DATA_SIZE_SAMPLES);

	read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba_start - LBA_START, sectors_to_check, 0, 0);
	read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_start - LBA_START, sectors_to_check, 0, (uint8_t)State::ERROR_SKIP);

	bool data_correct = true;
	for(auto const &s : state)
		if(s == State::ERROR_SKIP || s == State::ERROR_C2)
		{
			data_correct = false;
			continue;
		}

	if(data_correct)
	{
		auto it = std::search(data.begin(), data.end(), magic.begin(), magic.end());
		if(it != data.end())
			write_offset = (uint32_t)(it - data.begin());
	}

	return write_offset;
}


int32_t track_find_offset_shift(int32_t write_offset, int32_t lba, uint32_t count, std::fstream &state_fs, std::fstream &scm_fs)
{
	int32_t offset_shift = 0;

	// make sure there are no errors in data
	std::vector<State> state(count * CD_DATA_SIZE_SAMPLES);
	read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, count, -write_offset, (uint8_t)State::ERROR_SKIP);
	for(auto const &s : state)
		if(s == State::ERROR_SKIP || s == State::ERROR_C2)
			return offset_shift;

	std::vector<uint8_t> data(count * CD_DATA_SIZE);
	read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba - LBA_START, count, -write_offset * CD_SAMPLE_SIZE, 0);

	uint32_t sector_shift = 0;
	for(uint32_t i = 0; i < data.size() - sizeof(CD_DATA_SYNC); i += CD_SAMPLE_SIZE)
	{
		if(i && !memcmp(&data[i], CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
		{
			sector_shift = i % CD_DATA_SIZE;
			break;
		}
	}

	if(sector_shift)
	{
		Scrambler scrambler;

		constexpr uint32_t head_size = sizeof(Sector::sync) + sizeof(Sector::header);
		for(uint32_t i = sector_shift; i + head_size <= data.size(); i += CD_DATA_SIZE)
		{
			auto s = (Sector *)&data[i];
			// don't use .Descramble() here as sectors in-between will be randomly damaged preventing detection
			scrambler.Process((uint8_t *)s, (uint8_t *)s, std::min(CD_DATA_SIZE, (uint32_t)(data.size() - i)));

			// expected sector
			int32_t sector_lba = BCDMSF_to_LBA(s->header.address);
			if(sector_lba >= lba && sector_lba < lba + (int32_t)count)
			{
				offset_shift = (int32_t)i / (int32_t)CD_SAMPLE_SIZE - (sector_lba - lba) * CD_DATA_SIZE_SAMPLES;
				break;
			}
		}
	}

	return offset_shift;
}


uint32_t track_sync_count(int32_t lba_start, int32_t lba_end, int32_t write_offset, std::fstream &scm_fs)
{
	uint32_t sectors_count = 0;

	std::vector<uint8_t> data(CD_DATA_SIZE);

	for(int32_t lba = lba_start; lba < lba_end; ++lba)
	{
		uint32_t lba_index = lba - LBA_START;

		read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba_index, 1, -write_offset * CD_SAMPLE_SIZE, 0);
		if(!memcmp(data.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
			++sectors_count;
	}

	return sectors_count;
}


void edc_ecc_check(TrackEntry &track_entry, Sector &sector)
{
	switch(sector.header.mode)
	{
	case 1:
	{
		bool error_detected = false;

		Sector::ECC ecc(ECC().Generate((uint8_t *)&sector.header));
		if(memcmp(ecc.p_parity, sector.mode1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode1.ecc.q_parity, sizeof(ecc.q_parity)))
		{
			++track_entry.ecc_errors;
			error_detected = true;
		}

		uint32_t edc = EDC().ComputeBlock(0, (uint8_t *)&sector, offsetof(Sector, mode1.edc));
		if(edc != sector.mode1.edc)
		{
			++track_entry.edc_errors;
			error_detected = true;
		}

		// log dual ECC/EDC mismatch as one error
		if(error_detected)
			++track_entry.redump_errors;

		break;
	}

	// XA Mode2 EDC covers subheader, subheader copy and user data, user data size depends on Form1 / Form2 flag
	case 2:
	{
		// subheader mismatch, just a warning
		if(memcmp(&sector.mode2.xa.sub_header, &sector.mode2.xa.sub_header_copy, sizeof(sector.mode2.xa.sub_header)))
		{
			++track_entry.subheader_errors;
			++track_entry.redump_errors;
		}

		// Form2
		if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
		{
			// Form2 EDC can be zero depending on mastering utility
			if(sector.mode2.xa.form2.edc)
			{
				uint32_t edc = EDC().ComputeBlock(0, (uint8_t *)&sector.mode2.xa.sub_header,
												  offsetof(Sector, mode2.xa.form2.edc) - offsetof(Sector, mode2.xa.sub_header));
				if(edc != sector.mode2.xa.form2.edc)
				{
					++track_entry.edc_errors;
					++track_entry.redump_errors;
				}
			}
		}
		// Form1
		else
		{
			bool error_detected = false;

			// EDC
			uint32_t edc = EDC().ComputeBlock(0, (uint8_t *)&sector.mode2.xa.sub_header,
											  offsetof(Sector, mode2.xa.form1.edc) - offsetof(Sector, mode2.xa.sub_header));
			if(edc != sector.mode2.xa.form1.edc)
			{
				++track_entry.edc_errors;
				error_detected = true;
			}

			// ECC
			// modifies sector, make sure sector data is not used after ECC calculation, otherwise header has to be restored
			Sector::Header header = sector.header;
			std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0);

			Sector::ECC ecc(ECC().Generate((uint8_t *)&sector.header));
			if(memcmp(ecc.p_parity, sector.mode2.xa.form1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode2.xa.form1.ecc.q_parity, sizeof(ecc.q_parity)))
			{
				++track_entry.ecc_errors;
				error_detected = true;
			}

			// restore modified sector header
			sector.header = header;

			// log dual ECC/EDC mismatch as one error
			if(error_detected)
				++track_entry.redump_errors;
		}
		break;
	}

	default:
		;
	}
}


uint32_t iso9660_volume_size(std::fstream &scm_fs, uint64_t scm_offset, bool scrap)
{
	ImageBrowser browser(scm_fs, scm_offset, !scrap);
	auto volume_size = browser.GetPVD().primary.volume_space_size.lsb;
	return volume_size;
}


bool check_tracks(const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, int32_t write_offset_data, int32_t write_offset_audio,
                  const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, int32_t lba_start, bool scrap, const Options &options)
{
	bool no_errors = true;

	std::vector<State> state(CD_DATA_SIZE_SAMPLES);

	LOG("checking tracks");

	auto time_start = std::chrono::high_resolution_clock::now();
	for(auto const &se : toc.sessions)
	{
		for(auto const &t : se.tracks)
		{
			bool data_track = t.control & (uint8_t)ChannelQ::Control::DATA;
			int32_t write_offset = data_track ? write_offset_data : write_offset_audio;

			LOG_F("track {}... ", toc.TrackString(t.track_number));

			uint32_t skip_samples = 0;
			uint32_t c2_samples = 0;
			uint32_t skip_sectors = 0;
			uint32_t c2_sectors = 0;

			//FIXME: omit iso9660 volume size if the filesystem is different

			uint32_t track_length = options.iso9660_trim && data_track && !t.indices.empty() ? iso9660_volume_size(scm_fs, (-lba_start + t.indices.front()) * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE, scrap) : t.lba_end - t.lba_start;
			for(int32_t lba = t.lba_start; lba < t.lba_start + (int32_t)track_length; ++lba)
			{
				if(inside_range(lba, skip_ranges) != nullptr)
					continue;

				uint32_t lba_index = lba - lba_start;

				uint32_t skip_count = 0;
				uint32_t c2_count = 0;
				read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, -write_offset, (uint8_t)State::ERROR_SKIP);
				for(auto const &s : state)
				{
					if(s == State::ERROR_SKIP)
						++skip_count;
					else if(s == State::ERROR_C2)
						++c2_count;
				}

				if(skip_count)
				{
					skip_samples += skip_count;
					++skip_sectors;
				}

				if(c2_count)
				{
					c2_samples += c2_count;
					++c2_sectors;
				}
			}

			if(skip_sectors || c2_sectors)
			{
				LOG("failed, sectors: {{SKIP: {}, C2: {}}}, samples: {{SKIP: {}, C2: {}}}", skip_sectors, c2_sectors, skip_samples, c2_samples);
				no_errors = false;
			}
			else
				LOG("passed");
		}
	}
	auto time_stop = std::chrono::high_resolution_clock::now();

	LOG("check complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start).count());
	LOG("");

	return no_errors;
}


void write_tracks(std::vector<TrackEntry> &track_entries, TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, int32_t write_offset_data, int32_t write_offset_audio,
                  const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, int32_t lba_start, bool scrap, const Options &options)
{
	Scrambler scrambler;
	std::vector<uint8_t> sector(CD_DATA_SIZE);
	std::vector<State> state(CD_DATA_SIZE_SAMPLES);

	bool offset_shift_warning = true;

	LOG("splitting tracks");

	auto time_start = std::chrono::high_resolution_clock::now();
	for(auto &s : toc.sessions)
	{
		for(auto &t : s.tracks)
		{
			// skip empty tracks
			if(t.lba_end == t.lba_start)
				continue;

			bool data_track = t.control & (uint8_t)ChannelQ::Control::DATA;
			int32_t write_offset = data_track ? write_offset_data : write_offset_audio;
			bool data_mode_set = false;
			bool force_descramble = false;

			std::string track_string = toc.TrackString(t.track_number);
			bool lilo = t.track_number == 0x00 || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER);

			// add session number to lead-in/lead-out track string to make filename unique
			if(lilo && toc.sessions.size() > 1)
				track_string = fmt::format("{}.{}", s.session_number, track_string);

			std::string track_name = fmt::format("{}{}.bin", options.image_name,
				toc.sessions.size() == 1 && toc.sessions.front().tracks.size() == 1 && !lilo ? "" : fmt::format(" (Track {})", track_string));
			LOG("writing \"{}\"", track_name);

			if(std::filesystem::exists(std::filesystem::path(options.image_path) / track_name) && !options.overwrite)
				throw_line(fmt::format("file already exists ({})", track_name));

			std::fstream fs_bin(std::filesystem::path(options.image_path) / track_name, std::fstream::out | std::fstream::binary);
			if(!fs_bin.is_open())
				throw_line(fmt::format("unable to create file ({})", track_name));

			TrackEntry track_entry;
			track_entry.filename = track_name;
			track_entry.data = data_track;
			track_entry.ecc_errors = 0;
			track_entry.edc_errors = 0;
			track_entry.subheader_errors = 0;
			track_entry.redump_errors = 0;

			uint32_t crc = crc32_seed();
			MD5 bh_md5;
			SHA1 bh_sha1;

			int32_t lba_end = t.lba_end;
			if(options.iso9660_trim && data_track && !t.indices.empty())
				lba_end = t.lba_start + iso9660_volume_size(scm_fs, (-lba_start + t.indices.front()) * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE, scrap);

			for(int32_t lba = t.lba_start; lba < lba_end; ++lba)
			{
				uint32_t lba_index = lba - lba_start;

				bool generate_sector = false;
				if(!options.leave_unchanged)
				{
					read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, -write_offset, (uint8_t)State::ERROR_SKIP);
					for(auto const &s : state)
					{
						if(s == State::ERROR_SKIP || s == State::ERROR_C2)
						{
							generate_sector = true;
							break;
						}
					}
				}

				// generate sector and fill it with fill byte (default: 0x55)
				if(generate_sector)
				{
					// data
					if(data_track)
					{
						Sector &s = *(Sector *)sector.data();
						memcpy(s.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
						s.header.address = LBA_to_BCDMSF(lba);
						s.header.mode = t.data_mode;
						memset(s.mode2.user_data, options.skip_fill, sizeof(s.mode2.user_data));
					}
					// audio
					else
						memset(sector.data(), options.skip_fill, sector.size());
				}
				else
				{
					read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba_index, 1, -write_offset * CD_SAMPLE_SIZE, 0);

					// data: needs unscramble
					if(data_track)
					{
						if(memcmp(sector.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
						{
							int32_t offset_shift = track_find_offset_shift(write_offset, lba, std::min(OFFSET_SHIFT_MAX_SECTORS, (uint32_t)(lba_end - lba)), state_fs, scm_fs);
							if(offset_shift)
							{
								if(offset_shift_warning)
								{
									LOG("warning: offset shift detected, to apply please use an option (LBA: {:6}, offset: {}, difference: {:+})", lba, write_offset, offset_shift);
									offset_shift_warning = false;
								}

								if(options.correct_offset_shift)
								{
									// offset shifting discs usually have some level of corruption in a couple of transitional sectors
									// preventing normal descramble detection, as everything is scrambled in this case, force descrambling
									force_descramble = true;

									// also, due to possible sync damage, make sure not to shift too early
									auto sync_diff = diff_bytes_count(sector.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
									if(sync_diff > OFFSET_SHIFT_SYNC_TOLERANCE)
									{
										// extract garbage portion to file
										if(offset_shift > 0)
										{
											uint32_t garbage_count = offset_shift / CD_DATA_SIZE_SAMPLES + (offset_shift % CD_DATA_SIZE_SAMPLES ? 1 : 0);
											std::vector<uint8_t> garbage(garbage_count * CD_DATA_SIZE);
											read_entry(scm_fs, garbage.data(), CD_DATA_SIZE, lba_index, garbage_count, -write_offset * CD_SAMPLE_SIZE, 0);

											std::filesystem::path track_garbage_path(std::filesystem::path(options.image_path) / fmt::format("{}.{:06}", track_name, lba));
											std::fstream fs(track_garbage_path, std::fstream::out | std::fstream::binary);
											if(!fs.is_open())
												throw_line(fmt::format("unable to create file ({})", track_garbage_path.filename().string()));
											fs.write((char *)garbage.data(), offset_shift * CD_SAMPLE_SIZE);
										}

										write_offset += offset_shift;
										write_offset_data += offset_shift;
										write_offset_audio += offset_shift;

										read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba_index, 1, -write_offset * CD_SAMPLE_SIZE, 0);

										LOG("offset shift correction applied (new offset: {})", write_offset);
										offset_shift_warning = true;
									}
								}
							}
						}

						bool success = true;
						if(force_descramble)
							scrambler.Process(sector.data(), sector.data(), sector.size());
						else
							success = scrambler.Descramble(sector.data(), &lba);

						if(success)
						{
							if(!data_mode_set)
							{
								t.data_mode = ((Sector *)sector.data())->header.mode;
								data_mode_set = true;
							}
						}
						else
						{
							LOG("warning: descramble failed (LBA: {:6})", lba);
						}
					}
				}

				crc = crc32(sector.data(), sector.size(), crc);
				bh_md5.Update(sector.data(), sector.size());
				bh_sha1.Update(sector.data(), sector.size());
				if(data_track)
				{
					Sector s = *(Sector *)sector.data();
					edc_ecc_check(track_entry, s);
				}

				fs_bin.write((char *)sector.data(), sector.size());
				if(fs_bin.fail())
					throw_line(fmt::format("write failed ({})", track_name));
			}

			track_entry.crc = crc32_final(crc);
			track_entry.md5 = bh_md5.Final();
			track_entry.sha1 = bh_sha1.Final();
			track_entries.push_back(track_entry);
		}
	}
	auto time_stop = std::chrono::high_resolution_clock::now();

	LOG("split complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start).count());
	LOG("");
}


bool toc_mismatch(const TOC &toc, const TOC &qtoc)
{
	bool mismatch = false;

	std::set<uint8_t> tracks;

	std::map<uint8_t, const TOC::Session::Track *> toc_tracks;
	for(auto const &s : toc.sessions)
		for(auto const &t : s.tracks)
		{
			toc_tracks[t.track_number] = &t;
			tracks.insert(t.track_number);
		}

	std::map<uint8_t, const TOC::Session::Track *> qtoc_tracks;
	for(auto const &s : qtoc.sessions)
		for(auto const &t : s.tracks)
		{
			qtoc_tracks[t.track_number] = &t;
			tracks.insert(t.track_number);
		}

	for(auto const &t : tracks)
	{
		auto tt = toc_tracks.find(t);
		auto qt = qtoc_tracks.find(t);

		if(tt != toc_tracks.end() && qt != qtoc_tracks.end())
		{
			if(tt->second->control != qt->second->control)
			{
				mismatch = true;
				LOG("warning: TOC / QTOC control mismatch (track: {}, control: {:04b} / {:04b})", t, tt->second->control, qt->second->control);
			}

			if(tt->second->lba_start != qt->second->lba_start)
			{
				mismatch = true;
				LOG("warning: TOC / QTOC track index 00 mismatch (track: {}, LBA: {} / {})", t, tt->second->lba_start, qt->second->lba_start);
			}

			auto tt_size = tt->second->indices.size();
			auto qt_size = qt->second->indices.size();
			if(tt_size == qt_size)
			{
				if(tt_size && qt_size && tt->second->indices.front() != qt->second->indices.front())
				{
					mismatch = true;
					LOG("warning: TOC / QTOC track index 01 mismatch (track: {}, LBA: {} / {})", t, tt->second->indices.front(), qt->second->indices.front());
				}
			}
			else
			{
				mismatch = true;
				LOG("warning: TOC / QTOC track index size mismatch (track: {})", t);
			}

			if(tt->second->lba_end != qt->second->lba_end)
			{
				mismatch = true;
				LOG("warning: TOC / QTOC track length mismatch (track: {}, LBA: {} / {})", t, tt->second->lba_end, qt->second->lba_end);
			}
		}
		else
		{
			if(tt == toc_tracks.end())
			{
				mismatch = true;
				LOG("warning: nonexistent track in TOC (track: {})", t);
			}

			if(qt == qtoc_tracks.end())
			{
				mismatch = true;
				LOG("warning: nonexistent track in QTOC (track: {})", t);
			}
		}
	}

	return mismatch;
}


std::vector<std::pair<int32_t, int32_t>> audio_get_toc_index0_ranges(const TOC &toc)
{
	std::vector<std::pair<int32_t, int32_t>> index0_ranges;

	for(auto &s : toc.sessions)
	{
		for(auto &t : s.tracks)
		{
			int32_t index0_end = t.indices.empty() ? t.lba_end : t.indices.front();
			if(index0_end > t.lba_start)
				index0_ranges.emplace_back(t.lba_start * (int32_t)CD_DATA_SIZE_SAMPLES, index0_end * (int32_t)CD_DATA_SIZE_SAMPLES);
		}
	}

	return index0_ranges;
}


std::vector<std::vector<std::pair<int32_t, int32_t>>> audio_get_silence_ranges(std::fstream &scm_fs, uint32_t sectors_count, uint16_t silence_threshold, uint32_t min_count)
{
	std::vector<std::vector<std::pair<int32_t, int32_t>>> silence_ranges(silence_threshold + 1);

	uint32_t samples[CD_DATA_SIZE_SAMPLES];

	// don't use std::vector here because it's too slow
	auto silence_start = std::make_unique<int32_t[]>(silence_threshold + 1);
	std::fill_n(silence_start.get(), silence_threshold + 1, std::numeric_limits<int32_t>::min());

	for(uint32_t i = 0; i < sectors_count; ++i)
	{
		read_entry(scm_fs, (uint8_t *)samples, CD_DATA_SIZE, i, 1, 0, 0);

		for(uint32_t j = 0; j < CD_DATA_SIZE_SAMPLES; ++j)
		{
			int32_t position = (LBA_START + i) * CD_DATA_SIZE_SAMPLES + j;

			auto sample = (int16_t *)&samples[j];

			for(uint16_t k = 0; k <= silence_threshold; ++k)
			{
				// silence
				if(std::abs(sample[0]) <= (int)k && std::abs(sample[1]) <= (int)k)
				{
					if(silence_start[k] == std::numeric_limits<int32_t>::max())
						silence_start[k] = position;
				}
				// not silence
				else
				{
					if(silence_start[k] != std::numeric_limits<int32_t>::max())
					{
						if(silence_start[k] == std::numeric_limits<int32_t>::min() || position - silence_start[k] >= (int32_t)min_count)
							silence_ranges[k].emplace_back(silence_start[k], position);

						silence_start[k] = std::numeric_limits<int32_t>::max();
					}
				}
			}
		}
	}

	// tail
	for(uint16_t k = 0; k <= silence_threshold; ++k)
		silence_ranges[k].emplace_back(silence_start[k] == std::numeric_limits<int32_t>::max() ? sectors_count * CD_DATA_SIZE_SAMPLES + (LBA_START * (int32_t)CD_DATA_SIZE_SAMPLES) : silence_start[k], std::numeric_limits<int32_t>::max());

	return silence_ranges;
}


std::pair<int32_t, int32_t> audio_get_sample_range(std::fstream &scm_fs, uint32_t sectors_count)
{
	std::pair<int32_t, int32_t> range(0, 0);

	uint32_t samples[CD_DATA_SIZE_SAMPLES];

	// head
	for(uint32_t i = 0; i < sectors_count; ++i)
	{
		bool to_break = false;

		read_entry(scm_fs, (uint8_t *)samples, CD_DATA_SIZE, i, 1, 0, 0);

		for(uint32_t j = 0; j < CD_DATA_SIZE_SAMPLES; ++j)
		{
			auto sample = (int16_t *)&samples[j];

			if(sample[0] || sample[1])
			{
				range.first = (LBA_START + i) * CD_DATA_SIZE_SAMPLES + j;
				to_break = true;
				break;
			}
		}

		if(to_break)
			break;
	}

	// tail
	for(uint32_t ii = sectors_count; ii > 0; --ii)
	{
		bool to_break = false;

		uint32_t i = ii - 1;

		read_entry(scm_fs, (uint8_t *)samples, CD_DATA_SIZE, i, 1, 0, 0);

		for(uint32_t jj = CD_DATA_SIZE_SAMPLES; jj > 0; --jj)
		{
			uint32_t j = jj - 1;

			auto sample = (int16_t *)&samples[j];

			if(sample[0] || sample[1])
			{
				range.second = (LBA_START + i) * CD_DATA_SIZE_SAMPLES + jj;
				to_break = true;
				break;
			}
		}

		if(to_break)
			break;
	}

	return range;
}


int32_t disc_offset_by_silence(const TOC &toc, std::fstream &scm_fs, uint32_t sectors_count, const Options &options)
{
	int32_t write_offset = std::numeric_limits<int32_t>::max();

	auto index0_ranges = audio_get_toc_index0_ranges(toc);
	uint32_t silence_samples_min = std::numeric_limits<uint32_t>::max();
	for(auto const &r : index0_ranges)
	{
		uint32_t length = r.second - r.first;
		if(silence_samples_min > length)
			silence_samples_min = length;
	}

	LOG_F("audio silence detection... ");
	auto silence_ranges = audio_get_silence_ranges(scm_fs, sectors_count, options.audio_silence_threshold, silence_samples_min);
	LOG("done");

	std::pair<int32_t, int32_t> toc_sample_range(toc.sessions.front().tracks.front().lba_start * (int32_t)CD_DATA_SIZE_SAMPLES, toc.sessions.back().tracks.back().lba_end * (int32_t)CD_DATA_SIZE_SAMPLES);
	std::pair<int32_t, int32_t> data_sample_range(silence_ranges[0].front().second, silence_ranges[0].back().first);
	int32_t toc_sample_size = toc_sample_range.second - toc_sample_range.first;
	int32_t data_sample_size = data_sample_range.second - data_sample_range.first;
	int32_t pregap_sample_size = 150 * CD_DATA_SIZE_SAMPLES;

	for(uint16_t t = 0; t <= options.audio_silence_threshold; ++t)
	{
		auto &silence_range = silence_ranges[t];

		std::vector<std::pair<int32_t, int32_t>> offset_ranges;
		for(int32_t sample_offset = -OFFSET_DEVIATION_MAX; sample_offset <= (int32_t)OFFSET_DEVIATION_MAX; ++sample_offset)
		{
			bool match = true;

			uint32_t cache_i = 0;
			for(auto const &r : index0_ranges)
			{
				bool found = false;

				std::pair<int32_t, int32_t> ir(r.first + sample_offset, r.second + sample_offset);

				for(uint32_t i = cache_i; i < silence_range.size(); ++i)
				{
					bool ahead = ir.first >= silence_range[i].first;
					if(ahead)
						cache_i = i;

					if(ahead && ir.second <= silence_range[i].second)
					{
						found = true;
						break;
					}

					if(ir.second < silence_range[i].first)
						break;
				}

				if(!found)
				{
					match = false;
					break;
				}
			}

			if(match)
			{
				if(offset_ranges.empty())
				{
					offset_ranges.emplace_back(sample_offset, sample_offset);
				}
				else
				{
					if(offset_ranges.back().second + 1 == sample_offset)
						offset_ranges.back().second = sample_offset;
					else
						offset_ranges.emplace_back(sample_offset, sample_offset);
				}
			}
		}

		if(!offset_ranges.empty())
		{
			LOG_F("Perfect Audio Offset (silence level: {}): ", t);
			for(uint32_t i = 0; i < offset_ranges.size(); ++i)
			{
				auto const &r = offset_ranges[i];

				if(r.first == r.second)
					LOG_F("{:+}{}", r.first, i + 1 == offset_ranges.size() ? "" : ", ");
				else
					LOG_F("[{:+} .. {:+}]{}", r.first, r.second, i + 1 == offset_ranges.size() ? "" : ", ");
			}
			LOG("");

			// only one perfect offset exists
			if(offset_ranges.size() == 1 && offset_ranges.front().first == offset_ranges.front().second)
				write_offset = offset_ranges.front().first;

			// try to move out data from pre-gap if it's still in perfect range
			if(write_offset == std::numeric_limits<int32_t>::max())
			{
				if(data_sample_range.first < toc_sample_range.first + pregap_sample_size && data_sample_size + pregap_sample_size <= toc_sample_size)
				{
					int32_t wo = data_sample_range.first - (toc_sample_range.first + pregap_sample_size);

					for(auto const r : offset_ranges)
					{
						if(wo >= r.first && wo <= r.second)
						{
							write_offset = wo;
							break;
						}
					}
				}
			}

			// favor offset 0 if it belongs to perfect range
			if(write_offset == std::numeric_limits<int32_t>::max())
			{
				for(auto const r : offset_ranges)
				{
					if(0 >= r.first && 0 <= r.second)
					{
						write_offset = 0;
						break;
					}
				}
			}

			// choose the closest offset to 0
			if(write_offset == std::numeric_limits<int32_t>::max())
			{
				for(auto const r : offset_ranges)
				{
					if(std::abs(r.first) < std::abs(write_offset))
						write_offset = r.first;

					if(std::abs(r.second) < std::abs(write_offset))
						write_offset = r.second;
				}
			}

			break;
		}
	}

	return write_offset;
}


int32_t disc_offset_by_overlap(const TOC &toc, std::fstream &scm_fs, int32_t write_offset_data)
{
	int32_t write_offset = std::numeric_limits<int32_t>::max();

	for(auto &s : toc.sessions)
	{
		for(uint32_t t = 1; t < s.tracks.size(); ++t)
		{
			auto &t1 = s.tracks[t - 1];
			auto &t2 = s.tracks[t];

			if(t1.control & (uint8_t)ChannelQ::Control::DATA && !(t2.control & (uint8_t)ChannelQ::Control::DATA))
			{
				static constexpr uint32_t OVERLAP_COUNT = 10;

				uint32_t sectors_to_check = std::min(std::min((uint32_t)(t1.lba_end - t1.lba_start), (uint32_t)(t2.lba_end - t2.lba_start)), OVERLAP_COUNT);

				std::vector<uint32_t> t1_samples(sectors_to_check * CD_DATA_SIZE_SAMPLES);
				read_entry(scm_fs, (uint8_t *)t1_samples.data(), CD_DATA_SIZE, (t1.lba_end - sectors_to_check) - LBA_START, sectors_to_check, -write_offset_data * CD_SAMPLE_SIZE, 0);

				std::vector<uint32_t> t2_samples(sectors_to_check * CD_DATA_SIZE_SAMPLES);
				read_entry(scm_fs, (uint8_t *)t2_samples.data(), CD_DATA_SIZE, t2.lba_start - LBA_START, sectors_to_check, 0 * CD_SAMPLE_SIZE, 0);

				Scrambler scrambler;
				for(uint32_t i = 0; i < sectors_to_check; ++i)
				{
					uint8_t *s = (uint8_t *)t1_samples.data() + i * CD_DATA_SIZE;
					scrambler.Process(s, s);
				}

				for(auto it = t1_samples.begin(); it != t1_samples.end(); ++it)
				{
					if(std::equal(it, t1_samples.end(), t2_samples.begin()))
					{
						write_offset = t1_samples.end() - it;
						break;
					}
				}

				break;
			}
		}

		if(write_offset != std::numeric_limits<int32_t>::max())
			break;
	}

	return write_offset;
}


std::pair<int32_t, int32_t> find_non_zero_range(std::fstream &scm_fs, std::fstream &state_fs, int32_t lba_start, int32_t lba_end, int32_t write_offset, bool data_track, bool reverse)
{
	int32_t step = 1;
	if(reverse)
	{
		std::swap(lba_start, lba_end);
		--lba_start;
		--lba_end;
		step = -1;
	}

	Scrambler scrambler;

	int32_t lba = lba_start;
	for(; lba != lba_end; lba += step)
	{
		std::vector<uint8_t> sector(CD_DATA_SIZE);
		read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba - LBA_START, 1, -write_offset * CD_SAMPLE_SIZE, 0);

		std::vector<State> state(CD_DATA_SIZE_SAMPLES);
		read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -write_offset, (uint8_t)State::ERROR_SKIP);

		// skip all incomplete / erroneous sectors
		bool skip = false;
		for(auto const &s : state)
			if(s == State::ERROR_SKIP || s == State::ERROR_C2)
			{
				skip = true;
				break;
			}
		if(skip)
			continue;

		auto data = (uint32_t *)sector.data();
		uint64_t data_size = sector.size();
		if(data_track)
		{
			scrambler.Descramble(sector.data(), &lba);

			auto s = (Sector *)sector.data();
            if(s->header.mode == 0)
            {
                data = (uint32_t *)s->mode2.user_data;
                data_size = MODE0_DATA_SIZE;
            }
            else if(s->header.mode == 1)
            {
                data = (uint32_t *)s->mode1.user_data;
                data_size = FORM1_DATA_SIZE;
            }
            else if(s->header.mode == 2)
            {
                if(s->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
                {
                    data = (uint32_t *)s->mode2.xa.form2.user_data;
                    data_size = FORM2_DATA_SIZE;
                }
                else
                {
                    data = (uint32_t *)s->mode2.xa.form1.user_data;
                    data_size = FORM1_DATA_SIZE;
                }
            }
		}
		data_size /= sizeof(uint32_t);

		if(!is_zeroed(data, data_size))
			break;
	}

	return std::pair<int32_t, int32_t>(reverse ? lba_end + 1 : lba, reverse ? lba + 1 : lba_end);
}


void redumper_protection(Options &options)
{
	if(options.image_name.empty())
		throw_line("no image name provided");

	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::filesystem::path scm_path(image_prefix + ".scram");
	std::filesystem::path scp_path(image_prefix + ".scrap");
	std::filesystem::path sub_path(image_prefix + ".subcode");
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");

	bool scrap = !std::filesystem::exists(scm_path) && std::filesystem::exists(scp_path);
	auto scra_path(scrap ? scp_path : scm_path);

	//TODO: rework
	uint32_t sectors_count = check_file(state_path, CD_DATA_SIZE_SAMPLES);

	// TOC
	std::vector<uint8_t> toc_buffer = read_vector(toc_path);
	TOC toc(toc_buffer, false);

	// FULL TOC
	if(std::filesystem::exists(fulltoc_path))
	{
		std::vector<uint8_t> fulltoc_buffer = read_vector(fulltoc_path);
		TOC toc_full(fulltoc_buffer, true);
		if(toc_full.sessions.size() > 1)
			toc = toc_full;
		else
			toc.disc_type = toc_full.disc_type;
	}

	{
		auto &t = toc.sessions.back().tracks.back();

		// fake TOC
		if(t.lba_end < 0)
		{
			LOG("warning: fake TOC detected, using default 74min disc size");
			t.lba_end = MSF_to_LBA(MSF{74, 0, 0});
		}

		// incomplete dump (dumped with --stop-lba)
		if(t.lba_end > (int32_t)sectors_count + LBA_START)
		{
			LOG("warning: incomplete dump detected, using available dump size");
			t.lba_end = (int32_t)sectors_count + LBA_START;
		}
	}

	std::fstream scm_fs(scra_path, std::fstream::in | std::fstream::binary);
	if(!scm_fs.is_open())
		throw_line(fmt::format("unable to open file ({})", scra_path.filename().string()));

	std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
	if(!state_fs.is_open())
		throw_line(fmt::format("unable to open file ({})", state_path.filename().string()));

	std::string protection("N/A");

	LOG("scan started");

	auto scan_time_start = std::chrono::high_resolution_clock::now();

	// PS2 Datel DATA.DAT / BIG.DAT
	// only one track
	if(toc.sessions.size() == 1 && toc.sessions.front().tracks.size() == 1)
	{
		// data track
		auto &t = toc.sessions.front().tracks.front();
		if(t.control & (uint8_t)ChannelQ::Control::DATA)
		{
			std::vector<State> state(CD_DATA_SIZE_SAMPLES);

			int32_t write_offset = track_offset_by_sync(t.indices.front(), t.lba_end, state_fs, scm_fs);
			if(write_offset != std::numeric_limits<int32_t>::max())
			{
				// preliminary check
				bool candidate = false;
				{
					constexpr int32_t lba_check = 50;
					if(lba_check >= t.indices.front() && lba_check < t.lba_end)
					{
						read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_check - LBA_START, 1, -write_offset, (uint8_t)State::ERROR_SKIP);
						bool error = false;
						for(auto const &s : state)
							if(s == State::ERROR_C2)
							{
								candidate = true;
								break;
							}
					}
				}

				if(candidate)
				{
					std::string protected_filename;
					{
						ImageBrowser browser(scm_fs, -LBA_START * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE, !scrap);
						auto root_dir = browser.RootDirectory();

						// protection file exists
						auto data_dat = root_dir->SubEntry("DATA.DAT");
						auto big_dat = root_dir->SubEntry("BIG.DAT");

						std::shared_ptr<ImageBrowser::Entry> protection_dat;
						if(data_dat && big_dat)
							protection_dat = data_dat->SectorOffset() < big_dat->SectorOffset() ? data_dat : big_dat;
						else if(data_dat)
							protection_dat = data_dat;
						else if(big_dat)
							protection_dat = big_dat;

						// first file on disc and starts from LBA 23
						if(protection_dat->SectorOffset() == 23)
							protected_filename = protection_dat->Name();
					}

					if(!protected_filename.empty())
					{
						std::pair<int32_t, int32_t> range(0, 0);
						for(int32_t lba = 25, lba_end = std::min(t.lba_end, 5000); lba < lba_end; ++lba)
						{
							read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -write_offset, (uint8_t)State::ERROR_SKIP);

							bool error = false;
							for(auto const &s : state)
								if(s == State::ERROR_C2)
								{
									error = true;
									break;
								}

							if(error)
							{
								if(!range.first)
									range.first = lba;
								range.second = lba + 1;
							}
							else
							{
								if(range.first)
									break;
							}
						}

						if(range.second > range.first)
						{
							protection = fmt::format("PS2/Datel {}, C2: {}, range: {}-{}", protected_filename, range.second - range.first, range.first, range.second - 1);

							auto skip_ranges = string_to_ranges(options.skip);
							skip_ranges.push_back(range);
							options.skip = ranges_to_string(skip_ranges);
						}
					}
				}
			}
		}
	}

	LOG("protection: {}", protection);

	auto scan_time_stop = std::chrono::high_resolution_clock::now();
	LOG("scan complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(scan_time_stop - scan_time_start).count());
	LOG("");
}


void redumper_split(const Options &options)
{
	if(options.image_name.empty())
		throw_line("no image name provided");

	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::filesystem::path scm_path(image_prefix + ".scram");
	std::filesystem::path scp_path(image_prefix + ".scrap");
	std::filesystem::path sub_path(image_prefix + ".subcode");
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
	std::filesystem::path cdtext_path(image_prefix + ".cdtext");

	bool subcode = std::filesystem::exists(sub_path);
	bool scrap = !std::filesystem::exists(scm_path) && std::filesystem::exists(scp_path);
	auto scra_path(scrap ? scp_path : scm_path);

	//TODO: rework?
	uint32_t sectors_count = check_file(state_path, CD_DATA_SIZE_SAMPLES);

	std::fstream scm_fs(scra_path, std::fstream::in | std::fstream::binary);
	if(!scm_fs.is_open())
		throw_line(fmt::format("unable to open file ({})", scra_path.filename().string()));

	std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
	if(!state_fs.is_open())
		throw_line(fmt::format("unable to open file ({})", state_path.filename().string()));

	// TOC
	std::vector<uint8_t> toc_buffer = read_vector(toc_path);
	TOC toc(toc_buffer, false);

	// FULL TOC
	if(std::filesystem::exists(fulltoc_path))
	{
		std::vector<uint8_t> full_toc_buffer = read_vector(fulltoc_path);
		TOC toc_full(full_toc_buffer, true);

		// PX-W5224TA: incorrect FULL TOC data in some cases
		toc_full.DeriveINDEX(toc);

		if(toc_full.sessions.size() > 1)
			toc = toc_full;
		else
			toc.disc_type = toc_full.disc_type;
	}

	// preload subchannel Q
	std::vector<ChannelQ> subq(sectors_count);
	if(subcode)
	{
		std::fstream fs(sub_path, std::fstream::in | std::fstream::binary);
		if(!fs.is_open())
			throw_line(fmt::format("unable to open file ({})", sub_path.filename().string()));

		std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
		for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
		{
			read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), lba_index, 1, 0, 0);
			subcode_extract_channel((uint8_t *)&subq[lba_index], sub_buffer.data(), Subchannel::Q);
		}

		// correct Q
		LOG_F("correcting Q... ");
		correct_program_subq(subq.data(), sectors_count);
		LOG("done");
		LOG("");

		toc.UpdateQ(subq.data(), sectors_count, LBA_START);
	}
	else
	{
		LOG("warning: subchannel data is not available, generating TOC index 0 entries");
		toc.GenerateIndex0();
	}

	LOG("final TOC:");
	toc.Print();
	LOG("");

	if(subcode)
	{
		TOC qtoc(subq.data(), sectors_count, LBA_START);

		// compare TOC and QTOC
		if(toc_mismatch(toc, qtoc))
		{
			LOG("");
			LOG("final QTOC:");
			qtoc.Print();
			LOG("");
		}

		if(options.force_qtoc)
		{
			toc = qtoc;
			LOG("warning: split is performed by QTOC");
			LOG("");
		}

		toc.UpdateMCN(subq.data(), sectors_count);
	}

	// CD-TEXT
	if(std::filesystem::exists(cdtext_path))
	{
		std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);

		toc.UpdateCDTEXT(cdtext_buffer);
	}

	// CD-i Ready / AudioVision
	if(options.cdi_ready_normalize)
	{
		auto t0 = toc.sessions.front().tracks.front();
		auto &t1 = toc.sessions.front().tracks.front();

		t0.track_number = 0;
		t0.lba_end = t1.indices.front();
		t0.control = (uint8_t)ChannelQ::Control::DATA;
		t0.indices.clear();
		t0.indices.push_back(t1.lba_start - MSF_LBA_SHIFT);

		t1.lba_start = t1.indices.front();

		toc.sessions.front().tracks.insert(toc.sessions.front().tracks.begin(), t0);
	}

	LOG("detecting offset");
	auto time_start = std::chrono::high_resolution_clock::now();

	int32_t write_offset = options.force_offset ? *options.force_offset : std::numeric_limits<int32_t>::max();
	int32_t write_offset_data = scrap ? std::numeric_limits<int32_t>::max() : write_offset;

	// data track
	if(write_offset_data == std::numeric_limits<int32_t>::max())
	{
		for(auto &s : toc.sessions)
			for(auto &t : s.tracks)
				if(t.control & (uint8_t)ChannelQ::Control::DATA)
				{
					write_offset_data = track_offset_by_sync(t.indices.empty() ? t.lba_start : t.indices.front(), t.lba_end, state_fs, scm_fs);
					if(write_offset_data != std::numeric_limits<int32_t>::max())
					{
						if(!scrap)
							write_offset = write_offset_data;

						LOG("data track detected");
						break;
					}
				}
	}

	// CD-i Ready
	if(write_offset == std::numeric_limits<int32_t>::max() && toc.sessions.size() == 1)
	{
		auto &t = toc.sessions.front().tracks.front();
		if(!(t.control & (uint8_t)ChannelQ::Control::DATA))
		{
			uint32_t index0_count = (t.indices.empty() ? t.lba_end : t.indices.front()) - t.lba_start;
			int32_t track_write_offset = track_offset_by_sync(t.lba_start, t.lba_start + index0_count, state_fs, scm_fs);

			if(track_write_offset != std::numeric_limits<int32_t>::max() && track_sync_count(t.lba_start, t.lba_start + index0_count, track_write_offset, scm_fs) > index0_count / 2)
			{
				write_offset = track_write_offset;
				LOG("CD-i Ready / AudioVision disc detected");
			}
		}
	}

	// Atari Jaguar CD
	if(write_offset == std::numeric_limits<int32_t>::max() && toc.sessions.size() == 2 && !(toc.sessions.back().tracks.front().control & (uint8_t)ChannelQ::Control::DATA))
	{
		auto &t = toc.sessions.back().tracks.front();

		if(!t.indices.empty())
		{
			int32_t byte_offset = byte_offset_by_magic(t.indices.front() - 1, t.indices.front() + 1, state_fs, scm_fs, std::string("TAIRTAIR"));
			if(byte_offset != std::numeric_limits<int32_t>::max())
			{
				byte_offset -= sizeof(uint16_t);
				write_offset = byte_offset / CD_SAMPLE_SIZE - CD_DATA_SIZE_SAMPLES;
				LOG("Atari Jaguar disc detected");
			}
		}
	}

/*
	// PSX GameShark Upgrade CD
	if(write_offset == std::numeric_limits<int32_t>::max())
	{
		if(toc.sessions.size() == 1)
		{
			auto &t = toc.sessions.back().tracks.front();

			if(!t.indices.empty())
			{
				int32_t byte_offset = byte_offset_by_magic(t.indices.front(), t.indices.front() + 8, state_fs, scm_fs, std::string("G.THORNTON"));
				if(byte_offset != std::numeric_limits<int32_t>::max())
				{
					write_offset = byte_offset / CD_SAMPLE_SIZE;
					LOG("PSX GameShark Upgrade CD detected");
				}
			}
		}
	}
*/

	// try to detect positive offset based on scrambled data track overlap into audio
	if(write_offset == std::numeric_limits<int32_t>::max() && write_offset_data != std::numeric_limits<int32_t>::max() && scrap)
	{
		write_offset = disc_offset_by_overlap(toc, scm_fs, write_offset_data);
		if(write_offset != std::numeric_limits<int32_t>::max())
			LOG("overlap offset detected");
	}

	// perfect audio offset
	if(write_offset == std::numeric_limits<int32_t>::max() && !scrap)
	{
		int32_t perfect_audio_offset = disc_offset_by_silence(toc, scm_fs, sectors_count, options);
		if(perfect_audio_offset != std::numeric_limits<int32_t>::max() && options.perfect_audio_offset)
		{
			write_offset = perfect_audio_offset;
			LOG("Perfect Audio Offset applied");
		}
	}

	if(write_offset == std::numeric_limits<int32_t>::max())
	{
		std::pair<int32_t, int32_t> toc_sample_range(toc.sessions.front().tracks.front().lba_start * (int32_t)CD_DATA_SIZE_SAMPLES, toc.sessions.back().tracks.back().lba_end * (int32_t)CD_DATA_SIZE_SAMPLES);
		std::pair<int32_t, int32_t> data_sample_range(audio_get_sample_range(scm_fs, sectors_count));
		int32_t toc_sample_size = toc_sample_range.second - toc_sample_range.first;
		int32_t data_sample_size = data_sample_range.second - data_sample_range.first;
		int32_t pregap_sample_size = 150 * CD_DATA_SIZE_SAMPLES;
		LOG(" TOC sample range (size: {}): [{:+} .. {:+})", toc_sample_size, toc_sample_range.first, toc_sample_range.second);
		LOG("data sample range (size: {}): [{:+} .. {:+})", data_sample_size, data_sample_range.first, data_sample_range.second);

		//DEBUG
//		std::cout << fmt::format("offset start: {:08X}, offset end: {:08X}",
//				data_sample_range.first * CD_SAMPLE_SIZE - LBA_START * CD_DATA_SIZE,
//				data_sample_range.second * CD_SAMPLE_SIZE - LBA_START * CD_DATA_SIZE) << std::endl;

		if(data_sample_size <= toc_sample_size)
		{
			// move data out of lead-out
			if(data_sample_range.second > toc_sample_range.second)
			{
				write_offset = data_sample_range.second - toc_sample_range.second;
				LOG("moving data out of lead-out (difference: {:+})", write_offset);
			}
			// move data out of pre-gap only if we can get rid of it whole
			else if(data_sample_range.first < toc_sample_range.first + pregap_sample_size && data_sample_size + pregap_sample_size <= toc_sample_size)
			{
				write_offset = data_sample_range.first - (toc_sample_range.first + pregap_sample_size);
				LOG("moving data out of pre-gap (difference: {:+})", write_offset);
			}
		}
	}

	// fallback
	if(write_offset == std::numeric_limits<int32_t>::max())
	{
		write_offset = 0;
		LOG("warning: fallback offset 0 applied");
	}
	if(write_offset_data == std::numeric_limits<int32_t>::max())
		write_offset_data = write_offset;

	LOG("disc write offset: {:+}", write_offset);

	//TODO: find another way without passing cdi_ready?
	// CD-i Ready
	bool cdi_ready = false;
	if(toc.sessions.size() == 1)
	{
		auto &t = toc.sessions.front().tracks.front();
		if(!(t.control & (uint8_t)ChannelQ::Control::DATA))
		{
			uint32_t index0_count = (t.indices.empty() ? t.lba_end : t.indices.front()) - t.lba_start;

			if(track_sync_count(t.lba_start, t.lba_start + index0_count, write_offset_data, scm_fs) > index0_count / 2)
				cdi_ready = true;
		}
	}

	// identify CD-I tracks, needed for CUE-sheet generation
	for(auto &s : toc.sessions)
		for(auto &t : s.tracks)
			if(t.control & (uint8_t)ChannelQ::Control::DATA)
			{
				// CDI
				try
				{
					ImageBrowser browser(scm_fs, -LBA_START * CD_DATA_SIZE + write_offset_data * CD_SAMPLE_SIZE, !scrap);

					auto pvd = browser.GetPVD();

					if(!memcmp(pvd.standard_identifier, iso9660::CDI_STANDARD_INDENTIFIER, sizeof(pvd.standard_identifier)))
						t.cdi = true;
				}
				catch(...)
				{
					//FIXME: be verbose
					;
				}
			}

	// check session pre-gap for non-zero data
	for(auto &s : toc.sessions)
	{
		TOC::Session::Track &t = s.tracks.front();

		int32_t pregap_start = t.lba_start;
		int32_t pregap_end = pregap_start < 0 ? 0 : t.indices.front();

		auto pregap_range = find_non_zero_range(scm_fs, state_fs, pregap_start, pregap_end, write_offset, t.control & (uint8_t)ChannelQ::Control::DATA, false);
		if(pregap_range.second > pregap_range.first)
		{
			if(t.control & (uint8_t)ChannelQ::Control::DATA || cdi_ready)
			{
				t.lba_start = pregap_end;

				auto t_00 = t;

				t_00.track_number = 0;
				t_00.lba_start = pregap_start;
				t_00.lba_end = pregap_end;
				t_00.indices.clear();

				s.tracks.insert(s.tracks.begin(), t_00);
			}

			LOG("warning: pre-gap contains non-zero data (session: {}, sectors: {}/{})", s.session_number, pregap_range.second - pregap_range.first, pregap_end - pregap_start);
		}
		// pre-gap zeroed
		else
			t.lba_start = pregap_end;
	}

	// check session lead-out for non-zero data
	for(auto &s : toc.sessions)
	{
		auto &t = s.tracks.back();

		auto leadout_range = find_non_zero_range(scm_fs, state_fs, t.lba_start, t.lba_end, write_offset, t.control & (uint8_t)ChannelQ::Control::DATA, true);
		if(leadout_range.second > leadout_range.first)
			LOG("warning: lead-out contains non-zero data (session: {}, sectors: {}/{})", s.session_number, leadout_range.second - leadout_range.first, t.lba_end - t.lba_start);

		t.lba_end = leadout_range.second;
	}

	auto time_stop = std::chrono::high_resolution_clock::now();
	LOG("detection complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start).count());
	LOG("");

	std::vector<std::pair<int32_t, int32_t>> skip_ranges = string_to_ranges(options.skip);

	// check tracks
	if(!check_tracks(toc, scm_fs, state_fs, write_offset_data, write_offset, skip_ranges, LBA_START, scrap, options) && !options.force_split)
		throw_line(fmt::format("data errors detected, unable to continue"));

	// write tracks
	std::vector<TrackEntry> track_entries;
	write_tracks(track_entries, toc, scm_fs, state_fs, write_offset_data, write_offset, skip_ranges, LBA_START, scrap, options);

	// write CUE-sheet
	std::vector<std::string> cue_sheets;
	LOG("writing CUE-sheet");
	if(toc.cd_text_lang.size() > 1)
	{
		cue_sheets.resize(toc.cd_text_lang.size());
		for(uint32_t i = 0; i < toc.cd_text_lang.size(); ++i)
		{
			cue_sheets[i] = i ? fmt::format("{}_{:02X}.cue", options.image_name, toc.cd_text_lang[i]) : fmt::format("{}.cue", options.image_name);
			LOG_F("{}... ", cue_sheets[i]);

			if(std::filesystem::exists(std::filesystem::path(options.image_path) / cue_sheets[i]) && !options.overwrite)
				throw_line(fmt::format("file already exists ({})", cue_sheets[i]));

			std::fstream fs(std::filesystem::path(options.image_path) / cue_sheets[i], std::fstream::out);
			if(!fs.is_open())
				throw_line(fmt::format("unable to create file ({})", cue_sheets[i]));
			toc.PrintCUE(fs, options.image_name, i);
			LOG("done");
		}
	}
	else
	{
		cue_sheets.push_back(fmt::format("{}.cue", options.image_name));
		LOG_F("{}... ", cue_sheets.front());

		if(std::filesystem::exists(std::filesystem::path(options.image_path) / cue_sheets.front()) && !options.overwrite)
			throw_line(fmt::format("file already exists ({})", cue_sheets.front()));

		std::fstream fs(std::filesystem::path(options.image_path) / cue_sheets.front(), std::fstream::out);
		if(!fs.is_open())
			throw_line(fmt::format("unable to create file ({})", cue_sheets.front()));
		toc.PrintCUE(fs, options.image_name);
		LOG("done");
		LOG("");
	}

	for(auto const &t : track_entries)
	{
		if(!t.data)
			continue;

		LOG("data errors [{}]:", t.filename);
		LOG("  ECC: {}", t.ecc_errors);
		LOG("  EDC: {}", t.edc_errors);
		if(t.subheader_errors)
			LOG("  CD-XA SubHeader mismatch: {}", t.subheader_errors);
		LOG("  redump: {}", t.redump_errors);
		LOG("");
	}

	if(toc.sessions.size() > 1)
	{
		LOG("multisession: ");
		for(auto const &s : toc.sessions)
			LOG("  session {}: {}-{}", s.session_number, s.tracks.front().indices.empty() ? s.tracks.front().lba_start : s.tracks.front().indices.front(), s.tracks.back().lba_end - 1);
		LOG("");
	}

	LOG("dat:");
	for(auto const &t : track_entries)
	{
		std::string filename = t.filename;
		replace_all_occurences(filename, "&", "&amp;");

		LOG("<rom name=\"{}\" size=\"{}\" crc=\"{:08x}\" md5=\"{}\" sha1=\"{}\" />", filename, std::filesystem::file_size(std::filesystem::path(options.image_path) / t.filename), t.crc, t.md5, t.sha1);
	}
	LOG("");

	for(auto const &c : cue_sheets)
	{
		LOG("CUE [{}]:", c);
		std::filesystem::path cue_path(std::filesystem::path(options.image_path) / c);
		std::fstream ifs(cue_path, std::fstream::in);
		if(!ifs.is_open())
			throw_line(fmt::format("unable to open file ({})", cue_path.filename().string()));
		std::string line;
		while(std::getline(ifs, line))
			LOG("{}", line);
		LOG("");
	}
}


std::list<std::pair<std::string, bool>> cue_get_entries(const std::filesystem::path &cue_path)
{
	std::list<std::pair<std::string, bool>> entries;

	std::fstream fs(cue_path, std::fstream::in);
	if(!fs.is_open())
		throw_line(fmt::format("unable to open file ({})", cue_path.filename().string()));

	std::pair<std::string, bool> entry;
	std::string line;
	while(std::getline(fs, line))
	{
		auto tokens(tokenize(line, " \t", "\"\""));
		if(tokens.size() == 3)
		{
			if(tokens[0] == "FILE")
				entry.first = tokens[1];
			else if(tokens[0] == "TRACK" && !entry.first.empty())
			{
				entry.second = tokens[2] != "AUDIO";
				entries.push_back(entry);
				entry.first.clear();
			}
		}
	}

	return entries;
}


constexpr uint32_t SECTORS_AT_ONCE = 10000;


void redumper_info(const Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	auto tracks = cue_get_entries(image_prefix + ".cue");

	for(auto const &t : tracks)
	{
		if(!t.second)
			continue;

		if(ImageBrowser::IsDataTrack(std::filesystem::path(options.image_path) / t.first))
		{
			ImageBrowser browser(std::filesystem::path(options.image_path) / t.first, 0, false);

			LOG("ISO9660 [{}]:", t.first);

			auto pvd = browser.GetPVD();
			LOG("  PVD:");
			LOG("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
		}
	}
}

}
