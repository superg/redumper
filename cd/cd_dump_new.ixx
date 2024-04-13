module;
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <span>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.dump_new;

import cd.cd;
import cd.subcode;
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
import utils.misc;
import utils.signal;



namespace gpsxre
{

const uint32_t SLOW_SECTOR_TIMEOUT = 5;
const uint32_t LEADOUT_OVERREAD_COUNT = 100;


void progress_output(int32_t lba, int32_t lba_start, int32_t lba_end, uint32_t errors_scsi, uint32_t errors_c2, uint32_t errors_q)
{
	std::string status = lba == lba_end ? "dump complete" :
		std::format("LBA: {:6}/{}, errors: {{ SCSI: {}, C2: {}, Q: {} }}", lba, lba_end, errors_scsi, errors_c2, errors_q);

	char animation = lba == lba_end ? '*' : spinner_animation();
	uint32_t percentage = (lba - lba_start) * 100 / (lba_end - lba_start);
	LOGC_RF("{} [{:3}%] {}", animation, percentage, status);
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


uint32_t c2_to_state(std::vector<State> &state, const uint8_t *c2_data)
{
	uint32_t c2_count = 0;

	// group 4 C2 consecutive errors into 1 state, this way it aligns to the drive offset
	// and covers the case where for 1 C2 bit there are 2 damaged sector bytes (scrambled data bytes, usually)
	for(uint32_t i = 0; i < CD_DATA_SIZE_SAMPLES; ++i)
	{
		uint8_t c2_quad = c2_data[i / 2];
		if(i % 2)
			c2_quad &= 0x0F;
		else
			c2_quad >>= 4;

		if(c2_quad)
		{
			state[i] = State::ERROR_C2;
			c2_count += std::popcount(c2_quad);
		}
	}

	return c2_count;
}


bool sector_data_complete(std::fstream &fs_state, int32_t index, int32_t offset)
{
	std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);

	read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, index, 1, offset, (uint8_t)State::ERROR_SKIP);

	return std::all_of(sector_state.begin(), sector_state.end(), [](State s){ return s == State::SUCCESS; });
}


bool sector_data_blank(std::fstream &fs_state, int32_t index, int32_t offset)
{
	std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);

	read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, index, 1, offset, (uint8_t)State::ERROR_SKIP);

	return std::all_of(sector_state.begin(), sector_state.end(), [](State s){ return s == State::ERROR_SKIP; });
}


bool sector_subcode_complete(std::fstream &fs_subcode, int32_t index)
{
	std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);

	read_entry(fs_subcode, sector_subcode.data(), CD_SUBCODE_SIZE, index, 1, 0, 0);
	ChannelQ Q;
	subcode_extract_channel((uint8_t *)&Q, sector_subcode.data(), Subchannel::Q);

	return Q.isValid();
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

	// multisession gaps
	std::vector<std::pair<int32_t, int32_t>> error_ranges;
	for(uint32_t i = 1; i < toc.sessions.size(); ++i)
		error_ranges.emplace_back(toc.sessions[i - 1].tracks.back().lba_end, toc.sessions[i].tracks.front().indices.front() + ctx.drive_config.pregap_start);

	std::fstream::openmode mode = std::fstream::out | std::fstream::binary | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in);
	std::fstream fs_scram(scram_path, mode);
	std::fstream fs_subcode(subcode_path, mode);
	std::fstream fs_state(state_path, mode);

	std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
	auto sector_data = sector_buffer.data() + 0;
	auto sector_c2 = sector_buffer.data() + CD_DATA_SIZE;
	auto sector_subcode = sector_buffer.data() + CD_DATA_SIZE + CD_C2_SIZE;
	std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);

	int32_t data_offset = ctx.drive_config.read_offset;
	if(options.dump_write_offset)
		data_offset = -*options.dump_write_offset;
	else
	{
		if(dump_mode != DumpMode::DUMP)
		{
			//TODO: detect scram offset
			;
		}
	}

