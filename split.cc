#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include "analyzers/silence.hh"
#include "analyzers/sync.hh"
#include "systems/system.hh"
#include "common.hh"
#include "crc32.hh"
#include "ecc_edc.hh"
#include "file_io.hh"
#include "image_browser.hh"
#include "logger.hh"
#include "md5.hh"
#include "offset_manager.hh"
#include "scrambler.hh"
#include "sha1.hh"
#include "split.hh"



namespace gpsxre
{

bool correct_program_subq(ChannelQ *subq, uint32_t sectors_count)
{
	uint32_t mcn = sectors_count;
	std::map<uint8_t, uint32_t> isrc;
	ChannelQ q_empty;
	memset(&q_empty, 0, sizeof(q_empty));

	bool invalid_subq = true;
	uint8_t tno = 0;
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		if(!subq[lba_index].Valid())
			continue;

		invalid_subq = false;

		uint8_t adr = subq[lba_index].control_adr & 0x0F;
		if(adr == 1)
			tno = subq[lba_index].mode1.tno;
		else if(adr == 2 && mcn == sectors_count)
			mcn = lba_index;
		else if(adr == 3 && tno && isrc.find(tno) == isrc.end())
			isrc[tno] = lba_index;
	}

	if(invalid_subq)
		return false;

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
					if(bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[j], sizeof(ChannelQ) / sizeof(uint32_t)) < bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[c], sizeof(ChannelQ) / sizeof(uint32_t)))
						c = j;

				subq[lba_index] = candidates[c];
			}
		}
	}

	return true;
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


uint32_t iso9660_volume_size(std::fstream &scm_fs, uint64_t scm_offset, bool scrap)
{
	ImageBrowser browser(scm_fs, scm_offset, 0, !scrap);
	auto volume_size = browser.GetPVD().primary.volume_space_size.lsb;
	return volume_size;
}


bool optional_track(uint32_t track_number)
{
	return track_number == 0x00 || track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER);
}


