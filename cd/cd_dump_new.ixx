module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.dump_new;

import cd.cd;
import cd.toc;
import drive;
import dump;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;
import utils.signal;



namespace gpsxre
{

const uint32_t SLOW_SECTOR_TIMEOUT = 5;


void progress_output(int32_t lba, int32_t lba_start, int32_t lba_end, uint32_t errors_scsi, uint32_t errors_c2, uint32_t errors_q)
{
	uint32_t sectors_count = lba_end - lba_start;

	char animation = lba == lba_end ? '*' : spinner_animation();

	uint32_t percentage = (lba - lba_start) * 100 / (lba_end - lba_start);

	LOGC_RF("{} [{:3}%] LBA: {:6}/{}, errors: {{ SCSI: {}, C2: {}, Q: {} }}", animation, percentage,
			lba, lba_end, errors_scsi, errors_c2, errors_q);
}


TOC process_toc(Context &ctx, const Options &options, DumpMode dump_mode)
{
	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::string toc_path(image_prefix + ".toc");
	std::string fulltoc_path(image_prefix + ".fulltoc");
	std::string pma_path(image_prefix + ".pma");
	std::string atip_path(image_prefix + ".atip");
	std::string cdtext_path(image_prefix + ".cdtext");

	SPTD::Status status;

	std::vector<uint8_t> toc_buffer;
	status = cmd_read_toc(*ctx.sptd, toc_buffer, false, READ_TOC_Format::TOC, 1);
	if(status.status_code)
		throw_line("failed to read TOC, SCSI ({})", SPTD::StatusMessage(status));

	// optional
	std::vector<uint8_t> full_toc_buffer;
	status = cmd_read_toc(*ctx.sptd, full_toc_buffer, true, READ_TOC_Format::FULL_TOC, 1);
	if(status.status_code)
		LOG("warning: FULL_TOC is unavailable (no multisession information), SCSI ({})", SPTD::StatusMessage(status));

	auto toc = choose_toc(toc_buffer, full_toc_buffer);

	// store TOC information
	if(dump_mode == DumpMode::DUMP)
	{
		// TOC / FULL_TOC
		write_vector(toc_path, toc_buffer);
		if(full_toc_buffer.size() > sizeof(CMD_ParameterListHeader))
			write_vector(fulltoc_path, full_toc_buffer);

		// PMA
		std::vector<uint8_t> pma_buffer;
		status = cmd_read_toc(*ctx.sptd, pma_buffer, true, READ_TOC_Format::PMA, 0);
		if(!status.status_code && pma_buffer.size() > sizeof(CMD_ParameterListHeader))
			write_vector(pma_path, pma_buffer);

		// ATIP
		std::vector<uint8_t> atip_buffer;
		status = cmd_read_toc(*ctx.sptd, atip_buffer, true, READ_TOC_Format::ATIP, 0);
		if(!status.status_code && atip_buffer.size() > sizeof(CMD_ParameterListHeader))
			write_vector(atip_path, atip_buffer);

		// CD-TEXT
		bool read_cdtext = !options.disable_cdtext;
		// disable multisession CD-TEXT for certain drives that hang indefinitely
		if(toc.sessions.size() > 1 && ctx.drive_config.vendor_id == "PLEXTOR" && ctx.drive_config.product_id == "CD-R PX-W4824A")
			read_cdtext = false;
		std::vector<uint8_t> cd_text_buffer;
		if(read_cdtext)
		{
			status = cmd_read_toc(*ctx.sptd, cd_text_buffer, false, READ_TOC_Format::CD_TEXT, 0);
			if(status.status_code)
				LOG("warning: unable to read CD-TEXT, SCSI ({})", SPTD::StatusMessage(status));
		}
		else
			LOG("warning: CD-TEXT disabled");
		if(cd_text_buffer.size() > sizeof(CMD_ParameterListHeader))
			write_vector(cdtext_path, cd_text_buffer);
	}
	// compare disc / file TOC to make sure it's the same disc
	else
	{
		std::vector<uint8_t> toc_buffer_file = read_vector(toc_path);
		if(toc_buffer != toc_buffer_file)
			throw_line("disc / file TOC don't match, refining from a different disc?");
	}
	
	return toc;
}


export bool redumper_dump_cd_new(Context &ctx, const Options &options, DumpMode dump_mode)
{
	if(options.image_name.empty())
		throw_line("image name is not provided");

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	// don't use .replace_extension() as it messes up paths with dot
	std::string scram_path(image_prefix + ".scram");
	std::string subcode_path(image_prefix + ".subcode");
	std::string state_path(image_prefix + ".state");

	if(dump_mode == DumpMode::DUMP)
	{
		image_check_overwrite(state_path, options);

		if(!options.image_path.empty())
			std::filesystem::create_directories(options.image_path);
	}

	auto toc = process_toc(ctx, options, dump_mode);
	if(dump_mode == DumpMode::DUMP)
	{
		LOG("disc TOC:");
		print_toc(toc);
		LOG("");
	}

	// initialize dump range
	int32_t lba_start = options.lba_start ? *options.lba_start : ctx.drive_config.pregap_start;
	int32_t lba_end;
	if(options.lba_end)
		lba_end = *options.lba_end;
	else
	{
		lba_end = toc.sessions.back().tracks.back().lba_end;

		// fake TOC
		// [PSX] Breaker Pro
		if(lba_end < 0)
		{
			lba_end = MSF_to_LBA(MSF{ 74, 0, 0 }); // default: 74min / 650Mb
			LOG("warning: fake TOC detected, using default 74min disc size");
		}
	}

	auto layout = sector_order_layout(ctx.drive_config.sector_order);
	bool subcode = layout.subcode_offset != CD_RAW_DATA_SIZE;

	std::fstream::openmode mode = std::fstream::out | std::fstream::binary
		| (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in);
	std::fstream fs_scram(scram_path, mode);
	std::fstream fs_subcode;
	if(subcode)
		fs_subcode.open(subcode_path, mode);
	std::fstream fs_state(state_path, mode);

	// buffers
//	std::vector<uint8_t> sector_data(CD_DATA_SIZE);
//	std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);
//	std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);
	std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);

//	uint32_t refine_counter = 0;
//	uint32_t refine_retries = options.retries ? options.retries : 1;

