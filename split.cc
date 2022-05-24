#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include "common.hh"
#include "crc32.hh"
#include "ecc_edc.hh"
#include "file_io.hh"
#include "image_browser.hh"
#include "md5.hh"
#include "scrambler.hh"
#include "sha1.hh"
#include "split.hh"



namespace gpsxre
{

constexpr uint8_t CDI_EMPTY_SYNC[] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
constexpr uint32_t CDI_MAX_OFFSET_SHIFT = 4;


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
	std::vector<State> state(sectors_to_check * SECTOR_STATE_SIZE);

	uint32_t groups_count = (lba_end - lba_start) / sectors_to_check;
	for(uint32_t i = 0; i < groups_count; ++i)
	{
		int32_t lba = lba_start + i * sectors_to_check;
		read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba - LBA_START, sectors_to_check, 0, 0);
		read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba - LBA_START, sectors_to_check, 0, (uint8_t)State::ERROR_SKIP);

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
				scrambler.Process((uint8_t *)&sector, (uint8_t *)&sector);
				int32_t sector_lba = BCDMSF_to_LBA(sector.header.address);

				write_offset = ((int32_t)sector_offset - (sector_lba - lba) * (int32_t)CD_DATA_SIZE) / (int32_t)CD_SAMPLE_SIZE;
				break;
			}
		}
	}

	return write_offset;
}