//	uint32_t refine_counter = 0;
//	uint32_t refine_retries = options.retries ? options.retries : 1;

	uint32_t errors_scsi = 0;
	uint32_t errors_c2 = 0;
	uint32_t errors_q = 0;

	int32_t subcode_shift = 0;
	uint32_t errors_q_last = errors_q;

	SignalINT signal;

	int32_t lba_overread = lba_end;
	for(int32_t lba = lba_start; lba < lba_overread;)
	{
		if(signal.interrupt())
		{
			LOG_R("[LBA: {:6}] forced stop ", lba);
			LOG("");
			break;
		}

		int32_t lba_index = lba - LBA_START;

		bool skip = true;
		if(dump_mode == DumpMode::DUMP)
			skip = false;
		else if(dump_mode == DumpMode::REFINE)
		{
			skip = sector_data_complete(fs_state, lba_index, ctx.drive_config.read_offset)
				&& sector_data_complete(fs_state, lba_index, data_offset)
				&& (!options.refine_subchannel || sector_subcode_complete(fs_subcode, lba_index));
		}
		else if(dump_mode == DumpMode::VERIFY)
			skip = sector_data_blank(fs_state, lba_index, ctx.drive_config.read_offset) && sector_data_blank(fs_state, lba_index, data_offset);

		if(skip)
		{
			//FIXME: verify leadout improvement on a different drive
			++lba;

			continue;
		}

		progress_output(lba, lba_start, lba_overread, errors_scsi, errors_c2, errors_q);

		// read sector
		auto read_time_start = std::chrono::high_resolution_clock::now();
		auto [status, read_as_data] = read_sector_new(*ctx.sptd, sector_buffer.data(), ctx.drive_config, lba);
		auto read_time_stop = std::chrono::high_resolution_clock::now();

		auto error_range = inside_range(lba, error_ranges);
		bool slow_sector = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;

		// skip known erroneous areas (multisession gaps and protection rings)
		if(error_range != nullptr && slow_sector)
		{
			lba = error_range->second;
			continue;
		}

		if(status.status_code)
		{
			if(error_range == nullptr && lba < lba_end)
			{
				if(dump_mode == DumpMode::DUMP)
					++errors_scsi;

				if(options.verbose)
					LOG_R("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
			}
		}
		else
		{
			ChannelQ Q;
			subcode_extract_channel((uint8_t *)&Q, sector_subcode, Subchannel::Q);
			if(Q.isValid())
			{
				errors_q_last = errors_q;

				// desync diagnostics
				if(Q.adr == 1 && Q.mode1.tno)
				{
					int32_t lbaq = BCDMSF_to_LBA(Q.mode1.a_msf);

					int32_t shift = lbaq - lba;
					if(subcode_shift != shift)
					{
						subcode_shift = shift;

						if(options.verbose)
							LOG_R("[LBA: {:6}] subcode desync (shift: {:+})", lba, subcode_shift);
					}
				}
			}
			else
			{
				// PLEXTOR: some drives byte desync on subchannel after mass C2 errors with high bit count on high speed
				// prevent this by flushing drive cache after C2 error range (flush cache on 5 consecutive Q errors)
				if(errors_q - errors_q_last > 5)
				{
					cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);
					errors_q_last = errors_q;
				}

				if(dump_mode == DumpMode::DUMP)
					++errors_q;
			}

			auto c2_bits = c2_to_state(sector_state, sector_c2);
			if(c2_bits)
			{
				if(dump_mode == DumpMode::DUMP)
					++errors_c2;

				if(options.verbose)
				{
					LOG_R("[LBA: {:6}] C2 error (bits: {:4})", lba, c2_bits);
				}
			}

			if(dump_mode == DumpMode::DUMP)
			{
				int32_t offset = read_as_data ? data_offset : ctx.drive_config.read_offset;

				write_entry(fs_scram, sector_data, CD_DATA_SIZE, lba_index, 1, offset * CD_SAMPLE_SIZE);
				write_entry(fs_subcode, sector_subcode, CD_SUBCODE_SIZE, lba_index, 1, 0);
				write_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, offset);
			}
			else if(dump_mode == DumpMode::REFINE)
			{
				;
			}
			else if(dump_mode == DumpMode::VERIFY)
			{
				;
			}

			// grow lead-out overread if we still can read
			if(lba + 1 == lba_overread && !slow_sector &&
			   !options.lba_end && (lba_overread - lba_end <= LEADOUT_OVERREAD_COUNT || options.overread_leadout))
			{
				++lba_overread;
			}
		}

		++lba;
	}

	if(!signal.interrupt())
	{
		progress_output(lba_overread, lba_start, lba_overread, errors_scsi, errors_c2, errors_q);
		LOGC("");
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