bool check_tracks(const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, std::shared_ptr<const OffsetManager> offset_manager,
                  const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, bool scrap, const Options &options)
{
	bool no_errors = true;

	std::vector<State> state(CD_DATA_SIZE_SAMPLES);

	LOG("checking tracks");

	auto time_start = std::chrono::high_resolution_clock::now();
	for(auto const &se : toc.sessions)
	{
		for(auto const &t : se.tracks)
		{
			// skip empty tracks
			if(t.lba_end == t.lba_start)
				continue;

			bool data_track = t.control & (uint8_t)ChannelQ::Control::DATA;

			LOG_F("track {}... ", toc.TrackString(t.track_number));

			uint32_t skip_samples = 0;
			uint32_t c2_samples = 0;
			uint32_t skip_sectors = 0;
			uint32_t c2_sectors = 0;

			//FIXME: omit iso9660 volume size if the filesystem is different

			uint32_t track_length = options.iso9660_trim && data_track && !t.indices.empty() ? iso9660_volume_size(scm_fs, (t.indices.front() - LBA_START) * CD_DATA_SIZE + offset_manager->getOffset(t.indices.front()) * CD_SAMPLE_SIZE, scrap) : t.lba_end - t.lba_start;
			for(int32_t lba = t.lba_start; lba < t.lba_start + (int32_t)track_length; ++lba)
			{
				if(inside_range(lba, skip_ranges) != nullptr)
					continue;

				uint32_t skip_count = 0;
				uint32_t c2_count = 0;
				read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);
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

			if(skip_sectors && !optional_track(t.track_number) || c2_sectors)
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


void write_tracks(std::vector<TrackEntry> &track_entries, TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, std::shared_ptr<const OffsetManager> offset_manager,
                  const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, bool scrap, const Options &options)
{
	Scrambler scrambler;
	std::vector<uint8_t> sector(CD_DATA_SIZE);
	std::vector<State> state(CD_DATA_SIZE_SAMPLES);

	// discs with offset shift usually have some corruption in a couple of transitional sectors preventing normal descramble detection,
	// as everything is scrambled in this case, force descrambling
	bool force_descramble = offset_manager->isVariable();

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
			bool data_mode_set = false;

			std::string track_string = toc.TrackString(t.track_number);
			bool lilo = t.track_number == 0x00 || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER);

			// add session number to lead-in/lead-out track string to make filename unique
			if(lilo && toc.sessions.size() > 1)
				track_string = fmt::format("{}.{}", track_string, s.session_number);

			std::string track_name = fmt::format("{}{}.bin", options.image_name, toc.TracksCount() > 1 || lilo ? fmt::format(" (Track {})", track_string) : "");
			LOG("writing \"{}\"", track_name);

			if(std::filesystem::exists(std::filesystem::path(options.image_path) / track_name) && !options.overwrite)
				throw_line(fmt::format("file already exists ({})", track_name));

			std::fstream fs_bin(std::filesystem::path(options.image_path) / track_name, std::fstream::out | std::fstream::binary);
			if(!fs_bin.is_open())
				throw_line(fmt::format("unable to create file ({})", track_name));

			TrackEntry track_entry;
			track_entry.filename = track_name;

			uint32_t crc = crc32_seed();
			MD5 bh_md5;
			SHA1 bh_sha1;

			std::vector<std::pair<int32_t, int32_t>> descramble_errors;

			int32_t lba_end = t.lba_end;
			if(options.iso9660_trim && data_track && !t.indices.empty())
				lba_end = t.lba_start + iso9660_volume_size(scm_fs, (t.indices.front() - LBA_START) * CD_DATA_SIZE + offset_manager->getOffset(t.indices.front()) * CD_SAMPLE_SIZE, scrap);

			for(int32_t lba = t.lba_start; lba < lba_end; ++lba)
			{
				bool generate_sector = false;
				if(!options.leave_unchanged)
				{
					read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);
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
						memset(s.mode2.user_data, optional_track(t.track_number) ? 0x00 : options.skip_fill, sizeof(s.mode2.user_data));
					}
					// audio
					else
						memset(sector.data(), optional_track(t.track_number) ? 0x00 : options.skip_fill, sector.size());
				}
				else
				{
					read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offset_manager->getOffset(lba) * CD_SAMPLE_SIZE, 0);

					// data: needs unscramble
					if(data_track)
					{
						bool success = true;
						if(force_descramble)
							scrambler.Process(sector.data(), sector.data(), 0, sector.size());
						else
							success = scrambler.Descramble(sector.data(), &lba);

						if(success)
						{
							auto data_mode = ((Sector *)sector.data())->header.mode;
							if(!data_mode_set && data_mode < 3)
							{
								t.data_mode = data_mode;
								data_mode_set = true;
							}
						}
						else
						{
							if(descramble_errors.empty() || descramble_errors.back().second + 1 != lba)
								descramble_errors.emplace_back(lba, lba);
							else
								descramble_errors.back().second = lba;
						}
					}
				}

				crc = crc32(sector.data(), sector.size(), crc);
				bh_md5.Update(sector.data(), sector.size());
				bh_sha1.Update(sector.data(), sector.size());

				fs_bin.write((char *)sector.data(), sector.size());
				if(fs_bin.fail())
					throw_line(fmt::format("write failed ({})", track_name));
			}

			for(auto const &d : descramble_errors)
			{
				if(d.first == d.second)
					LOG("warning: descramble failed (LBA: {})", d.first);
				else
					LOG("warning: descramble failed (LBA: [{} .. {}])", d.first, d.second);

				//DEBUG
//				LOG("debug: scram offset: {:08X}", debug_get_scram_offset(d.first, write_offset));
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

	std::set<std::string> tracks;

	std::map<std::string, const TOC::Session::Track *> toc_tracks;
	for(auto const &s : toc.sessions)
		for(auto const &t : s.tracks)
		{
			toc_tracks[toc.TrackString(t.track_number)] = &t;
			tracks.insert(toc.TrackString(t.track_number));
		}

	std::map<std::string, const TOC::Session::Track *> qtoc_tracks;
	for(auto const &s : qtoc.sessions)
		for(auto const &t : s.tracks)
		{
			qtoc_tracks[toc.TrackString(t.track_number)] = &t;
			tracks.insert(toc.TrackString(t.track_number));
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
				LOG("warning: TOC / QTOC mismatch, control (track: {}, control: {:04b} <=> {:04b})", t, tt->second->control, qt->second->control);
			}

			if(tt->second->lba_start != qt->second->lba_start)
			{
				mismatch = true;
				LOG("warning: TOC / QTOC mismatch, track index 00 (track: {}, LBA: {} <=> {})", t, tt->second->lba_start, qt->second->lba_start);
			}

			auto tt_size = tt->second->indices.size();
			auto qt_size = qt->second->indices.size();
			if(tt_size == qt_size)
			{
				if(tt_size && qt_size && tt->second->indices.front() != qt->second->indices.front())
				{
					mismatch = true;
					LOG("warning: TOC / QTOC mismatch, track index 01 (track: {}, LBA: {} <=> {})", t, tt->second->indices.front(), qt->second->indices.front());
				}
			}
			else
			{
				mismatch = true;
				LOG("warning: TOC / QTOC mismatch, track index size (track: {})", t);
			}

			if(tt->second->lba_end != qt->second->lba_end)
			{
				mismatch = true;
				LOG("warning: TOC / QTOC mismatch, track length (track: {}, LBA: {} <=> {})", t, tt->second->lba_end, qt->second->lba_end);
			}
		}
		else
		{
			if(tt == toc_tracks.end())
			{
				mismatch = true;
				LOG("warning: TOC / QTOC mismatch, nonexistent track in TOC (track: {})", t);
			}

			if(qt == qtoc_tracks.end())
			{
				mismatch = true;
				LOG("warning: TOC / QTOC mismatch, nonexistent track in QTOC (track: {})", t);
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


void analyze_scram_samples(std::fstream &scm_fs, std::fstream &state_fs, uint32_t samples_count, uint32_t batch_size, const std::list<std::shared_ptr<Analyzer>> &analyzers)
{
	std::vector<uint32_t> samples(batch_size);
	std::vector<State> state(batch_size);

	batch_process_range<uint32_t>(std::pair(0, samples_count), batch_size, [&scm_fs, &state_fs, &samples, &state, &analyzers](int32_t offset, int32_t size, bool last) -> bool
	{
		read_entry(scm_fs, (uint8_t *)samples.data(), CD_SAMPLE_SIZE, offset, size, 0, 0);
		read_entry(state_fs, (uint8_t *)state.data(), 1, offset, size, 0, (uint8_t)State::ERROR_SKIP);

		for(auto const &a : analyzers)
			a->process(samples.data(), state.data(), size, offset, last);

		return false;
	});
}


uint16_t disc_offset_by_silence(std::vector<std::pair<int32_t, int32_t>> &offset_ranges,
		const std::vector<std::pair<int32_t, int32_t>> &index0_ranges, const std::vector<std::vector<std::pair<int32_t, int32_t>>> &silence_ranges)
{
	for(uint16_t t = 0; t < silence_ranges.size(); ++t)
	{
		auto &silence_range = silence_ranges[t];

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
			return t;
	}

	return silence_ranges.size();
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
					scrambler.Process(s, s, 0, CD_DATA_SIZE);
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


uint32_t find_non_zero_range(std::fstream &scm_fs, std::fstream &state_fs, int32_t lba_start, int32_t lba_end, std::shared_ptr<const OffsetManager> offset_manager, bool data_track, bool reverse)
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
		read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offset_manager->getOffset(lba) * CD_SAMPLE_SIZE, 0);

		std::vector<State> state(CD_DATA_SIZE_SAMPLES);
		read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);

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

	return reverse ? lba - lba_end : lba_end - lba;
}