int32_t track_process_offset_shift(int32_t write_offset, int32_t lba, uint32_t count, std::fstream &state_fs, std::fstream &scm_fs, const std::filesystem::path &track_path)
{
	int32_t write_offset_next = std::numeric_limits<int32_t>::max();

	std::vector<State> state(count * SECTOR_STATE_SIZE);
	read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba - LBA_START, count, -write_offset, (uint8_t)State::ERROR_SKIP);
	for(auto const &s : state)
		if(s == State::ERROR_SKIP || s == State::ERROR_C2)
			return write_offset_next;

	std::vector<uint8_t> data(count * CD_DATA_SIZE);
	read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba - LBA_START, count, -write_offset * CD_SAMPLE_SIZE, 0);

	uint32_t sector_shift = 0;
	for(uint32_t i = 0; i < data.size() - sizeof(CD_DATA_SYNC); i += CD_SAMPLE_SIZE)
	{
		if(!memcmp(&data[i], CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
		{
			sector_shift = i % CD_DATA_SIZE;
			break;
		}
	}

	if(sector_shift)
	{
		uint32_t head_size = sizeof(Sector::sync) + sizeof(Sector::header);
		for(uint32_t i = sector_shift; i + head_size <= data.size(); i += CD_DATA_SIZE)
		{
			Sector &sector = *(Sector *)&data[i];
			Scrambler scrambler;
			scrambler.Process((uint8_t *)&sector, (uint8_t *)&sector, head_size);

			// expected sector
			int32_t sector_lba = BCDMSF_to_LBA(sector.header.address);
			if(sector_lba >= lba && sector_lba < lba + (int32_t)count)
			{
				int32_t sector_offset = (int32_t)i - (sector_lba - lba) * CD_DATA_SIZE;
				if(sector_offset > 0)
				{
					std::filesystem::path track_garbage_path = std::format("{}.{:06}", track_path.string(), lba);
					std::fstream fs(track_garbage_path, std::fstream::out | std::fstream::binary);
					if(!fs.is_open())
						throw_line(std::format("unable to create file ({})", track_garbage_path.filename().string()));
					fs.write((char *)data.data(), sector_offset);
				}

				write_offset_next = write_offset + sector_offset / (int32_t)CD_SAMPLE_SIZE;
				break;
			}
		}
	}
	
	return write_offset_next;
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


uint32_t iso9660_volume_size(std::fstream &scm_fs, uint64_t scm_offset)
{
	ImageBrowser browser(scm_fs, scm_offset, true);
	auto volume_size = browser.GetPVD().primary.volume_space_size.lsb;
	return volume_size;
}


bool check_tracks(const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, int32_t write_offset, const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, int32_t lba_start, const Options &options)
{
	bool no_errors = true;

	std::vector<State> state(SECTOR_STATE_SIZE);

	std::cout << "checking tracks" << std::endl;

	auto time_start = std::chrono::high_resolution_clock::now();
	for(auto const &se : toc.sessions)
	{
		for(auto const &t : se.tracks)
		{
			std::cout << std::format("track {}... ", t.track_number) << std::flush;

			uint32_t skip_samples = 0;
			uint32_t c2_samples = 0;
			uint32_t skip_sectors = 0;
			uint32_t c2_sectors = 0;

			//FIXME: omit iso9660 volume size if the filesystem is different

			uint32_t track_length = options.iso9660_trim && t.control & (uint8_t)ChannelQ::Control::DATA && !t.indices.empty() ? iso9660_volume_size(scm_fs, (-lba_start + t.indices.front()) * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE) : t.lba_end - t.lba_start;
			for(int32_t lba = t.lba_start; lba < t.lba_start + (int32_t)track_length; ++lba)
			{
				if(inside_range(lba, skip_ranges) != nullptr)
				   continue;

				uint32_t lba_index = lba - lba_start;

				uint32_t skip_count = 0;
				uint32_t c2_count = 0;
				read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba_index, 1, -write_offset, (uint8_t)State::ERROR_SKIP);
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
				std::cout << std::format("failed, sectors: {{SKIP: {}, C2: {}}}, samples: {{SKIP: {}, C2: {}}}", skip_sectors, c2_sectors, skip_samples, c2_samples) << std::endl;
				no_errors = false;
			}
			else
				std::cout << "passed" << std::endl;
		}
	}
	auto time_stop = std::chrono::high_resolution_clock::now();

	std::cout << std::format("check complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start).count()) << std::endl << std::endl;

	return no_errors;
}


void write_tracks(std::vector<TrackEntry> &track_entries, const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, int32_t write_offset, const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, int32_t lba_start, const Options &options)
{
	std::string track_format = std::format(" (Track {{:0{}}})", (uint32_t)log10(toc.sessions.back().tracks.back().track_number) + 1);

	Scrambler scrambler;
	std::vector<uint8_t> sector(CD_DATA_SIZE);
	std::vector<State> state(SECTOR_STATE_SIZE);

	std::cout << "splitting tracks" << std::endl;

	auto time_start = std::chrono::high_resolution_clock::now();
	for(auto &s : toc.sessions)
	{
		for(auto &t : s.tracks)
		{
			bool data_track = t.control & (uint8_t)ChannelQ::Control::DATA;

			std::string track_name = std::format("{}{}.bin", options.image_name, toc.sessions.size() == 1 && toc.sessions.front().tracks.size() == 1 ? "" : std::format(track_format, t.track_number));
			std::cout << std::format("{}... ", track_name) << std::flush;

			if(std::filesystem::exists(std::filesystem::canonical(options.image_path) / track_name) && !options.overwrite)
				throw_line(std::format("file already exists ({})", track_name));

			std::fstream fs_bin(std::filesystem::canonical(options.image_path) / track_name, std::fstream::out | std::fstream::binary);
			if(!fs_bin.is_open())
				throw_line(std::format("unable to create file ({})", track_name));

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
				lba_end = t.lba_start + iso9660_volume_size(scm_fs, (-lba_start + t.indices.front()) * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE);

			for(int32_t lba = t.lba_start; lba < lba_end;)
			{
				bool advance = true;

				uint32_t lba_index = lba - lba_start;

				bool skip_sector = false;
				if(!options.force_split)
				{
					read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba_index, 1, -write_offset, (uint8_t)State::ERROR_SKIP);
					for(auto const &s : state)
					{
						if(s == State::ERROR_SKIP || s == State::ERROR_C2)
						{
							skip_sector = true;
							break;
						}
					}
				}

				if(skip_sector)
				{
					// data: generate sector header and fill user data with 0x55
					if(data_track)
					{
						uint8_t fill_byte = 0x55;
						if(options.zero_error_sectors && inside_range(lba, skip_ranges) == nullptr)
							fill_byte = 0x00;

						Sector &s = *(Sector *)sector.data();
						memcpy(s.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
						s.header.address = LBA_to_BCDMSF(lba);
						s.header.mode = t.data_mode;
						memset(s.mode2.user_data, fill_byte, sizeof(s.mode2.user_data));
					}
					// audio: fill whole sector with zeroes
					else
						memset(sector.data(), 0x00, sector.size());
				}
				else
				{
					read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba_index, 1, -write_offset * CD_SAMPLE_SIZE, 0);

					// data: might need unscramble
					if(data_track)
					{
						// unexpected data in sync
						if(memcmp(sector.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC))/* && memcmp(sector.data(), CDI_EMPTY_SYNC, sizeof(CDI_EMPTY_SYNC))*/)
						{
							int32_t write_offset_next = track_process_offset_shift(write_offset, lba, std::min(CDI_MAX_OFFSET_SHIFT, (uint32_t)(lba_end - lba)),
																				   state_fs, scm_fs, std::filesystem::canonical(options.image_path) / track_name);
							if(write_offset_next != std::numeric_limits<int32_t>::max() && write_offset_next != write_offset)
							{
								std::cout << std::endl << std::format("warning: offset shift detected (LBA: {:6}, difference: {:+})", lba, write_offset_next - write_offset) << std::endl;
								write_offset = write_offset_next;
								advance = false;
							}
						}

						if(advance)
						{
							bool unscrambled = scrambler.Unscramble(sector.data(), lba);

							//DEBUG
							if(!unscrambled)
							{
								std::cout << "";
							}
						}
					}
				}

				if(advance)
				{
					crc = crc32(sector.data(), sizeof(Sector), crc);
					bh_md5.Update(sector.data(), sizeof(Sector));
					bh_sha1.Update(sector.data(), sizeof(Sector));
					if(data_track)
					{
						Sector s = *(Sector *)sector.data();
						edc_ecc_check(track_entry, s);
					}

					fs_bin.write((char *)sector.data(), sector.size());
					if(fs_bin.fail())
						throw_line(std::format("write failed ({})", track_name));

					++lba;
				}
			}

			track_entry.crc = crc32_final(crc);
			track_entry.md5 = bh_md5.Final();
			track_entry.sha1 = bh_sha1.Final();
			track_entries.push_back(track_entry);

			std::cout << "done" << std::endl;
		}
	}
	auto time_stop = std::chrono::high_resolution_clock::now();

	std::cout << std::format("split complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start).count()) << std::endl << std::endl;
}


void redumper_protection(Options &options)
{
	if(options.image_name.empty())
		throw_line("no image name provided");

	std::string image_prefix = (std::filesystem::canonical(options.image_path) / options.image_name).string();

	std::filesystem::path scm_path(image_prefix + ".scram");
	std::filesystem::path sub_path(image_prefix + ".sub");
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
	std::filesystem::path log_path(image_prefix + "_protection.txt");

	uint32_t sectors_count = check_file(scm_path, CD_DATA_SIZE);
	if(check_file(sub_path, CD_SUBCODE_SIZE) != sectors_count)
		throw_line(std::format("file sizes mismatch ({} <=> {})", scm_path.filename().string(), sub_path.filename().string()));
	if(check_file(state_path, SECTOR_STATE_SIZE) != sectors_count)
		throw_line(std::format("file sizes mismatch ({} <=> {})", scm_path.filename().string(), state_path.filename().string()));

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

	std::cout << std::endl << "TOC:" << std::endl;
	toc.Print(std::cout, "  ", 1);
	std::cout << std::endl;

	//TODO: review TOC altering

	{
		auto &t = toc.sessions.back().tracks.back();

		// fake TOC
		if(t.lba_end < 0)
		{
			std::cout << std::format("warning: fake TOC detected, using default 74min disc size") << std::endl;
			t.lba_end = MSF_to_LBA(MSF{74, 0, 0});
		}

		// incomplete dump (dumped with --stop-lba)
		if(t.lba_end > (int32_t)sectors_count + LBA_START)
		{
			std::cout << std::format("warning: incomplete dump detected, using available dump size") << std::endl;
			t.lba_end = (int32_t)sectors_count + LBA_START;
		}
	}

	std::fstream scm_fs(scm_path, std::fstream::in | std::fstream::binary);
	if(!scm_fs.is_open())
		throw_line(std::format("unable to open file ({})", scm_path.filename().string()));

	std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
	if(!state_fs.is_open())
		throw_line(std::format("unable to open file ({})", state_path.filename().string()));

	std::string protection("<NONE>");

	std::cout << "scan started" << std::endl;

	auto scan_time_start = std::chrono::high_resolution_clock::now();

	// PS2 Datel DATA.DAT / BIG.DAT
	// only one track
	if(toc.sessions.size() == 1 && toc.sessions.front().tracks.size() == 1)
	{
		// data track
		auto &t = toc.sessions.front().tracks.front();
		if(t.control & (uint8_t)ChannelQ::Control::DATA)
		{
			std::vector<State> state(SECTOR_STATE_SIZE);

			int32_t write_offset = track_offset_by_sync(t.indices.front(), t.lba_end, state_fs, scm_fs);
			if(write_offset != std::numeric_limits<int32_t>::max())
			{
				// preliminary check
				bool candidate = false;
				{
					constexpr int32_t lba_check = 50;
					if(lba_check >= t.indices.front() && lba_check < t.lba_end)
					{
						read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba_check - LBA_START, 1, -write_offset, (uint8_t)State::ERROR_SKIP);
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
						ImageBrowser browser(scm_fs, -LBA_START * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE, true);
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
							read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba - LBA_START, 1, -write_offset, (uint8_t)State::ERROR_SKIP);

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
							protection = std::format("PS2/Datel {}, C2: {}, range: {}-{}", protected_filename, range.second - range.first, range.first, range.second - 1);

							auto skip_ranges = string_to_ranges(options.skip);
							skip_ranges.push_back(range);
							options.skip = ranges_to_string(skip_ranges);
						}
					}
				}
			}
		}
	}

	std::cout << std::format("protection: {}", protection) << std::endl;

	auto scan_time_stop = std::chrono::high_resolution_clock::now();
	std::cout << std::format("scan complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(scan_time_stop - scan_time_start).count()) << std::endl << std::endl;

	{
		std::fstream ofs(log_path, std::fstream::out);
		if(!ofs.is_open())
			throw_line(std::format("unable to create file ({})", log_path.filename().string()));
		ofs << std::format("DUMPER: {}", redumper_version()) << std::endl;
		ofs << std::format("COMMAND: {}", options.command) << std::endl;
		ofs << std::endl;

		ofs << "PROTECTION: " << std::endl;
		ofs << std::format("\t{}", protection) << std::endl;
		ofs << std::endl;
	}
}


