module;
#include <algorithm>
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

struct Errors
{
	uint32_t scsi;
	uint32_t c2;
	uint32_t q;
};


const uint32_t SLOW_SECTOR_TIMEOUT = 5;
const uint32_t LEADOUT_OVERREAD_COUNT = 100;


void progress_output(int32_t lba, int32_t lba_start, int32_t lba_end, const Errors &errors)
{
	std::string status = lba == lba_end ? "dump complete" :
		std::format("LBA: {:6}/{}, errors: {{ SCSI: {}, C2: {}, Q: {} }}", lba, lba_end, errors.scsi, errors.c2, errors.q);

	char animation = lba == lba_end ? '*' : spinner_animation();
	uint32_t percentage = (lba - lba_start) * 100 / (lba_end - lba_start);
	LOGC_RF("{} [{:3}%] {}", animation, percentage, status);
}


TOC toc_process(Context &ctx, const Options &options, bool store)
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
	if(store)
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


void toc_fix_faketoc(TOC &toc)
{
	// [PSX] Breaker Pro
	if(toc.sessions.back().tracks.back().lba_end < 0)
	{
		toc.sessions.back().tracks.back().lba_end = MSF_to_LBA(MSF{ 74, 0, 0 }); // default: 74min / 650Mb
		LOG("warning: fake TOC detected, using default 74min disc size");
	}
}


std::vector<std::pair<int32_t, int32_t>> toc_get_gaps(const TOC &toc, int32_t pregap_start)
{
	std::vector<std::pair<int32_t, int32_t>> gaps;

	for(uint32_t i = 1; i < toc.sessions.size(); ++i)
		gaps.emplace_back(toc.sessions[i - 1].tracks.back().lba_end, toc.sessions[i].tracks.front().indices.front() + pregap_start);

	return gaps;
}


std::vector<State> c2_to_state(const uint8_t *c2_data)
{
	std::vector<State> state(CD_DATA_SIZE_SAMPLES);

	// sample based granularity (4 bytes), if any C2 bit within 1 sample is set, mark whole sample as bad
	for(uint32_t i = 0; i < state.size(); ++i)
	{
		uint8_t c2_quad = c2_data[i / 2];
		if(i % 2)
			c2_quad &= 0x0F;
		else
			c2_quad >>= 4;

		state[i] = c2_quad ? State::ERROR_C2 : State::SUCCESS;
	}

	return state;
}


uint32_t c2_bits_count(std::span<const uint8_t> c2_data)
{
	return std::accumulate(c2_data.begin(), c2_data.end(), 0, [](uint32_t accumulator, uint8_t c2) { return accumulator + std::popcount(c2); });
}


bool sector_data_complete(std::span<const State> sector_state)
{
	return std::all_of(sector_state.begin(), sector_state.end(), [](State s) { return s == State::SUCCESS; });
}


bool sector_data_blank(std::span<const State> sector_state)
{
	return std::all_of(sector_state.begin(), sector_state.end(), [](State s) { return s == State::ERROR_SKIP; });
}


bool sector_data_state_update(std::span<State> sector_state, std::span<uint8_t> sector_data, std::span<const State> sector_state_in, std::span<const uint8_t> sector_data_in)
{
	bool updated = false;

	for(uint32_t i = 0; i < CD_DATA_SIZE_SAMPLES; ++i)
	{
		if(sector_state[i] < sector_state_in[i])
		{
			sector_state[i] = sector_state_in[i];
			((uint32_t *)sector_data.data())[i] = ((uint32_t *)sector_data_in.data())[i];

			updated = true;
		}
	}

	return updated;
}


bool sector_subcode_update(std::span<uint8_t> sector_subcode, std::span<const uint8_t> sector_subcode_in)
{
	bool updated = false;

	ChannelQ Q = subcode_extract_q(sector_subcode.data());
	ChannelQ Q_in = subcode_extract_q(sector_subcode_in.data());

	if(std::all_of((uint8_t *)&Q, (uint8_t *)&Q + sizeof(Q), [](uint8_t s) { return s == 0; }) || !Q.isValid() && Q_in.isValid())
	{
		std::copy(sector_subcode_in.begin(), sector_subcode_in.end(), sector_subcode.begin());

		updated = true;
	}

	return updated;
}