std::string calculate_universal_hash(std::fstream &scm_fs, std::pair<int32_t, int32_t> nonzero_data_range)
{
	SHA1 bh_sha1;

	std::vector<uint32_t> samples(10 * 1024 * 1024); // 10Mb chunk
	batch_process_range<int32_t>(nonzero_data_range, samples.size(), [&scm_fs, &samples, &bh_sha1](int32_t offset, int32_t size, bool) -> bool
	{
		read_entry(scm_fs, (uint8_t *)samples.data(), CD_SAMPLE_SIZE, offset - LBA_START * CD_DATA_SIZE_SAMPLES, size, 0, 0);
		bh_sha1.Update((uint8_t *)samples.data(), size * sizeof(uint32_t));

		return false;
	});

	return bh_sha1.Final();
}


void offset_shift_shrink_gaps(std::vector<SyncAnalyzer::Record> &offsets, std::fstream &scm_fs, std::fstream &state_fs)
{
	std::vector<uint8_t> data(CD_DATA_SIZE);

	for(uint32_t i = 0; i + 1 < offsets.size(); ++i)
	{
		for(int32_t lba = offsets[i + 1].range.first - 1; lba > offsets[i].range.second; --lba)
		{
			read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offsets[i + 1].offset * CD_SAMPLE_SIZE, 0);
			auto sync_diff = diff_bytes_count(data.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
			if(sync_diff <= OFFSET_SHIFT_SYNC_TOLERANCE)
				offsets[i + 1].range.first = lba;
			else
				break;
		}
	}
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
						ImageBrowser browser(scm_fs, -LBA_START * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE, 0, !scrap);
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

	bool scrap = !std::filesystem::exists(scm_path) && std::filesystem::exists(scp_path);
	auto scra_path(scrap ? scp_path : scm_path);

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
	}

	// preload subchannel Q
	std::vector<ChannelQ> subq;
	if(std::filesystem::exists(sub_path))
	{
		std::fstream fs(sub_path, std::fstream::in | std::fstream::binary);
		if(!fs.is_open())
			throw_line(fmt::format("unable to open file ({})", sub_path.filename().string()));

		subq.resize(sectors_count);
		std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
		for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
		{
			read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), lba_index, 1, 0, 0);
			subcode_extract_channel((uint8_t *)&subq[lba_index], sub_buffer.data(), Subchannel::Q);
		}

		// correct Q
		LOG_F("correcting Q... ");
		if(!correct_program_subq(subq.data(), sectors_count))
			subq.clear();
		LOG("done");
		LOG("");

	}

	if(subq.empty())
	{
		LOG("warning: subchannel data is not available, generating TOC index 0 entries");
		toc.GenerateIndex0();
	}
	else
		toc.UpdateQ(subq.data(), sectors_count, LBA_START);

	LOG("final TOC:");
	toc.Print();
	LOG("");

	if(!subq.empty())
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

	std::list<std::shared_ptr<Analyzer>> analyzers;

	auto index0_ranges = audio_get_toc_index0_ranges(toc);
	auto silence_analyzer = std::make_shared<SilenceAnalyzer>(options.audio_silence_threshold, index0_ranges);
	analyzers.emplace_back(silence_analyzer);

	auto sync_analyzer = std::make_shared<SyncAnalyzer>(scrap);
	analyzers.emplace_back(sync_analyzer);

	LOG_F("analyzing image... ");
	analyze_scram_samples(scm_fs, state_fs, std::filesystem::file_size(scra_path), CD_DATA_SIZE_SAMPLES, analyzers);
	LOG("done");
	LOG("");

	auto silence_ranges = silence_analyzer->ranges();
	auto nonzero_data_range = std::pair(silence_ranges.front().front().second, silence_ranges.front().back().first);

	std::pair<int32_t, int32_t> nonzero_toc_range(toc.sessions.front().tracks.front().lba_start * CD_DATA_SIZE_SAMPLES, toc.sessions.back().tracks.back().lba_start * CD_DATA_SIZE_SAMPLES);
	LOG("non-zero  TOC sample range: [{:+9} .. {:+9}]", nonzero_toc_range.first, nonzero_toc_range.second);
	LOG("non-zero data sample range: [{:+9} .. {:+9}]", nonzero_data_range.first, nonzero_data_range.second);
	LOG("Universal Hash (SHA-1): {}", calculate_universal_hash(scm_fs, nonzero_data_range));
	LOG("");

	std::vector<std::pair<int32_t, int32_t>> offsets;

	// data track
	if(offsets.empty())
	{
		auto data_offsets = sync_analyzer->getOffsets();

		uint32_t count = 0;
		for(auto const &o : data_offsets)
			count += o.count;

		if(count >= CD_PREGAP_SIZE)
		{
			offset_shift_shrink_gaps(data_offsets, scm_fs, state_fs);

			LOG("data disc detected, track offset statistics:");
			for(auto const &o : data_offsets)
				LOG("  LBA: [{:6} .. {:6}], offset: {:+}, count: {}", o.range.first, o.range.second, o.offset, o.count);

			for(auto const &o : data_offsets)
				offsets.emplace_back(o.range.first, o.offset);
		}
	}

	if(scrap)
	{
		if(offsets.empty())
			throw_line("no data sectors detected in scrap mode");

		if(offsets.size() == 1)
		{
			int32_t write_offset_data = offsets.front().second;

			int32_t write_offset = std::numeric_limits<int32_t>::max();
			if(options.force_offset)
				write_offset = *options.force_offset;
			else
			{
				// try to detect positive offset based on scrambled data track overlap into audio
				write_offset = disc_offset_by_overlap(toc, scm_fs, write_offset_data);
				if(write_offset != std::numeric_limits<int32_t>::max())
					LOG("overlap offset detected");
			}

			// interleave data and audio offsets
			for(auto const &s : toc.sessions)
				for(auto const &t : s.tracks)
				{
					auto o = t.control & (uint8_t)ChannelQ::Control::DATA ? write_offset_data : write_offset;
					if(offsets.empty() || o != offsets.back().second)
						offsets.emplace_back(t.lba_start, o);
				}
		}
		else
			LOG("warning: offset shift detected in scrap mode");
	}
	else
	{
		if(options.force_offset)
		{
			offsets.clear();
			offsets.emplace_back(0, *options.force_offset);
		}
	}

	// Atari Jaguar CD
	if(offsets.empty() && toc.sessions.size() == 2 && !(toc.sessions.back().tracks.front().control & (uint8_t)ChannelQ::Control::DATA))
	{
		auto &t = toc.sessions.back().tracks.front();

		if(!t.indices.empty())
		{
			int32_t byte_offset = byte_offset_by_magic(t.indices.front() - 1, t.indices.front() + 1, state_fs, scm_fs, std::string("TAIRTAIR"));
			if(byte_offset != std::numeric_limits<int32_t>::max())
			{
				byte_offset -= sizeof(uint16_t);
				offsets.emplace_back(0, byte_offset / CD_SAMPLE_SIZE - CD_DATA_SIZE_SAMPLES);
				LOG("Atari Jaguar disc detected");
			}
		}
	}

	// perfect audio offset
	if(offsets.empty())
	{
		std::vector<std::pair<int32_t, int32_t>> offset_ranges;
		uint16_t silence_level = disc_offset_by_silence(offset_ranges, index0_ranges, silence_ranges);
		if(silence_level < silence_ranges.size())
		{
			LOG_F("Perfect Audio Offset (silence level: {}): ", silence_level);
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
			if(offset_ranges.size() == 1 && offset_ranges.front().first == offset_ranges.front().second && !silence_level)
			{
				offsets.emplace_back(0, offset_ranges.front().first);
				LOG("Perfect Audio Offset applied");
			}
		}
	}

	// move data
	if(offsets.empty())
	{
		int32_t toc_sample_size = nonzero_toc_range.second - nonzero_toc_range.first;
		int32_t data_sample_size = nonzero_data_range.second - nonzero_data_range.first;

		// attempt to move data only if sample data range fits into TOC calculated range
		if(data_sample_size <= toc_sample_size)
		{
			// move data out of lead-out
			if(nonzero_data_range.second > nonzero_toc_range.second)
			{
				int32_t write_offset = nonzero_data_range.second - nonzero_toc_range.second;
				offsets.emplace_back(0, write_offset);
				LOG("moving data out of lead-out (difference: {:+})", write_offset);
			}
			// move data out of lead-in only if we can get rid of it whole
			else if(nonzero_data_range.first < 0 && data_sample_size <= nonzero_toc_range.second)
			{
				int32_t write_offset = nonzero_data_range.first;
				offsets.emplace_back(0, write_offset);
				LOG("moving data out of lead-in (difference: {:+})", write_offset);
			}
			// move data out of TOC
			else if(nonzero_data_range.first < nonzero_toc_range.first && data_sample_size <= toc_sample_size)
			{
				int32_t write_offset = nonzero_data_range.first - nonzero_toc_range.first;
				offsets.emplace_back(0, write_offset);
				LOG("moving data out of TOC (difference: {:+})", write_offset);
			}
		}
	}

	// fallback
	if(offsets.empty())
	{
		offsets.emplace_back(0, 0);
		LOG("warning: fallback offset 0 applied");
	}