void redumper_split(const Options &options)
{
	if(options.image_name.empty())
		throw_line("no image name provided");

	std::string image_prefix = (std::filesystem::canonical(options.image_path) / options.image_name).string();

	std::filesystem::path scm_path(image_prefix + ".scram");
	std::filesystem::path sub_path(image_prefix + ".sub");
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
	std::filesystem::path cdtext_path(image_prefix + ".cdtext");
	std::filesystem::path log_path(image_prefix + "_split.txt");

	uint32_t sectors_count = check_file(scm_path, CD_DATA_SIZE);
	if(check_file(sub_path, CD_SUBCODE_SIZE) != sectors_count)
		throw_line(std::format("file sizes mismatch ({} <=> {})", scm_path.filename().string(), sub_path.filename().string()));
	if(check_file(state_path, SECTOR_STATE_SIZE) != sectors_count)
		throw_line(std::format("file sizes mismatch ({} <=> {})", scm_path.filename().string(), state_path.filename().string()));

	std::fstream scm_fs(scm_path, std::fstream::in | std::fstream::binary);
	if(!scm_fs.is_open())
		throw_line(std::format("unable to open file ({})", scm_path.filename().string()));

	std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
	if(!state_fs.is_open())
		throw_line(std::format("unable to open file ({})", state_path.filename().string()));

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
	{
		std::fstream fs(sub_path, std::fstream::in | std::fstream::binary);
		if(!fs.is_open())
			throw_line(std::format("unable to open file ({})", sub_path.filename().string()));

		std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
		for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
		{
			read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), lba_index, 1, 0, 0);
			subcode_extract_channel((uint8_t *)&subq[lba_index], sub_buffer.data(), Subchannel::Q);
		}
	}

	// correct Q
	std::cout << "correcting Q... ";
	correct_program_subq(subq.data(), sectors_count);
	std::cout << "done" << std::endl;

	toc.UpdateQ(subq.data(), sectors_count, LBA_START);
	std::cout << std::endl << "TOC:" << std::endl;
	toc.Print(std::cout, "  ", 1);

	TOC qtoc(subq.data(), sectors_count, LBA_START);

	// derive disc type and track control from TOC
	qtoc.Derive(toc);

	std::cout << std::endl << "QTOC:" << std::endl;
	qtoc.Print(std::cout, "  ", 1);
	std::cout << std::endl;

	// compare TOC and QTOC
	bool use_qtoc = false;
	if(toc.sessions.size() == qtoc.sessions.size())
	{
		uint32_t tracks_count = 0;
		uint32_t qtracks_count = 0;
		bool tracks_match = true;
		for(uint32_t i = 0; i < toc.sessions.size(); ++i)
		{
			if(toc.sessions[i].tracks.size() != qtoc.sessions[i].tracks.size())
			{
				tracks_match = false;
				std::cout << std::format("warning: TOC / QTOC session track count mismatch (session: {})", toc.sessions[i].session_number) << std::endl;
			}
			tracks_count += (uint32_t)toc.sessions[i].tracks.size();
			qtracks_count += (uint32_t)qtoc.sessions[i].tracks.size();
		}

		if(qtracks_count > tracks_count)
			use_qtoc = true;

		if(tracks_match)
		{
			for(uint32_t i = 0; i < toc.sessions.size(); ++i)
			{
				for(uint32_t j = 0; j < toc.sessions[i].tracks.size(); ++j)
				{
					if(toc.sessions[i].tracks[j].indices.size() != qtoc.sessions[i].tracks[j].indices.size() || toc.sessions[i].tracks[j].indices.front() != qtoc.sessions[i].tracks[j].indices.front())
						std::cout << std::format("warning: TOC / QTOC index 01 mismatch (session: {}, track: {})", toc.sessions[i].session_number, toc.sessions[i].tracks[j].track_number) << std::endl;

					if(j == toc.sessions[i].tracks.size() - 1 && toc.sessions[i].tracks[j].lba_end != qtoc.sessions[i].tracks[j].lba_end)
						std::cout << std::format("warning: TOC / QTOC lead-out mismatch (session: {}, track: {})", toc.sessions[i].session_number, toc.sessions[i].tracks[j].track_number) << std::endl;
				}
			}
		}
	}
	else
	{
		std::cout << "warning: TOC / QTOC session count mismatch" << std::endl;
		if(qtoc.sessions.size() > toc.sessions.size())
			use_qtoc = true;
	}

	if(!options.force_toc && (options.force_qtoc || use_qtoc))
	{
		toc = qtoc;
		std::cout << "warning: split is performed by QTOC" << std::endl;
	}

	toc.UpdateMCN(subq.data(), sectors_count);

	// CD-TEXT
	if(std::filesystem::exists(cdtext_path))
	{
		std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);

		toc.UpdateCDTEXT(cdtext_buffer);
	}

	//TODO: guess TOC disc_type by tracks data if no FULLTOC command available?

	std::vector<std::pair<int32_t, int32_t>> skip_ranges = string_to_ranges(options.skip);

	// determine write offset based on a data track
	int32_t write_offset = std::numeric_limits<int32_t>::max();
	for(auto &s : toc.sessions)
	{
		for(auto &t : s.tracks)
		{
			if(t.control & (uint8_t)ChannelQ::Control::DATA)
			{
				write_offset = track_offset_by_sync(t.indices.empty() ? t.lba_start : t.indices.front(), t.lba_end, state_fs, scm_fs);
				if(write_offset != std::numeric_limits<int32_t>::max())
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
			write_offset = track_offset_by_sync(t.lba_start, t.lba_start + index0_count, state_fs, scm_fs);
			if(write_offset != std::numeric_limits<int32_t>::max() && track_sync_count(t.lba_start, t.lba_start + index0_count, write_offset, scm_fs) > index0_count / 2)
			{
				toc.disc_type = TOC::DiscType::CD_I;

				// shift first track out of pre-gap
				t.lba_start -= MSF_LBA_SHIFT;

				std::cout << "CD-i Ready disc detected, extending first track with pre-gap" << std::endl;
			}
		}
	}

	// audio cd
	if(write_offset == std::numeric_limits<int32_t>::max())
	{
		// TODO: intelligent silence based audio write offset detection
	}

	if(write_offset == std::numeric_limits<int32_t>::max())
		write_offset = 0;

	std::cout << std::format("write offset: {:+}", write_offset) << std::endl;

	// determine data track modes
	for(auto &s : toc.sessions)
	{
		for(auto &t : s.tracks)
		{
			if(!(t.control & (uint8_t)ChannelQ::Control::DATA))
				continue;

			int32_t lba = t.indices.empty() ? t.lba_start : t.indices.front();
			Sector sector;
			read_entry(scm_fs, (uint8_t *)&sector, CD_DATA_SIZE, lba - LBA_START, 1, -write_offset * CD_SAMPLE_SIZE, 0);

			Scrambler scrambler;
			scrambler.Unscramble((uint8_t *)&sector, lba);

			t.data_mode = sector.header.mode;
		}
	}

	// check first track index 0 of each session for non zero data
	for(auto &s : toc.sessions)
	{
		TOC::Session::Track &t = s.tracks.front();
		if(t.control & (uint8_t)ChannelQ::Control::DATA)
		{
			// unconditionally strip index 0 from first data track of each session
			// TODO: analyze if not empty, store it separate?
			s.tracks.front().lba_start = s.tracks.front().indices.front();
		}
		else
		{
			uint32_t lba_index = t.lba_start - LBA_START;
			std::vector<uint32_t> data_samples((t.indices.front() - t.lba_start) * SECTOR_STATE_SIZE);
			read_entry(scm_fs, (uint8_t *)data_samples.data(), CD_DATA_SIZE, lba_index, t.indices.front() - t.lba_start, -write_offset * CD_SAMPLE_SIZE, 0);

			std::vector<State> state((t.indices.front() - t.lba_start) * SECTOR_STATE_SIZE);
			read_entry(state_fs, (uint8_t *)state.data(), SECTOR_STATE_SIZE, lba_index, t.indices.front() - t.lba_start, -write_offset, (uint8_t)State::ERROR_SKIP);

			bool lead_scan = true;
			uint32_t lead_zero_samples = 0;
			uint32_t error_samples = 0;
			uint32_t non_zero_samples = 0;
			for(uint32_t i = 0; i < (uint32_t)state.size(); ++i)
			{
				if(state[i] == State::ERROR_C2 || state[i] == State::ERROR_SKIP)
				{
					if(lead_scan && lead_zero_samples)
						lead_scan = false;

					++error_samples;
				}
				else
				{
					if(lead_scan)
					{
						if(data_samples[i])
							lead_scan = false;
						else
							++lead_zero_samples;
					}

					if(data_samples[i])
						++non_zero_samples;
				}
			}
			if(error_samples)
				std::cout << std::format("warning: incomplete audio track pre-gap (session: {}, errors: {}, leading zeroes: {}, non-zeroes: {}/{})",
										 s.session_number, error_samples, lead_zero_samples, non_zero_samples, state.size() - error_samples) << std::endl;
			//FIXME: cut fixed 150 sectors, not index1!
			if(non_zero_samples == 0)
				t.lba_start = t.indices.front();
		}
	}

	// check tracks
	if(!check_tracks(toc, scm_fs, state_fs, write_offset, skip_ranges, LBA_START, options) && !options.force_split && !options.zero_error_sectors)
		throw_line(std::format("data errors detected, unable to continue"));

	// write tracks
	std::vector<TrackEntry> track_entries;
	write_tracks(track_entries, toc, scm_fs, state_fs, write_offset, skip_ranges, LBA_START, options);

	// write CUE-sheet
	std::vector<std::string> cue_sheets;
	std::cout << "writing CUE-sheet" << std::endl;
	if(toc.cd_text_lang.size() > 1)
	{
		cue_sheets.resize(toc.cd_text_lang.size());
		for(uint32_t i = 0; i < toc.cd_text_lang.size(); ++i)
		{
			cue_sheets[i] = i ? std::format("{}_{:02X}.cue", options.image_name, toc.cd_text_lang[i]) : std::format("{}.cue", options.image_name);
			std::cout << cue_sheets[i] << "... " << std::flush;

			if(std::filesystem::exists(std::filesystem::canonical(options.image_path) / cue_sheets[i]) && !options.overwrite)
				throw_line(std::format("file already exists ({})", cue_sheets[i]));

			std::fstream fs(std::filesystem::canonical(options.image_path) / cue_sheets[i], std::fstream::out);
			if(!fs.is_open())
				throw_line(std::format("unable to create file ({})", cue_sheets[i]));
			toc.PrintCUE(fs, options.image_name, i);
			std::cout << "done" << std::endl;
		}
	}
	else
	{
		cue_sheets.push_back(std::format("{}.cue", options.image_name));
		std::cout << cue_sheets.front() << "... " << std::flush;

		if(std::filesystem::exists(std::filesystem::canonical(options.image_path) / cue_sheets.front()) && !options.overwrite)
			throw_line(std::format("file already exists ({})", cue_sheets.front()));

		std::fstream fs(std::filesystem::canonical(options.image_path) / cue_sheets.front(), std::fstream::out);
		if(!fs.is_open())
			throw_line(std::format("unable to create file ({})", cue_sheets.front()));
		toc.PrintCUE(fs, options.image_name);
		std::cout << "done" << std::endl;
	}

	{
		std::fstream ofs(log_path, std::fstream::out);
		if(!ofs.is_open())
			throw_line(std::format("unable to create file ({})", log_path.filename().string()));
		ofs << std::format("DUMPER: {}", redumper_version()) << std::endl;
		ofs << std::format("COMMAND: {}", options.command) << std::endl;
		ofs << std::endl;

		ofs << "CD: " << std::endl;
		ofs << std::format("\twrite offset: {:+}", write_offset) << std::endl;
		ofs << std::endl;
		
		if(toc.sessions.size() > 1)
		{
			ofs << "MULTISESSION: " << std::endl;
			for(auto const &s : toc.sessions)
				ofs << std::format("\tsession {}: {}-{}", s.session_number, s.tracks.front().indices.empty() ? s.tracks.front().lba_start : s.tracks.front().indices.front(), s.tracks.back().lba_end - 1) << std::endl;
			ofs << std::endl;
		}

		// DATA ERRORS
		for(auto const &t : track_entries)
		{
			if(!t.data)
				continue;
			
			ofs << std::format("DATA ERRORS [{}]:", t.filename) << std::endl;
			ofs << std::format("\tECC: {}", t.ecc_errors) << std::endl;
			ofs << std::format("\tEDC: {}", t.edc_errors) << std::endl;
			if(t.subheader_errors)
				ofs << std::format("\tCD-XA SubHeader mismatch: {}", t.subheader_errors) << std::endl;
			ofs << std::format("\tredump: {}", t.redump_errors) << std::endl;
			ofs << std::endl;
		}
		
		// DAT
		ofs << "DAT:" << std::endl;
		for(auto const &t : track_entries)
		{
			std::string filename = t.filename;
			replace_all_occurences(filename, "&", "&amp;");

			ofs << std::format("<rom name=\"{}\" size=\"{}\" crc=\"{:08x}\" md5=\"{}\" sha1=\"{}\" />", filename, std::filesystem::file_size(std::filesystem::canonical(options.image_path) / t.filename), t.crc, t.md5, t.sha1) << std::endl;
		}
		ofs << std::endl;

		// CUE
		for(auto const &c : cue_sheets)
		{
			ofs << std::format("CUE [{}]:", c) << std::endl;
			std::filesystem::path cue_path(std::filesystem::canonical(options.image_path) / c);
			std::fstream ifs(cue_path, std::fstream::in);
			if(!ifs.is_open())
				throw_line(std::format("unable to open file ({})", cue_path.filename().string()));
			std::string line;
			while(std::getline(ifs, line))
				ofs << line << std::endl;
			ofs << std::endl;
		}
	}
}