void check_subcode_shift(int32_t &subcode_shift, int32_t lba, std::span<const uint8_t> sector_subcode, const Options &options)
{
	ChannelQ Q = subcode_extract_q(sector_subcode.data());
	if(Q.isValid())
	{
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
}


void check_fix_byte_desync(Context &ctx, uint32_t &errors_q_last, uint32_t errors_q, int32_t lba, std::span<const uint8_t> sector_subcode)
{
	if(subcode_extract_q(sector_subcode.data()).isValid())
	{
		errors_q_last = errors_q;
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
	}
}


std::optional<int32_t> find_disc_offset(const TOC &toc, std::fstream &fs_state, std::fstream &fs_scram)
{
	for(auto &s : toc.sessions)
	{
		for(uint32_t i = 0; i + 1 < s.tracks.size(); ++i)
		{
			auto &t = s.tracks[i];
			auto &t_next = s.tracks[i + 1];

			if(t.control & (uint8_t)ChannelQ::Control::DATA)
			{
				auto track_offset = track_offset_by_sync(t.lba_start, t_next.lba_start, fs_state, fs_scram);
				if(track_offset)
					return track_offset;
			}
		}
	}

	return std::nullopt;
}


export bool redumper_dump_cd_new(Context &ctx, const Options &options)
{
	image_check_empty(options);
	image_check_overwrite(options);

	if(!options.image_path.empty())
		std::filesystem::create_directories(options.image_path);

	auto toc = toc_process(ctx, options, true);
	LOG("disc TOC:");
	print_toc(toc);
	LOG("");

	toc_fix_faketoc(toc);

	int32_t lba_start = options.lba_start ? *options.lba_start : ctx.drive_config.pregap_start;
	int32_t lba_end = options.lba_end ? *options.lba_end : toc.sessions.back().tracks.back().lba_end;

	auto gaps = toc_get_gaps(toc, ctx.drive_config.pregap_start);

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
	std::fstream::openmode mode = std::fstream::out | std::fstream::binary | std::fstream::trunc;
	std::fstream fs_scram(image_prefix + ".scram", mode);
	std::fstream fs_subcode(image_prefix + ".subcode", mode);
	std::fstream fs_state(image_prefix + ".state", mode);

	std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
	std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
	std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);
	std::span<const uint8_t> sector_subcode(sector_buffer.begin() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

	int32_t data_offset = ctx.drive_config.read_offset;
	if(options.dump_write_offset)
		data_offset = -*options.dump_write_offset;

	Errors errors = {};

	int32_t subcode_shift = 0;
	uint32_t errors_q_last = 0;

	SignalINT signal;

	int32_t lba_overread = lba_end;
	for(int32_t lba = lba_start; lba < lba_overread; ++lba)
	{
		if(signal.interrupt())
		{
			LOG_R("[LBA: {:6}] forced stop ", lba);
			LOG("");
			break;
		}

		progress_output(lba, lba_start, lba_overread, errors);

		auto read_time_start = std::chrono::high_resolution_clock::now();
		bool read_as_data;
		auto status = read_sector_new(*ctx.sptd, sector_buffer.data(), &read_as_data, ctx.drive_config, lba);
		auto read_time_stop = std::chrono::high_resolution_clock::now();

		auto error_range = inside_range(lba, gaps);
		bool slow_sector = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;

		if(error_range != nullptr && slow_sector)
		{
			lba = error_range->second - 1;
			continue;
		}

		if(status.status_code)
		{
			if(error_range == nullptr && lba < lba_end)
			{
				++errors.scsi;

				if(options.verbose)
					LOG_R("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
			}
		}
		else
		{
			check_subcode_shift(subcode_shift, lba, sector_subcode, options);
			check_fix_byte_desync(ctx, errors_q_last, errors.q, lba, sector_subcode);

			if(!subcode_extract_q(sector_subcode.data()).isValid())
				++errors.q;

			auto sector_state = c2_to_state(sector_c2.data());
			if(!sector_data_complete(sector_state))
			{
				++errors.c2;

				if(options.verbose)
					LOG_R("[LBA: {:6}] C2 error (bits: {:4})", lba, c2_bits_count(sector_c2));
			}

			int32_t lba_index = lba - LBA_START;
			int32_t offset = read_as_data ? data_offset : ctx.drive_config.read_offset;
			write_entry(fs_scram, sector_data.data(), CD_DATA_SIZE, lba_index, 1, offset * CD_SAMPLE_SIZE);
			write_entry(fs_subcode, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);
			write_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, offset);

			// grow lead-out overread if we still can read
			if(lba + 1 == lba_overread && !slow_sector &&
			   !options.lba_end && (lba_overread - lba_end <= LEADOUT_OVERREAD_COUNT || options.overread_leadout))
			{
				++lba_overread;
			}
		}
	}

	if(!signal.interrupt())
	{
		progress_output(lba_overread, lba_start, lba_overread, errors);
		LOGC("");
		LOGC("");
	}

	LOG("media errors: ");
	LOG("  SCSI: {}", errors.scsi);
	LOG("  C2: {}", errors.c2);
	LOG("  Q: {}", errors.q);

	if(signal.interrupt())
		signal.raiseDefault();

	return errors.scsi || errors.c2;
}


export void redumper_refine_cd_new(Context &ctx, const Options &options)
{
	image_check_empty(options);

	auto toc = toc_process(ctx, options, false);
	toc_fix_faketoc(toc);

	int32_t lba_start = options.lba_start ? *options.lba_start : ctx.drive_config.pregap_start;
	int32_t lba_end = options.lba_end ? *options.lba_end : toc.sessions.back().tracks.back().lba_end;

	auto gaps = toc_get_gaps(toc, ctx.drive_config.pregap_start);

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
	std::fstream::openmode mode = std::fstream::out | std::fstream::binary | std::fstream::in;
	std::fstream fs_scram(image_prefix + ".scram", mode);
	std::fstream fs_subcode(image_prefix + ".subcode", mode);
	std::fstream fs_state(image_prefix + ".state", mode);
	
	std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
	std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
	std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);
	std::span<const uint8_t> sector_subcode(sector_buffer.begin() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

	std::vector<uint8_t> sector_data_file_a(CD_DATA_SIZE);
	std::vector<uint8_t> sector_data_file_d(CD_DATA_SIZE);
	std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);
	std::vector<State> sector_state_file_a(CD_DATA_SIZE_SAMPLES);
	std::vector<State> sector_state_file_d(CD_DATA_SIZE_SAMPLES);

	int32_t data_offset = ctx.drive_config.read_offset;
	if(options.dump_write_offset)
		data_offset = -*options.dump_write_offset;
	else
	{
		auto disc_offset = find_disc_offset(toc, fs_state, fs_scram);
		if(disc_offset)
			data_offset = -*disc_offset;
	}

	Errors errors = {};

	SignalINT signal;

	int32_t lba_overread = lba_end;
	for(int32_t lba = lba_start; lba < lba_overread; ++lba)
	{
		if(signal.interrupt())
		{
			LOG_R("[LBA: {:6}] forced stop ", lba);
			LOG("");
			break;
		}

		int32_t lba_index = lba - LBA_START;

		read_entry(fs_scram, sector_data_file_a.data(), CD_DATA_SIZE, lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE, 0);
		read_entry(fs_scram, sector_data_file_d.data(), CD_DATA_SIZE, lba_index, 1, data_offset * CD_SAMPLE_SIZE, 0);
		read_entry(fs_subcode, sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
		read_entry(fs_state, (uint8_t *)sector_state_file_a.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
		read_entry(fs_state, (uint8_t *)sector_state_file_d.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, data_offset, (uint8_t)State::ERROR_SKIP);

		if(sector_data_complete(sector_state_file_a) && sector_data_complete(sector_state_file_d)
			&& (!options.refine_subchannel || subcode_extract_q(sector_subcode_file.data()).isValid()))
			continue;

		bool data = false;
		for(uint32_t r = 0, n = (options.retries ? options.retries : 1); r < n; ++r)
		{
			if(signal.interrupt())
			{
				break;
			}

			progress_output(lba, lba_start, lba_overread, errors);

			// flush cache
			if(r)
				cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);

			auto read_time_start = std::chrono::high_resolution_clock::now();
			bool read_as_data;
			auto status = read_sector_new(*ctx.sptd, sector_buffer.data(), &read_as_data, ctx.drive_config, lba);
			auto read_time_stop = std::chrono::high_resolution_clock::now();

			auto error_range = inside_range(lba, gaps);
			bool slow_sector = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;

			if(error_range != nullptr && slow_sector)
			{
				lba = error_range->second - 1;
				break;
			}

			if(status.status_code)
			{
				if(error_range == nullptr && lba < lba_end)
				{
					if(options.verbose)
						LOG_R("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
				}
			}
			else
			{
				if(r)
				{
					if(data != read_as_data)
					{
						LOG_R("[LBA: {:6}] unexpected read type on retry (retry: {}, read type: {})", lba, r + 1, read_as_data ? "DATA" : "AUDIO");
						continue;
					}
				}
				else
				{
					data = read_as_data;
				}

				int32_t offset = data ? data_offset : ctx.drive_config.read_offset;
				std::span<uint8_t> sector_data_file(data ? sector_data_file_d : sector_data_file_a);
				std::span<State> sector_state_file(data ? sector_state_file_d : sector_state_file_a);

				auto sector_state = c2_to_state(sector_c2.data());
				bool store = sector_data_state_update(sector_state_file, sector_data_file, sector_state, sector_data);
				if(store)
				{
					write_entry(fs_scram, sector_data_file.data(), CD_DATA_SIZE, lba_index, 1, offset * CD_SAMPLE_SIZE);
					write_entry(fs_state, (uint8_t *)sector_state_file.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, offset);
				}

				bool store_q = sector_subcode_update(sector_subcode_file, sector_subcode);
				if(store_q)
					write_entry(fs_subcode, sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);

				if(store || store_q)
				{
					if(sector_data_complete(sector_state_file) && (!options.refine_subchannel || subcode_extract_q(sector_subcode_file.data()).isValid()))
						break;
				}

				// grow lead-out overread if we still can read
				if(lba + 1 == lba_overread && !slow_sector &&
				   !options.lba_end && (lba_overread - lba_end <= LEADOUT_OVERREAD_COUNT || options.overread_leadout))
				{
					++lba_overread;
				}
			}
		}
	}

	if(!signal.interrupt())
	{
		progress_output(lba_overread, lba_start, lba_overread, errors);
		LOGC("");
		LOGC("");
	}

	LOG("media errors: ");
	LOG("  SCSI: {}", errors.scsi);
	LOG("  C2: {}", errors.c2);
	LOG("  Q: {}", errors.q);

	if(signal.interrupt())
		signal.raiseDefault();
}

}