//	if(data_offsets.size() > 1)
//	{
//		LOG("warning: offset shift detected, to apply correction please use an option");
//		LOG("offset shift correction applied");
//	}

	auto offset_manager = std::make_shared<const OffsetManager>(offsets);
	LOG("");
	LOG("disc write offset: {:+}", offset_manager->getOffset(0));

	// identify CD-I tracks, needed for CUE-sheet generation
	for(auto &s : toc.sessions)
		for(auto &t : s.tracks)
			if(t.control & (uint8_t)ChannelQ::Control::DATA && !t.indices.empty())
			{
				// CDI
				try
				{
					int32_t lba = t.indices.front();
					ImageBrowser browser(scm_fs, (lba - LBA_START) * CD_DATA_SIZE + offset_manager->getOffset(lba) * CD_SAMPLE_SIZE, 0, !scrap);

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

	// check if pre-gap is complete
	for(uint32_t i = 0; i < toc.sessions.size(); ++i)
	{
		auto &t = toc.sessions[i].tracks.front();

		int32_t pregap_end = i ? t.indices.front() : 0;
		int32_t pregap_start = pregap_end - CD_PREGAP_SIZE;

		uint32_t unavailable = 0;
		for(int32_t lba = pregap_start; lba != pregap_end; ++lba)
		{
			std::vector<State> state(CD_DATA_SIZE_SAMPLES);
			read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);

			for(auto const &s : state)
				if(s == State::ERROR_SKIP)
				{
					++unavailable;
					break;
				}
		}

		if(unavailable)
			LOG("warning: incomplete pre-gap (session: {}, unavailable: {}/{})", toc.sessions[i].session_number, unavailable, pregap_end - pregap_start);
	}

	// check session pre-gap for non-zero data
	for(uint32_t i = 0; i < toc.sessions.size(); ++i)
	{
		auto &s = toc.sessions[i];
		auto &t = s.tracks.front();

		int32_t leadin_start = i ? toc.sessions[i - 1].tracks.back().lba_end : scale_left(nonzero_data_range.first, CD_DATA_SIZE_SAMPLES);
		int32_t leadin_end = i ? t.indices.front() : 0;

		// do this before new track insertion
		t.lba_start = leadin_end;

		// if it's not empty, construct 00 track with non-zero data
		uint32_t nonzero_count = 0;
		if(leadin_end > leadin_start)
			nonzero_count = find_non_zero_range(scm_fs, state_fs, leadin_start, leadin_end, offset_manager, t.control & (uint8_t)ChannelQ::Control::DATA, false);
		if(nonzero_count)
		{
			auto t_00 = t;

			t_00.track_number = 0;
			t_00.lba_start = leadin_start;
			t_00.lba_end = leadin_end;
			t_00.indices.clear();

			s.tracks.insert(s.tracks.begin(), t_00);

			LOG("warning: lead-in contains non-zero data (session: {}, sectors: {}/{})", s.session_number, nonzero_count, leadin_end - leadin_start);
		}
	}

	// check session lead-out for non-zero data
	for(auto &s : toc.sessions)
	{
		auto &t = s.tracks.back();

		auto nonzero_count = find_non_zero_range(scm_fs, state_fs, t.lba_start, t.lba_end, offset_manager, t.control & (uint8_t)ChannelQ::Control::DATA, true);
		if(nonzero_count)
			LOG("warning: lead-out contains non-zero data (session: {}, sectors: {}/{})", s.session_number, nonzero_count, t.lba_end - t.lba_start);

		t.lba_end = t.lba_start + nonzero_count;
	}

	// check if session lead-in/lead-out is isolated by one good sector
	for(uint32_t i = 0; i < toc.sessions.size(); ++i)
	{
		auto &t_s = toc.sessions[i].tracks.front();
		auto &t_e = toc.sessions[i].tracks.back();

		std::vector<State> state(CD_DATA_SIZE_SAMPLES);

		read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, t_s.lba_start - 1 - LBA_START, 1, -offset_manager->getOffset(t_s.lba_start - 1), (uint8_t)State::ERROR_SKIP);
		for(auto const &s : state)
			if(s == State::ERROR_SKIP)
			{
				LOG("warning: lead-in starts with unavailable sector (session: {})", toc.sessions[i].session_number);
				break;
			}

		read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, t_e.lba_end - LBA_START, 1, -offset_manager->getOffset(t_e.lba_end), (uint8_t)State::ERROR_SKIP);
		for(auto const &s : state)
			if(s == State::ERROR_SKIP)
			{
				LOG("warning: lead-out ends with unavailable sector (session: {})", toc.sessions[i].session_number);
				break;
			}
	}

	std::vector<std::pair<int32_t, int32_t>> skip_ranges = string_to_ranges(options.skip);

	// check tracks
	if(!check_tracks(toc, scm_fs, state_fs, offset_manager, skip_ranges, scrap, options) && !options.force_split)
		throw_line(fmt::format("data errors detected, unable to continue"));

	// write tracks
	std::vector<TrackEntry> track_entries;
	write_tracks(track_entries, toc, scm_fs, state_fs, offset_manager, skip_ranges, scrap, options);

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


void redumper_info(const Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	auto tracks = cue_get_entries(image_prefix + ".cue");

	for(auto const &t : tracks)
	{
		auto track_path = std::filesystem::path(options.image_path) / t.first;
		auto systems = System::get().getSystems(track_path);

		for(auto const &s : systems)
		{
			std::stringstream ss;
			s(ss);
			if(ss.rdbuf()->in_avail())
				LOG("{}", ss.str());
		}
	}
}

}
