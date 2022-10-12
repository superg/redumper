#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "cmd.hh"
#include "common.hh"
#include "file_io.hh"
#include "logger.hh"
#include "scrambler.hh"
#include "split.hh"
#include "subcode.hh"
#include "version.hh"
#include "redumper.hh"



namespace gpsxre
{

std::string redumper_version()
{
	return fmt::format("redumper v{}.{}.{} [{}]", XSTRINGIFY(REDUMPER_VERSION_MAJOR), XSTRINGIFY(REDUMPER_VERSION_MINOR), XSTRINGIFY(REDUMPER_VERSION_PATCH), version::build());
}


void validate_options(Options &options)
{
	if(options.positional.empty())
		options.positional.push_back("cd");

	std::list<std::string> positional;
	for(auto const &p : options.positional)
	{
		if(p == "cd")
		{
			positional.push_back("dump");
			positional.push_back("protection");
			positional.push_back("refine");
			positional.push_back("split");
			positional.push_back("info");
		}
		else
			positional.push_back(p);
	}
	options.positional.swap(positional);

	bool drive_required = false;
	bool name_generate = false;
	for(auto const &p : options.positional)
	{
		if(p == "dump" || p == "refine" || p == "rings")
			drive_required = true;

		if(p == "dump")
			name_generate = true;
	}

	// autodetect drive if not provided
	if(drive_required && options.drive.empty())
	{
		options.drive = first_ready_drive();
		if(options.drive.empty())
			throw_line("no ready drives detected on the system");
	}

	// add drive colon if unspecified
#ifdef _WIN32
	if(!options.drive.empty())
	{
		if(options.drive.back() != ':')
			options.drive += ':';
	}
#endif

	// autogenerate image name if unspecified
	if(name_generate && options.image_name.empty())
	{
		auto drive = options.drive;
		drive.erase(remove(drive.begin(), drive.end(), ':'), drive.end());
		drive.erase(remove(drive.begin(), drive.end(), '/'), drive.end());
		options.image_name = fmt::format("dump_{}_{}", system_date_time("%y%m%d_%H%M%S"), drive);

		Logger::Get().Reset((std::filesystem::path(options.image_path) / options.image_name).string() + ".log");
	}
}


void redumper(Options &options)
{
	validate_options(options);

	bool skip_refine = false;
	for(auto const &p : options.positional)
	{
		// skip refine mode if specified after dump mode and no errors encountered
		if(skip_refine && p == "refine")
			continue;

		LOG("*** MODE: {}", p);

		if(p == "dump")
			skip_refine = !redumper_dump(options, false);
		else if(p == "refine")
			redumper_dump(options, true);
		else if(p == "protection")
			redumper_protection(options);
		else if(p == "split")
			redumper_split(options);
		else if(p == "info")
			redumper_info(options);
		else if(p == "rings")
			redumper_rings(options);
		else if(p == "subchannel")
			redumper_subchannel(options);
		else if(p == "debug")
			redumper_debug(options);
		else
			LOG("warning: unknown mode, skipping ({})", p);
	}
}


bool redumper_dump(const Options &options, bool refine)
{
	SPTD sptd(options.drive);
	drive_init(sptd, options);

	DriveConfig drive_config = drive_get_config(cmd_drive_query(sptd));
	drive_override_config(drive_config, options.drive_type.get(),
						  options.drive_read_offset.get(), options.drive_c2_shift.get(), options.drive_pregap_start.get(), options.drive_read_method.get(), options.drive_sector_order.get());
	LOG("drive path: {}", options.drive);
	LOG("drive: {}", drive_info_string(drive_config));
	LOG("drive configuration: {}", drive_config_string(drive_config));

	if(options.image_name.empty())
		throw_line("image name is not provided");

	LOG("image path: {}", options.image_path.empty() ? "." : options.image_path);
	LOG("image name: {}", options.image_name);

	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	// don't use replace_extension as it messes up paths with dot
	std::filesystem::path scm_path(image_prefix + ".scram");
	std::filesystem::path scp_path(image_prefix + ".scrap");
	std::filesystem::path sub_path(image_prefix + ".subcode");
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
	std::filesystem::path cdtext_path(image_prefix + ".cdtext");
	std::filesystem::path asus_path(image_prefix + ".asus"); //DEBUG

	if(!refine && !options.overwrite && std::filesystem::exists(state_path))
		throw_line(fmt::format("dump already exists (name: {})", options.image_name));

	std::vector<std::pair<int32_t, int32_t>> skip_ranges = string_to_ranges(options.skip); //FIXME: transition to samples
	std::vector<std::pair<int32_t, int32_t>> error_ranges;

	int32_t lba_start = drive_config.pregap_start;
	int32_t lba_end = MSF_to_LBA(MSF{74, 0, 0}); // default: 74min / 650Mb

	// TOC
	std::vector<uint8_t> toc_buffer = cmd_read_toc(sptd);
	TOC toc(toc_buffer, false);

	// FULL TOC
	std::vector<uint8_t> full_toc_buffer = cmd_read_full_toc(sptd);
	if(!full_toc_buffer.empty())
	{
		TOC toc_full(full_toc_buffer, true);

		// [PSX] Motocross Mania
		// [ENHANCED-CD] Vanishing Point
		// PX-W5224TA: incorrect FULL TOC data in some cases
		toc_full.DeriveINDEX(toc);

		// prefer TOC for single session discs and FULL TOC for multisession discs
		if(toc_full.sessions.size() > 1)
			toc = toc_full;
		else
			toc.disc_type = toc_full.disc_type;
	}

	if(!refine)
	{
		LOG("");
		LOG("disc TOC:");
		toc.Print();
		LOG("");
	}

	// BE read mode
	bool scrap = false;
	if(drive_config.read_method == DriveConfig::ReadMethod::BE)
	{
		bool data_tracks = false;
		bool audio_tracks = false;
		for(auto &s : toc.sessions)
		{
			for(auto &t : s.tracks)
			{
				if(t.control & (uint8_t)ChannelQ::Control::DATA)
					data_tracks = true;
				else
					audio_tracks = true;
			}
		}
		
		if(data_tracks)
		{
			// by default don't allow BE mode for mixed data/audio discs
			// can be overriden with specifying any drive type in the options
			if(!options.drive_type && audio_tracks)
			{
				print_supported_drives();
				throw_line("unsupported drive read method for mixed data/audio");
			}

			LOG("warning: unsupported drive read method");

			scrap = true;
		}
	}

	if(refine && (std::filesystem::exists(scm_path) && scrap || std::filesystem::exists(scp_path) && !scrap))
		throw_line("refine using mixed read methods is unsupported");

	if(!refine && !options.image_path.empty())
		std::filesystem::create_directories(options.image_path);

	std::fstream fs_scm(scrap ? scp_path : scm_path, std::fstream::out | (refine ? std::fstream::in : std::fstream::trunc) | std::fstream::binary);
	std::fstream fs_sub(sub_path, std::fstream::out | (refine ? std::fstream::in : std::fstream::trunc) | std::fstream::binary);
	std::fstream fs_state(state_path, std::fstream::out | (refine ? std::fstream::in : std::fstream::trunc) | std::fstream::binary);

	// fake TOC
	// [PSX] Breaker Pro
	if(toc.sessions.back().tracks.back().lba_end < 0)
		LOG("warning: fake TOC detected, using default 74min disc size");
	// last session last track end
	else
		lba_end = toc.sessions.back().tracks.back().lba_end;

	// multisession gaps
	for(uint32_t i = 0; i < toc.sessions.size() - 1; ++i)
		error_ranges.emplace_back(toc.sessions[i].tracks.back().lba_end, toc.sessions[i + 1].tracks.front().indices.front() + drive_config.pregap_start);

	// CD-TEXT
	std::vector<uint8_t> cd_text_buffer;
	{
		auto status = cmd_read_cd_text(sptd, cd_text_buffer);
		if(status.status_code)
			LOG("warning: unable to read CD-TEXT, SCSI ({})", SPTD::StatusMessage(status));
	}

	// compare disc / file TOC to make sure it's the same disc
	if(refine)
	{
		std::vector<uint8_t> toc_buffer_file = read_vector(toc_path);
		if(toc_buffer != toc_buffer_file)
			throw_line("disc / file TOC don't match, refining from a different disc?");
	}
	// store TOC
	else
	{
		write_vector(toc_path, toc_buffer);
		if(!full_toc_buffer.empty())
			write_vector(fulltoc_path, full_toc_buffer);
		if(!cd_text_buffer.empty())
			write_vector(cdtext_path, cd_text_buffer);
	}

	// read lead-in early as it improves the chance of extracting both sessions at once
	if(drive_config.type == DriveConfig::Type::PLEXTOR && drive_config.product_id != "CD-R PX-W4824A" && !options.plextor_skip_leadin)
	{
		std::vector<int32_t> session_lba_start;
		for(uint32_t i = 0; i < toc.sessions.size(); ++i)
			session_lba_start.push_back((i ? toc.sessions[i].tracks.front().indices.front() : 0) + MSF_LBA_SHIFT);

		plextor_store_sessions_leadin(fs_scm, fs_sub, fs_state, sptd, session_lba_start, drive_config, options);
	}

	// override using options
	if(options.lba_start)
		lba_start = *options.lba_start;
	if(options.lba_end)
		lba_end = *options.lba_end;
	
	uint32_t errors = 0;
	uint32_t errors_q = 0;

	// buffers
	std::vector<uint8_t> sector_data(CD_DATA_SIZE);
	std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);
	std::vector<State> sector_state(SECTOR_STATE_SIZE);

