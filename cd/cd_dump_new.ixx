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
const uint32_t SUBCODE_BYTE_DESYNC_COUNT = 5;


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


void check_fix_byte_desync(Context &ctx, uint32_t &errors_q_counter, int32_t lba, std::span<const uint8_t> sector_subcode)
{
	if(subcode_extract_q(sector_subcode.data()).isValid())
	{
		errors_q_counter = 0;
	}
	else
	{
		++errors_q_counter;

		// PLEXTOR: some drives byte desync on subchannel after mass C2 errors with high bit count on high speed
		// prevent this by flushing drive cache after C2 error range (flush cache on 5 consecutive Q errors)
		if(errors_q_counter > SUBCODE_BYTE_DESYNC_COUNT)
		{
			cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);
			errors_q_counter = 0;
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


uint32_t refine_count_sectors(std::fstream &fs_state, std::fstream &fs_subcode, int32_t lba_start, int32_t lba_end, int32_t offset, int32_t data_offset, const Options &options)
{
	uint32_t sectors_count = 0;

	std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);
	std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);

	for(int32_t lba = lba_start; lba < lba_end; ++lba)
	{
		int32_t lba_index = lba - LBA_START;

		read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, offset, (uint8_t)State::ERROR_SKIP);
		if(!sector_data_complete(sector_state))
		{
			++sectors_count;
			continue;
		}

		read_entry(fs_state, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, data_offset, (uint8_t)State::ERROR_SKIP);
		if(!sector_data_complete(sector_state))
		{
			++sectors_count;
			continue;
		}

		read_entry(fs_subcode, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
		if(options.refine_subchannel && !subcode_extract_q(sector_subcode.data()).isValid())
			++sectors_count;
	}
	
	return sectors_count;
}


void refine_init_errors(Errors &errors, std::fstream &fs_state, std::fstream &fs_subcode, int32_t lba_start, int32_t lba_end, int32_t offset, int32_t data_offset)
{
	std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);
	std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);

	uint32_t sample_start = sample_offset_r2a(lba_to_sample(lba_start, -std::max(offset, data_offset)));
	uint32_t sample_end = sample_offset_r2a(lba_to_sample(lba_end, -std::min(offset, data_offset)));

	for(uint32_t i = sample_start; i < sample_end;)
	{
		uint32_t size = std::min(CD_DATA_SIZE_SAMPLES, sample_end - i);
		std::span ss(sector_state.begin(), sector_state.begin() + size);

		read_entry(fs_state, (uint8_t *)ss.data(), sizeof(State), i, size, 0, (uint8_t)State::ERROR_SKIP);

		errors.scsi += std::count(ss.begin(), ss.end(), State::ERROR_SKIP);
		errors.c2 += std::count(ss.begin(), ss.end(), State::ERROR_C2);

		i += size;
	}

	for(int32_t lba = lba_start; lba < lba_end; ++lba)
	{
		int32_t lba_index = lba - LBA_START;

		read_entry(fs_subcode, sector_subcode.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);
		if(!subcode_extract_q(sector_subcode.data()).isValid())
			++errors.q;
	}
}