	uint32_t errors_scsi = 0;
	uint32_t errors_c2 = 0;
	uint32_t errors_q = 0;

	SignalINT signal;

	int32_t lba_next = 0;
	int32_t lba_overread = lba_end;
	for(int32_t lba = lba_start; lba < lba_overread;)
	{
		bool increment = true;

		bool read = false;
		if(dump_mode == DumpMode::DUMP)
		{
			read = true;
		}
		else if(dump_mode == DumpMode::REFINE)
		{
//			read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);
//			read = std::count(file_state.begin(), file_state.end(), State::ERROR_SKIP);
		}
		else if(dump_mode == DumpMode::VERIFY)
		{
//			read_entry(fs_iso, (uint8_t *)file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0, 0);
//
//			read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);
//			read = true;
		}

		if(read)
		{
			progress_output(lba, lba_start, lba_overread, errors_scsi, errors_c2, errors_q);

			auto read_time_start = std::chrono::high_resolution_clock::now();
			auto status = read_sector(*ctx.sptd, sector_buffer.data(), ctx.drive_config, lba);
			auto read_time_stop = std::chrono::high_resolution_clock::now();
			bool slow = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;

			if(status.status_code)
			{/*
				if(options.verbose)
				{
					std::string status_retries;
					if(dump_mode == DumpMode::REFINE)
						status_retries = std::format(", retry: {}", refine_counter + 1);
					for(uint32_t i = 0; i < sectors_to_read; ++i)
						LOG_R("[sector: {}] SCSI error ({}{})", s + i, SPTD::StatusMessage(status), status_retries);
				}

				if(dump_mode == DumpMode::DUMP)
					errors_scsi += sectors_to_read;
				else if(dump_mode == DumpMode::REFINE)
				{
					++refine_counter;
					if(refine_counter < refine_retries)
						increment = false;
					else
					{
						if(options.verbose)
							for(uint32_t i = 0; i < sectors_to_read; ++i)
								LOG_R("[sector: {}] correction failure", s + i);

						refine_counter = 0;
					}
				}*/
			}
			else
			{
				if(dump_mode == DumpMode::DUMP)
				{
//					file_data.swap(drive_data);

//					write_entry(fs_iso, file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0);
//					std::fill(file_state.begin(), file_state.end(), State::SUCCESS);
//					write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);

				}
				else if(dump_mode == DumpMode::REFINE)
				{
/*
					for(uint32_t i = 0; i < sectors_to_read; ++i)
					{
						if(file_state[i] == State::SUCCESS)
							continue;

						if(options.verbose)
							LOG_R("[sector: {}] correction success", s + i);

						std::copy(drive_data.begin() + i * FORM1_DATA_SIZE, drive_data.begin() + (i + 1) * FORM1_DATA_SIZE, file_data.begin() + i * FORM1_DATA_SIZE);
						file_state[i] = State::SUCCESS;

						--errors_scsi;
					}

					refine_counter = 0;

					write_entry(fs_iso, file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0);
					write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);
*/
				}
				else if(dump_mode == DumpMode::VERIFY)
				{
/*
					bool update = false;

					for(uint32_t i = 0; i < sectors_to_read; ++i)
					{
						if(file_state[i] != State::SUCCESS)
							continue;

						if(!std::equal(file_data.begin() + i * FORM1_DATA_SIZE, file_data.begin() + (i + 1) * FORM1_DATA_SIZE, drive_data.begin() + i * FORM1_DATA_SIZE))
						{
							if(options.verbose)
								LOG_R("[sector: {}] data mismatch, sector state updated", s + i);

							file_state[i] = State::ERROR_SKIP;
							update = true;

							++errors_scsi;
						}
					}

					if(update)
						write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);
*/
				}
			}
		}

		if(signal.interrupt())
		{
			LOG_R("[LBA: {:6}] forced stop ", lba);
			break;
		}

		if(increment)
			++lba;
	}

	if(!signal.interrupt())
	{
		progress_output(lba_overread, lba_start, lba_overread, errors_scsi, errors_c2, errors_q);
		LOGC("");
	}

	LOG("media errors: ");
	LOG("  SCSI: {}", errors_scsi);
	LOG("  C2: {}", errors_c2);
	LOG("  Q: {}", errors_q);

	if(signal.interrupt())
		signal.raiseDefault();

	return errors_scsi || errors_c2;
}

}