std::list<std::pair<std::string, bool>> cue_get_entries(const std::filesystem::path &cue_path)
{
	std::list<std::pair<std::string, bool>> entries;

	std::fstream fs(cue_path, std::fstream::in);
	if(!fs.is_open())
		throw_line(std::format("unable to open file ({})", cue_path.filename().string()));

	std::pair<std::string, bool> entry;
	std::string line;
	while(std::getline(fs, line))
	{
		auto tokens(tokenize_quoted(line, " \t", "\"\""));
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
	std::string image_prefix = (std::filesystem::canonical(options.image_path) / options.image_name).string();
	std::filesystem::path log_path(image_prefix + "_info.txt");

	std::fstream ofs(log_path, std::fstream::out);
	if(!ofs.is_open())
		throw_line(std::format("unable to create file ({})", log_path.filename().string()));
	ofs << std::format("DUMPER: {}", redumper_version()) << std::endl;
	ofs << std::format("COMMAND: {}", options.command) << std::endl;
	ofs << std::endl;

	auto tracks = cue_get_entries(image_prefix + ".cue");

	for(auto const &t : tracks)
	{
		if(!t.second)
			continue;

		if(ImageBrowser::IsDataTrack(std::filesystem::canonical(options.image_path) / t.first))
		{
			ImageBrowser browser(std::filesystem::canonical(options.image_path) / t.first, 0, false);

			ofs << std::format("ISO9660 [{}]:", t.first) << std::endl;

			// PVD
			auto pvd = browser.GetPVD();
			ofs << "\tPVD:" << std::endl;
			ofs << hexdump((uint8_t *)&pvd, 0x320, 96);
			ofs << std::endl;
		}
	}
}

}