export bool redumper_refine_cd_new(Context &ctx, const Options &options, DumpMode dump_mode)
{
	image_check_empty(options);

	if(dump_mode == DumpMode::DUMP)
	{
		image_check_overwrite(options);

		if(!options.image_path.empty())
			std::filesystem::create_directories(options.image_path);
	}

	auto toc = toc_process(ctx, options, dump_mode == DumpMode::DUMP);
	if(dump_mode == DumpMode::DUMP)
	{
		LOG("disc TOC:");
		print_toc(toc);
		LOG("");
	}

	toc_fix_faketoc(toc);

	int32_t lba_start = options.lba_start ? *options.lba_start : ctx.drive_config.pregap_start;
	int32_t lba_end = options.lba_end ? *options.lba_end : toc.sessions.back().tracks.back().lba_end;

	auto gaps = toc_get_gaps(toc, ctx.drive_config.pregap_start);

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
	std::fstream::openmode mode = std::fstream::out | std::fstream::binary | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in);
	std::fstream fs_scram(image_prefix + ".scram", mode);
	std::fstream fs_state(image_prefix + ".state", mode);
	std::fstream fs_subcode(image_prefix + ".subcode", mode);
	
	std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
	std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
	std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);
	std::span<const uint8_t> sector_subcode(sector_buffer.begin() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

	std::vector<uint8_t> sector_data_file_a(CD_DATA_SIZE);
	std::vector<uint8_t> sector_data_file_d(CD_DATA_SIZE);
	std::vector<State> sector_state_file_a(CD_DATA_SIZE_SAMPLES);
	std::vector<State> sector_state_file_d(CD_DATA_SIZE_SAMPLES);
	std::vector<uint8_t> sector_subcode_file(CD_SUBCODE_SIZE);

	int32_t data_offset = ctx.drive_config.read_offset;
	if(options.dump_write_offset)
		data_offset = -*options.dump_write_offset;
	else if(dump_mode != DumpMode::DUMP)
	{
		auto disc_offset = find_disc_offset(toc, fs_state, fs_scram);
		if(disc_offset)
		{
			data_offset = -*disc_offset;
			LOG("disc write offset: {:+}", *disc_offset);
			LOG("");
		}
	}

	Errors errors_initial = {};
	if(dump_mode != DumpMode::DUMP)
		refine_init_errors(errors_initial, fs_state, fs_subcode, lba_start, lba_end, ctx.drive_config.read_offset, data_offset);
	Errors errors = errors_initial;

	uint32_t refine_sectors_processed = 0;
	uint32_t refine_sectors_count = 0;
	if(dump_mode == DumpMode::REFINE)
		refine_sectors_count = refine_count_sectors(fs_state, fs_subcode, lba_start, lba_end, ctx.drive_config.read_offset, data_offset, options);

	int32_t subcode_shift = 0;
	uint32_t errors_q_counter = 0;

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
		read_entry(fs_state, (uint8_t *)sector_state_file_a.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, ctx.drive_config.read_offset, (uint8_t)State::ERROR_SKIP);
		read_entry(fs_state, (uint8_t *)sector_state_file_d.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, data_offset, (uint8_t)State::ERROR_SKIP);
		read_entry(fs_subcode, sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);

		if(sector_data_complete(sector_state_file_a) && sector_data_complete(sector_state_file_d) && (!options.refine_subchannel || subcode_extract_q(sector_subcode_file.data()).isValid()))
			continue;

		uint32_t retries = dump_mode == DumpMode::REFINE ? options.retries : 0;

		std::optional<bool> read_as_data;
		for(uint32_t r = 0; r <= retries; ++r)
		{
			if(signal.interrupt())
			{
				break;
			}

			std::string status_message;
			if(r)
			{
				std::string data_message;
				if(read_as_data)
				{
					std::span<State> sector_state_file(*read_as_data ? sector_state_file_d : sector_state_file_a);
					uint32_t samples_good = std::count(sector_state_file.begin(), sector_state_file.end(), State::SUCCESS);
					data_message = std::format(", data: {:3}%", 100 * samples_good / CD_DATA_SIZE_SAMPLES);
				}

				status_message = std::format(", retry: {}{}", r, data_message);

				// flush cache
				cmd_read(*ctx.sptd, nullptr, 0, lba, 0, true);
			}

			uint32_t percentage;
			if(dump_mode == DumpMode::REFINE)
				percentage = 100 * (refine_sectors_processed * (options.retries + 1) + r) / (refine_sectors_count * (options.retries + 1));
			else
				percentage = 100 * (lba - lba_start) / (lba_overread - lba_start);
			LOGC_RF("{} [{:3}%] LBA: {:6}/{}, errors: {{ SCSI{}: {}, C2{}: {}, Q: {} }}{}", spinner_animation(), percentage, lba, lba_overread,
					dump_mode == DumpMode::DUMP ? "" : "s", errors.scsi, dump_mode == DumpMode::DUMP ? "" : "s", errors.c2, errors.q, status_message);

			auto read_time_start = std::chrono::high_resolution_clock::now();
			bool all_types = options.force_unscrambled;
			auto status = read_sector_new(*ctx.sptd, sector_buffer.data(), all_types, ctx.drive_config, lba);
			auto read_time_stop = std::chrono::high_resolution_clock::now();

			auto gap_range = inside_range(lba, gaps);
			bool slow_sector = std::chrono::duration_cast<std::chrono::seconds>(read_time_stop - read_time_start).count() > SLOW_SECTOR_TIMEOUT;
			if(gap_range != nullptr && slow_sector)
			{
				if(dump_mode == DumpMode::REFINE)
					refine_sectors_processed += refine_count_sectors(fs_state, fs_subcode, lba + 1, gap_range->second, ctx.drive_config.read_offset, data_offset, options);
				lba = gap_range->second - 1;
				break;
			}

			if(status.status_code)
			{
				if(gap_range == nullptr && lba < lba_end)
				{
					if(dump_mode != DumpMode::REFINE)
						++errors.scsi;

					if(options.verbose)
						LOG_R("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
				}
			}
			else
			{
				// grow lead-out overread if we still can read
				if(lba + 1 == lba_overread && !slow_sector && !options.lba_end && (lba_overread - lba_end < LEADOUT_OVERREAD_COUNT || options.overread_leadout))
					++lba_overread;

				check_subcode_shift(subcode_shift, lba, sector_subcode, options);
				check_fix_byte_desync(ctx, errors_q_counter, lba, sector_subcode);

				if(sector_subcode_update(sector_subcode_file, sector_subcode))
				{
					write_entry(fs_subcode, sector_subcode_file.data(), CD_SUBCODE_SIZE, lba_index, 1, 0);

					bool subcode_valid = subcode_extract_q(sector_subcode_file.data()).isValid();

					if(dump_mode == DumpMode::REFINE)
					{
						if(subcode_valid && lba < lba_end)
							--errors.q;
					}
					else
					{
						if(!subcode_valid)
							++errors.q;
					}
				}

				if(!read_as_data)
					read_as_data = all_types;

				if(*read_as_data == all_types)
				{
					uint32_t c2_bits = c2_bits_count(sector_c2);
					if(c2_bits)
					{
						if(dump_mode != DumpMode::REFINE)
							++errors.c2;

						if(options.verbose)
							LOG_R("[LBA: {:6}] C2 error (bits: {:4})", lba, c2_bits);
					}

					std::span<State> sector_state_file(all_types ? sector_state_file_d : sector_state_file_a);
					std::span<uint8_t> sector_data_file(all_types ? sector_data_file_d : sector_data_file_a);

					uint32_t scsi_before = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_SKIP);
					uint32_t c2_before = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_C2);

					if(sector_data_state_update(sector_state_file, sector_data_file, c2_to_state(sector_c2.data()), sector_data))
					{
						int32_t offset = all_types ? data_offset : ctx.drive_config.read_offset;
						write_entry(fs_scram, sector_data_file.data(), CD_DATA_SIZE, lba_index, 1, offset * CD_SAMPLE_SIZE);
						write_entry(fs_state, (uint8_t *)sector_state_file.data(), CD_DATA_SIZE_SAMPLES, lba_index, 1, offset);

						if(dump_mode == DumpMode::REFINE && lba < lba_end)
						{
							uint32_t scsi_after = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_SKIP);
							uint32_t c2_after = std::count(sector_state_file.begin(), sector_state_file.end(), State::ERROR_C2);

							errors.scsi -= scsi_before - scsi_after;
							errors.c2 -= c2_before - c2_after;
						}
					}

					if(sector_data_complete(sector_state_file) && (!options.refine_subchannel || subcode_extract_q(sector_subcode_file.data()).isValid()))
					{
						if(dump_mode == DumpMode::REFINE && options.verbose && lba < lba_end)
							LOG_R("[LBA: {:6}] correction success", lba);

						break;
					}
				}
				else
				{
					LOG_R("[LBA: {:6}] unexpected read type on retry (retry: {}, read type: {})", lba, r, all_types ? "DATA" : "AUDIO");
				}
			}

			if(dump_mode == DumpMode::REFINE && r == options.retries && options.verbose && lba < lba_end)
			{
				bool failure = false;

				std::string data_message;
				if(read_as_data)
				{
					std::span<State> sector_state_file(*read_as_data ? sector_state_file_d : sector_state_file_a);
					uint32_t samples_good = std::count(sector_state_file.begin(), sector_state_file.end(), State::SUCCESS);
					if(samples_good != CD_DATA_SIZE_SAMPLES)
					{
						data_message = std::format(", data: {:3}%", 100 * samples_good / CD_DATA_SIZE_SAMPLES);
						failure = true;
					}
					else if(options.refine_subchannel && !subcode_extract_q(sector_subcode_file.data()).isValid())
						failure = true;
				}
				else
					failure = true;

				if(failure)
					LOG_R("[LBA: {:6}] correction failure{}", lba, data_message);
			}
		}

		if(dump_mode == DumpMode::REFINE && lba < lba_end)
			++refine_sectors_processed;
	}

	if(!signal.interrupt())
	{
		LOGC_RF("");
		LOGC("");
	}

	if(dump_mode == DumpMode::DUMP)
	{
		LOG("media errors: ");
		LOG("  SCSI: {}", errors.scsi);
		LOG("  C2: {}", errors.c2);
		LOG("  Q: {}", errors.q);
	}
	else if(dump_mode == DumpMode::REFINE)
	{
		LOG("correction statistics: ");
		LOG("  SCSI: {} samples", errors_initial.scsi - errors.scsi);
		LOG("  C2: {} samples", errors_initial.c2 - errors.c2);
		LOG("  Q: {} sectors", errors_initial.q - errors.q);
	}

	if(signal.interrupt())
		signal.raiseDefault();

	return errors.scsi || errors.c2;
}

}