	// drive specific
	std::vector<uint8_t> asus_leadout_buffer;

	int32_t lba_refine = LBA_START - 1;
	uint32_t refine_counter = 0;
	uint32_t refine_processed = 0;
	uint32_t refine_count = 0;
	uint32_t refine_retries = options.retries ? options.retries : 1;

	if(refine)
	{
		auto scra_path = scrap ? scp_path : scm_path;
		uint32_t sectors_count = check_file(scra_path, CD_DATA_SIZE);
		if(check_file(sub_path, CD_SUBCODE_SIZE) != sectors_count)
			throw_line(fmt::format("file sizes mismatch ({} <=> {})", scra_path.filename().string(), sub_path.filename().string()));
		if(check_file(state_path, SECTOR_STATE_SIZE) != sectors_count)
			throw_line(fmt::format("file sizes mismatch ({} <=> {})", scra_path.filename().string(), state_path.filename().string()));

		for(int32_t lba = lba_start; lba < lba_end; ++lba)
		{
			int32_t lba_index = lba - LBA_START;
			if(lba_index >= (int32_t)sectors_count)
				break;

			if(inside_range(lba, skip_ranges) != nullptr || inside_range(lba, error_ranges) != nullptr)
				continue;

			bool refine_sector = false;

			read_entry(fs_state, (uint8_t *)sector_state.data(), SECTOR_STATE_SIZE, lba_index, 1, drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
			for(auto const &ss : sector_state)
			{
				if(ss == State::ERROR_C2 || ss == State::ERROR_SKIP)
				{
					++errors;
					refine_sector = true;
					break;
				}
			}

			read_entry(fs_sub, (uint8_t *)sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
			ChannelQ Q;
			subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
			if(!Q.Valid())
			{
				++errors_q;
				if(options.refine_subchannel)
					refine_sector = true;
			}

			if(refine_sector)
				++refine_count;
		}
	}

	uint32_t errors_q_last = errors_q;
	
	LOG("{} started", refine ? "refine" : "dump");

	auto dump_time_start = std::chrono::high_resolution_clock::now();

	int32_t lba_next = 0;
	int32_t lba_overread = lba_end;
	for(int32_t lba = lba_start; lba < lba_overread; lba = lba_next)
	{
		if(auto r = inside_range(lba, skip_ranges); r != nullptr)
		{
			lba_next = r->second;
			continue;
		}
		else
			lba_next = lba + 1;

		int32_t lba_index = lba - LBA_START;

		std::string refine_status;

		bool read = true;
		bool store = false;

		// mirror lead-out
		if(drive_config.type == DriveConfig::Type::LG_ASUS && !options.asus_skip_leadout)
		{
			// initial cache read
			auto r = inside_range(lba, error_ranges);
			if(r != nullptr && lba == r->first || lba == lba_end)
			{
				// dummy read to cache lead-out
				if(refine)
				{
					std::vector<uint8_t> sector_buffer;
					read_sector(sector_buffer, sptd, drive_config, lba - 1);
				}

				LOG_R();
				LOG("LG/ASUS: searching lead-out in cache (LBA: {:6})", lba);
				{
					auto cache = asus_cache_read(sptd);

					//DEBUG
					if(!std::filesystem::exists(asus_path))
						write_vector(asus_path, cache);

					asus_leadout_buffer = asus_cache_extract(cache, lba, 100);
				}

				uint32_t entries_count = (uint32_t)asus_leadout_buffer.size() / CD_RAW_DATA_SIZE;
				
				LOG_R();
				if(entries_count)
					LOG("LG/ASUS: lead-out found (LBA: {:6}, sectors: {})", lba, entries_count);
				else
					LOG("LG/ASUS: lead-out not found");
			}

			if(r != nullptr && lba >= r->first || lba >= lba_end)
			{
				uint32_t leadout_index = lba - (r == nullptr ? lba_end : r->first);
				if(leadout_index < asus_leadout_buffer.size() / CD_RAW_DATA_SIZE)
				{
					uint8_t *entry = &asus_leadout_buffer[CD_RAW_DATA_SIZE * leadout_index];

					memcpy(sector_data.data(), entry, CD_DATA_SIZE);
					memcpy(sector_subcode.data(), entry + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

					std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS_SCSI_OFF);
					auto c2_count = state_from_c2(sector_state, entry + CD_DATA_SIZE);
					if(c2_count)
					{
						if(!refine)
							++errors;
						if(options.verbose)
						{
							LOG_R();
							LOG("[LBA: {:6}] C2 error (bits: {})", lba, c2_count);
						}

						//DEBUG
//						debug_print_c2_scm_offsets(entry + CD_DATA_SIZE, lba_index, LBA_START, drive_config.read_offset);
					}

					store = true;
					read = false;
				}
			}
		}

		if(refine && read)
		{
			read = false;

			read_entry(fs_state, (uint8_t *)sector_state.data(), SECTOR_STATE_SIZE, lba_index, 1, drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
			for(auto const &ss : sector_state)
				if(ss == State::ERROR_C2 || ss == State::ERROR_SKIP)
				{
					read = true;
					break;
				}

			// refine subchannel (based on Q crc)
			if(options.refine_subchannel && !read)
			{
				read_entry(fs_sub, (uint8_t *)sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
				ChannelQ Q;
				subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
				if(!Q.Valid())
					read = true;
			}

			// read sector
			if(read)
			{
				if(lba_refine == lba)
				{
					++refine_counter;
					if(refine_counter < refine_retries)
						lba_next = lba;
					// maximum retries reached
					else
					{
						if(options.verbose)
						{
							LOG_R();
							LOG("[LBA: {:6}] correction failure", lba);
						}
						read = false;
						++refine_processed;
						refine_counter = 0;
					}
				}
				// initial read
				else
				{
					lba_refine = lba;
					lba_next = lba;
				}
			}
			// sector is fixed
			else if(lba_refine == lba)
			{
				if(options.verbose)
				{
					LOG_R();
					LOG("[LBA: {:6}] correction success", lba);
				}
				++refine_processed;
				refine_counter = 0;
			}
		}

		if(read)
		{
			std::vector<uint8_t> sector_buffer;

			if(refine)
				cmd_flush_drive_cache(sptd, lba);

			auto read_time_start = std::chrono::high_resolution_clock::now();
			auto status = read_sector(sector_buffer, sptd, drive_config, lba);
			auto read_time_stop = std::chrono::high_resolution_clock::now();
			bool slow = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;

			// PLEXTOR: multisession lead-out overread
			// usually there are couple of slow sectors before SCSI error is generated
			// some models (PX-708UF) exit on I/O semaphore timeout on such slow sectors
			if(drive_config.type == DriveConfig::Type::PLEXTOR && slow && inside_range(lba, error_ranges) != nullptr)
			{
				// skip sector in refine mode
//				lba_next = lba + 1; //FIXME:
			}
			else if(status.status_code)
			{
				// don't log lead-out overread SCSI error
				if(inside_range(lba, error_ranges) == nullptr && lba < lba_end)
				{
					if(refine)
						refine_status = fmt::format("R: {}, SCSI", refine_counter + 1);
					else
						++errors;
					if(options.verbose)
					{
						LOG_R();
						LOG("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
					}
				}
			}
			else
			{
				memcpy(sector_data.data(), sector_buffer.data(), CD_DATA_SIZE);
				memcpy(sector_subcode.data(), sector_buffer.data() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

				std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS);
				auto c2_count = state_from_c2(sector_state, sector_buffer.data() + CD_DATA_SIZE);
				if(c2_count)
				{
					if(!refine)
						++errors;
					if(options.verbose)
					{
						LOG_R();
						LOG("[LBA: {:6}] C2 error (bits: {})", lba, c2_count);
					}

					//DEBUG
//					debug_print_c2_scm_offsets(sector_buffer.data() + CD_DATA_SIZE, lba_index, LBA_START, drive_config.read_offset);
				}

				if(refine)
					refine_status = fmt::format("R: {}, C2 (B: {})", refine_counter + 1, c2_count);

				store = true;
			}
		}

		if(store)
		{
			if(refine)
			{
				std::vector<State> sector_state_file(SECTOR_STATE_SIZE);
				std::vector<uint8_t> sector_data_file(CD_DATA_SIZE);
				read_entry(fs_state, (uint8_t *)sector_state_file.data(), SECTOR_STATE_SIZE, lba_index, 1, drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
				read_entry(fs_scm, sector_data_file.data(), CD_DATA_SIZE, lba_index, 1, drive_config.read_offset * CD_SAMPLE_SIZE, 0);

				bool sector_fixed = true;
				bool update = false;
				for(uint32_t i = 0; i < SECTOR_STATE_SIZE; ++i)
				{
					// new data is improved
					if(sector_state[i] > sector_state_file[i])
						update = true;

					// inherit older data if state is better
					if(sector_state_file[i] > sector_state[i])
					{
						sector_state[i] = sector_state_file[i];
						((uint32_t *)sector_data.data())[i] = ((uint32_t *)sector_data_file.data())[i];
					}

					if(sector_state[i] == State::ERROR_C2 || sector_state[i] == State::ERROR_SKIP)
						sector_fixed = false;
				}

				if(update)
				{
					write_entry(fs_scm, sector_data.data(), CD_DATA_SIZE, lba_index, 1, drive_config.read_offset * CD_SAMPLE_SIZE);
					write_entry(fs_state, (uint8_t *)sector_state.data(), SECTOR_STATE_SIZE, lba_index, 1, drive_config.read_offset);

					if(sector_fixed && inside_range(lba, error_ranges) == nullptr)
						--errors;
				}

				ChannelQ Q;
				subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
				if(Q.Valid())
				{
					std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);
					read_entry(fs_sub, (uint8_t *)sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
					ChannelQ Q_file;
					subcode_extract_channel((uint8_t *)&Q_file, sector_subcode_file.data(), Subchannel::Q);
					if(!Q_file.Valid())
					{
						write_entry(fs_sub, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);
						if(inside_range(lba, error_ranges) == nullptr)
							--errors_q;
					}
				}
			}
			else
			{
				write_entry(fs_scm, sector_data.data(), CD_DATA_SIZE, lba_index, 1, drive_config.read_offset * CD_SAMPLE_SIZE);
				write_entry(fs_sub, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);
				write_entry(fs_state, (uint8_t *)sector_state.data(), SECTOR_STATE_SIZE, lba_index, 1, drive_config.read_offset);

				ChannelQ Q;
				subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);
				if(Q.Valid())
				{
					errors_q_last = errors_q;
				}
				else
				{
					// PLEXTOR: some drives desync on subchannel after mass C2 errors with high bit count
					// prevent this by flushing drive cache after C2 error range
					// (flush cache on 5 consecutive Q errors)
					if(errors_q - errors_q_last > 5)
					{
						cmd_flush_drive_cache(sptd, lba);
						errors_q_last = errors_q;
					}

					++errors_q;
				}
			}

			// grow lead-out overread if we still can read
			if(lba + 1 == lba_overread && !options.lba_end)
				++lba_overread;
		}
		else
		{
			// past last session (disc) lead-out
			if(lba + 1 == lba_overread)
				lba_overread = lba;
			// between sessions
			else if(auto r = inside_range(lba, error_ranges); r != nullptr)
				lba_next = r->second;
		}

		if(refine)
		{
			if(lba == lba_refine)
			{
				LOG_R();
				LOGC_F("[{:3}%] LBA: {:6}/{}, errors: {{ SCSI/C2: {}, Q: {} }} {}",
					   percentage(refine_processed * refine_retries + refine_counter, refine_count * refine_retries),
					   lba, lba_overread, errors, errors_q, refine_status);
			}
		}
		else
		{
			LOG_R();
			LOGC_F("[{:3}%] LBA: {:6}/{}, errors: {{ SCSI/C2: {}, Q: {} }}", percentage(lba, lba_overread - 1), lba, lba_overread, errors, errors_q);
		}
	}
	LOGC("");

	// keep files sector aligned
	write_align(fs_scm, lba_overread - LBA_START, CD_DATA_SIZE, 0);
	write_align(fs_state, lba_overread - LBA_START, SECTOR_STATE_SIZE, (uint8_t)State::ERROR_SKIP);
	write_align(fs_sub, lba_overread - LBA_START, CD_SUBCODE_SIZE, 0);

	auto dump_time_stop = std::chrono::high_resolution_clock::now();

	LOG("{} complete (time: {}s)", refine ? "refine" : "dump", std::chrono::duration_cast<std::chrono::seconds>(dump_time_stop - dump_time_start).count());
	LOG("");

	LOG("media errors: ");
	LOG("  SCSI/C2: {}", errors);
	LOG("  Q: {}", errors_q);
	LOG("");

	// always refine once if LG/ASUS to improve changes of capturing more lead-out sectors
	return errors || drive_config.type == DriveConfig::Type::LG_ASUS && !options.asus_skip_leadout;
}


void redumper_rings(const Options &options)
{
	SPTD sptd(options.drive);
	drive_init(sptd, options);

	DriveConfig drive_config = drive_get_config(cmd_drive_query(sptd));
	drive_override_config(drive_config, options.drive_type.get(),
						  options.drive_read_offset.get(), options.drive_c2_shift.get(), options.drive_pregap_start.get(), options.drive_read_method.get(), options.drive_sector_order.get());
	LOG("drive path: {}", options.drive);
	LOG("drive: {}", drive_info_string(drive_config));
	LOG("drive configuration: {}", drive_config_string(drive_config));

	// read TOC
	std::vector<uint8_t> full_toc_buffer = cmd_read_full_toc(sptd);

	TOC toc(full_toc_buffer, true);

	// fake TOC
//	if(toc.sessions.back().tracks.back().lba_end <= toc.sessions.back().tracks.back().lba_start)
//		throw_line("fake TOC detected");

	LOG("");
	LOG("TOC:");
	toc.Print();
	LOG("");

	LOG("skip size: {}", options.skip_size);
	LOG("ring size: {}", options.ring_size);

	int32_t lba_leadout = toc.sessions.back().tracks.back().lba_end;
	uint32_t pass = 0;

	std::vector<SPTD::Status> scsi(lba_leadout, SPTD::Status::RESERVED);
	std::vector<uint8_t> buffer(CD_DATA_SIZE);

	std::vector<std::pair<int32_t, int32_t>> rings;

	LOG("");
	LOG("analyze started");

	auto time_start = std::chrono::high_resolution_clock::now();

	for(bool to_continue = true; to_continue; ++pass)
	{
		to_continue = false;

		LOG("");
		LOG("pass {}", pass + 1);

		for(int32_t lba = 0; lba < lba_leadout;)
		{
			// sector not processed
			if(scsi[lba].status_code == SPTD::Status::RESERVED.status_code)
			{
				LOG_F("\rLBA: {:6}/{}", lba, lba_leadout);

				// broken, TOC incomplete, fix later
				/*scsi[lba] = cmd_read_sector(sptd, buffer.data(), lba, 1, ReadCommand::READ_CD, ReadType::DATA, is_data_track(lba, toc) ? ReadFilter::DATA : ReadFilter::CDDA);
				if(scsi[lba].status_code)
				{
					if(options.verbose)
					{
						LOG_R();
						LOG("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(scsi[lba]));
					}
				}*/
			}

			if(scsi[lba].status_code)
			{
				int32_t lba_next = lba + 1;
				if(pass)
				{
					// not last sector
					if(lba_next < lba_leadout)
					{
						// search next processed sector
						int32_t lba_end = lba_next;
						for(; lba_end < lba_leadout - 1; ++lba_end)
							if(scsi[lba_end].status_code != SPTD::Status::RESERVED.status_code)
								break;

						int32_t gap_size = lba_end - lba;

						if(gap_size > 1)
						{
							// gap between two slow sectors and it's still too big
							if(scsi[lba_end].status_code && scsi[lba_end].status_code != SPTD::Status::RESERVED.status_code)
							{
								if(gap_size < options.ring_size)
									lba_next = lba_end;
								else
								{
									lba_next = lba + gap_size / 2;
									to_continue = true;
								}
							}
							// open end gap
							else
							{
								lba_next = lba + gap_size / 2;
								to_continue = true;
							}
						}
					}
				}
				// pass 0
				else
				{
					lba_next = std::min(lba + options.skip_size, lba_leadout - 1);
					to_continue = true;
				}

				if(options.verbose)
				{
					LOG_R();
					LOG("LBA: {:6}, skipping (LBA: {})", lba, lba_next);
				}

				lba = lba_next;
			}
			else
				++lba;
		}

		{
			rings.clear();

			int32_t ring_start = -1;
			for(int32_t lba = 0; lba < lba_leadout; ++lba)
			{
				if(ring_start == -1)
				{
					if(scsi[lba].status_code && scsi[lba].status_code != 0xFF)
						ring_start = lba;
				}
				else
				{
					if(!scsi[lba].status_code)
					{
						rings.emplace_back(ring_start, lba - 1);
						ring_start = -1;
					}
				}
			}

			// last entry
			if(ring_start != -1)
				rings.emplace_back(ring_start, lba_leadout - 1);

			LOG("");
			LOG("pass rings: ");
			for(auto const &r : rings)
				LOG("{}-{}", r.first, r.second);
		}
	}

	LOG("");
	LOG("final rings: ");
	for(auto const &r : rings)
		LOG("{}-{}", r.first, r.second);

	auto time_stop = std::chrono::high_resolution_clock::now();

	LOG("");
	LOG("analyze completed (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start).count());
	LOG("");
}


void redumper_subchannel(const Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::filesystem::path sub_path(image_prefix + ".subcode");

	uint32_t sectors_count = check_file(sub_path, CD_SUBCODE_SIZE);
	std::fstream sub_fs(sub_path, std::fstream::in | std::fstream::binary);
	if(!sub_fs.is_open())
		throw_line(fmt::format("unable to open file ({})", sub_path.filename().string()));

	ChannelQ q_empty;
	memset(&q_empty, 0, sizeof(q_empty));

	bool empty = false;
	std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
	for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
	{
		read_entry(sub_fs, sub_buffer.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);

		ChannelQ Q;
		subcode_extract_channel((uint8_t *)&Q, sub_buffer.data(), Subchannel::Q);

		// Q is available
		if(memcmp(&Q, &q_empty, sizeof(q_empty)))
		{
			LOG("[LBA: {:6}] {}", LBA_START + (int32_t)lba_index, Q.Decode());
			empty = false;
		}
		else if(!empty)
		{
			LOG("...");
			empty = true;
		}
	}
}


void redumper_debug(const Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path cache_path(image_prefix + ".asus");

	//DEBUG: LG/ASUS cache dump read
	{
		std::vector<uint8_t> cache = read_vector(cache_path);

		asus_cache_print_subq(cache);

//		auto asd = asus_cache_unroll(cache);
//		auto asd = asus_cache_extract(cache, 128224, 0);
//		auto asd = asus_cache_extract(cache, 156097, 100);

		LOG("");
	}


	//DEBUG: convert old state file to new state file
	if(0)
	{
		std::fstream fs_state(state_path, std::fstream::out | std::fstream::in | std::fstream::binary);
		uint64_t states_count = std::filesystem::file_size(state_path) / sizeof(State);
		std::vector<State> states((std::vector<State>::size_type)states_count);
		fs_state.read((char *)states.data(), states.size() * sizeof(State));
		for(auto &s : states)
		{
			uint8_t value = (uint8_t)s;
			if(value == 0)
				s = (State)4;
			else if(value == 1)
				s = (State)3;
			else if(value == 3)
				s = (State)1;
			else if(value == 4)
				s = (State)0;
		}

		fs_state.seekp(0);
		fs_state.write((char *)states.data(), states.size() * sizeof(State));
	}

	LOG("");
}


uint32_t percentage(int32_t value, uint32_t value_max)
{
	if(value < 0)
		return 0;
	else if(!value_max || (uint32_t)value >= value_max)
		return 100;
	else
		return value * 100 / value_max;
}


std::string first_ready_drive()
{
	std::string drive;

	auto drives = SPTD::ListDrives();
	for(const auto &d : drives)
	{
		try
		{
			SPTD sptd(d);

			auto status = cmd_drive_ready(sptd);
			if(!status.status_code)
			{
				drive = d;
				break;
			}
		}
		// drive busy
		catch(const std::exception &)
		{
			;
		}
	}
		
	return drive;
}


void drive_init(SPTD &sptd, const Options &options)
{
	// test unit ready
	SPTD::Status status = cmd_drive_ready(sptd);
	if(status.status_code)
		throw_line(fmt::format("drive not ready, SCSI ({})", SPTD::StatusMessage(status)));

	// set drive speed
	uint16_t speed = options.speed ? 150 * *options.speed : 0xFFFF;
	status = cmd_set_cd_speed(sptd, speed);
	if(status.status_code)
		LOG("drive set speed failed, SCSI ({})", SPTD::StatusMessage(status));
}


SPTD::Status read_sector(std::vector<uint8_t> &sector_buffer, SPTD &sptd, const DriveConfig &drive_config, int32_t lba)
{
	// PLEXTOR: C2 is shifted 294/295 bytes late, read as much sectors as needed to get whole C2
	// as a consequence, lead-out overread will fail a few sectors earlier
	uint32_t sectors_count = drive_config.c2_shift / CD_C2_SIZE + (drive_config.c2_shift % CD_C2_SIZE ? 1 : 0) + 1;

	SPTD::Status status;
	if(drive_config.read_method == DriveConfig::ReadMethod::D8)
	{
		status = cmd_read_cdda(sptd, sector_buffer, lba, sectors_count, READ_CDDA_SubCode::DATA_C2_SUB);
	}
	else if(drive_config.read_method == DriveConfig::ReadMethod::BE_CDDA)
	{
		status = cmd_read_cd(sptd, sector_buffer, lba, sectors_count, READ_CD_ExpectedSectorType::CD_DA, READ_CD_ErrorField::C2, READ_CD_SubChannel::RAW);
	}
	else if(drive_config.read_method == DriveConfig::ReadMethod::BE)
	{
		status = cmd_read_cd(sptd, sector_buffer, lba, sectors_count, READ_CD_ExpectedSectorType::ALL_TYPES, READ_CD_ErrorField::C2, READ_CD_SubChannel::RAW);
	}
	else
		throw_line("invalid drive read mode specified");

	// compensate C2 shift
	if(!status.status_code)
	{
		std::vector<uint8_t> c2_buffer(CD_C2_SIZE * sectors_count);
		for(uint32_t i = 0; i < sectors_count; ++i)
			memcpy(c2_buffer.data() + CD_C2_SIZE * i, sector_buffer.data() + CD_RAW_DATA_SIZE * i + CD_DATA_SIZE, CD_C2_SIZE);

		memcpy(sector_buffer.data() + CD_DATA_SIZE, c2_buffer.data() + drive_config.c2_shift, CD_C2_SIZE);
	}
	sector_buffer.resize(CD_RAW_DATA_SIZE);

	// swap C2 and SUB
	if(drive_config.sector_order == DriveConfig::SectorOrder::DATA_SUB_C2)
	{
		std::vector<uint8_t> sector_buffer_swap(sector_buffer.size());
		memcpy(sector_buffer_swap.data(), sector_buffer.data(), CD_DATA_SIZE);
		memcpy(sector_buffer_swap.data() + CD_DATA_SIZE, sector_buffer.data() + CD_DATA_SIZE + CD_SUBCODE_SIZE, CD_C2_SIZE);
		memcpy(sector_buffer_swap.data() + CD_DATA_SIZE + CD_C2_SIZE, sector_buffer.data() + CD_DATA_SIZE, CD_SUBCODE_SIZE);

		sector_buffer.swap(sector_buffer_swap);
	}

	return status;
}


bool is_data_track(int32_t lba, const TOC &toc)
{
	bool data_track = false;

	for(auto const &s : toc.sessions)
		for(auto const &t : s.tracks)
			if(lba >= t.lba_start && lba < t.lba_end)
			{
				data_track = t.control & (uint8_t)ChannelQ::Control::DATA;
				break;
			}

	return data_track;
}


uint32_t state_from_c2(std::vector<State> &state, const uint8_t *c2_data)
{
	uint32_t c2_count = 0;

	// group 4 C2 consecutive errors into 1 state, this way it aligns to the drive offset
	// and covers the case where for 1 C2 bit there are 2 damaged sector bytes (scrambled data bytes, usually)
	for(uint32_t i = 0; i < SECTOR_STATE_SIZE; ++i)
	{
		uint8_t c2_quad = c2_data[i / 2];
		if(i % 2)
			c2_quad &= 0x0F;
		else
			c2_quad >>= 4;

		if(c2_quad)
		{
			state[i] = State::ERROR_C2;
			c2_count += __builtin_popcount(c2_quad);
		}
	}

	return c2_count;
}


void plextor_store_sessions_leadin(std::fstream &fs_scm, std::fstream &fs_sub, std::fstream &fs_state, SPTD &sptd, const std::vector<int32_t> &session_lba_start, const DriveConfig &di, const Options &options)
{
	std::vector<std::vector<uint8_t>> leadin_buffers(session_lba_start.size());

	// multisession disc:
	// there is no direct control over which session lead-in is returned
	// employ a number of tricks to maximize the chance of getting everything
	for(uint32_t i = 0; i < session_lba_start.size(); ++i)
	{
		LOG_R();
		LOG("PLEXTOR: reading lead-in");

		// helps with "choosing" the first session
		if(i == session_lba_start.size() - 1)
			cmd_flush_drive_cache(sptd, 0xFFFFFFFF);

		auto leadin_buffer = plextor_read_leadin(sptd, di.pregap_start);
		uint32_t entries_count = (uint32_t)leadin_buffer.size() / PLEXTOR_LEADIN_ENTRY_SIZE;

		if(entries_count < (uint32_t)(di.pregap_start - MSF_LBA_SHIFT))
			continue;

		// find which session lead-in belongs to
		for(uint32_t j = entries_count; j > 0; --j)
		{
			uint8_t *entry = &leadin_buffer[(j - 1) * PLEXTOR_LEADIN_ENTRY_SIZE];
			auto status = *(SPTD::Status *)entry;

			if(status.status_code)
				continue;

			uint8_t *sub_data = entry + sizeof(SPTD::Status) + CD_DATA_SIZE;

			ChannelQ Q;
			subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

			if(Q.Valid())
			{
				uint8_t adr = Q.control_adr & 0x0F;
				if(adr == 1 && Q.mode1.tno)
				{
					bool session_found = false;

					int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
					for(uint32_t s = 0; s < (uint32_t)session_lba_start.size(); ++s)
					{
						int32_t pregap_end = session_lba_start[s] + (di.pregap_start - MSF_LBA_SHIFT);
						if(lba >= session_lba_start[s] && lba < pregap_end)
						{
							uint32_t trim_count = j - 1 + pregap_end - lba;

							if(trim_count > entries_count)
							{
								LOG_R();
								LOG("PLEXTOR: incomplete pre-gap, skipping (session index: {})", s);
							}
							else
							{
								LOG_R();
								LOG("PLEXTOR: lead-in found (session index: {}, sectors: {})", s, trim_count);

								if(trim_count < entries_count)
									leadin_buffer.resize(trim_count * PLEXTOR_LEADIN_ENTRY_SIZE);

								if(leadin_buffers[s].size() < leadin_buffer.size())
									leadin_buffers[s].swap(leadin_buffer);
							}

							session_found = true;
							break;
						}
					}

					if(session_found)
						break;
				}
			}
		}
	}

	// store
	LOG_F("storing lead-ins... ");
	for(uint32_t s = 0; s < leadin_buffers.size(); ++s)
	{
		auto &leadin_buffer = leadin_buffers[s];
		uint32_t n = (uint32_t)leadin_buffer.size() / PLEXTOR_LEADIN_ENTRY_SIZE;
		for(uint32_t i = 0; i < n; ++i)
		{
			int32_t lba = session_lba_start[s] + (di.pregap_start - MSF_LBA_SHIFT) - (n - i);
			int32_t lba_index = lba - LBA_START;

			uint8_t *entry = &leadin_buffer[i * PLEXTOR_LEADIN_ENTRY_SIZE];
			auto status = *(SPTD::Status *)entry;

			if(status.status_code)
			{
				if(options.verbose)
				{
					LOG_R();
					LOG("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
				}
			}
			else
			{
				// data
				std::vector<State> sector_state(SECTOR_STATE_SIZE);
				read_entry(fs_state, (uint8_t *)sector_state.data(), SECTOR_STATE_SIZE, lba_index, 1, di.read_offset, (uint8_t)State::ERROR_SKIP);
				for(auto const &s : sector_state)
				{
					// new data is improved
					if(s < State::SUCCESS_C2_OFF)
					{
						uint8_t *sector_data = entry + sizeof(SPTD::Status);
						std::fill(sector_state.begin(), sector_state.end(), State::SUCCESS_C2_OFF);

						write_entry(fs_scm, sector_data, CD_DATA_SIZE, lba_index, 1, di.read_offset * CD_SAMPLE_SIZE);
						write_entry(fs_state, (uint8_t *)sector_state.data(), SECTOR_STATE_SIZE, lba_index, 1, di.read_offset);

						break;
					}
				}

				// subcode
				std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);
				read_entry(fs_sub, (uint8_t *)sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
				ChannelQ Q_file;
				subcode_extract_channel((uint8_t *)&Q_file, sector_subcode_file.data(), Subchannel::Q);
				if(!Q_file.Valid())
				{
					uint8_t *sector_subcode = entry + sizeof(SPTD::Status) + CD_DATA_SIZE;
					write_entry(fs_sub, sector_subcode, CD_SUBCODE_SIZE, lba_index, 1, 0);
				}
			}
		}
	}
	LOG("done");
}


void debug_print_c2_scm_offsets(const uint8_t *c2_data, uint32_t lba_index, int32_t lba_start, int32_t drive_read_offset)
{
	uint32_t scm_offset = lba_index * CD_DATA_SIZE - drive_read_offset * CD_SAMPLE_SIZE;
	uint32_t state_offset = lba_index * SECTOR_STATE_SIZE - drive_read_offset;

	std::string offset_str;
	for(uint32_t i = 0; i < CD_DATA_SIZE; ++i)
	{
		uint32_t byte_offset = i / CHAR_BIT;
		uint32_t bit_offset = ((CHAR_BIT - 1) - i % CHAR_BIT);

		if(c2_data[byte_offset] & (1 << bit_offset))
			offset_str += fmt::format("{:08X} ", scm_offset + i);
	}
	LOG("");
	LOG("C2 [LBA: {}, SCM: {:08X}, STATE: {:08X}]: {}", (int32_t)lba_index + lba_start, scm_offset, state_offset, offset_str);
}

}
