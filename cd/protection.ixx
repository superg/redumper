module;
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.protection;

import cd.cd;
import cd.subcode;
import cd.toc;
import dump;
import filesystem.iso9660;
import options;
import readers.image_bin_form1_reader;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export void redumper_protection(Context &ctx, Options &options)
{
	image_check_empty(options);

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

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
		throw_line("unable to open file ({})", scra_path.filename().string());

	std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
	if(!state_fs.is_open())
		throw_line("unable to open file ({})", state_path.filename().string());

	std::string protection("N/A");

	// SafeDisc
	// first track is data
	if(toc.sessions.size() == 1 && toc.sessions.front().tracks.size() >= 2)
	{
		auto &t = toc.sessions.front().tracks[0];
		auto &t_next = toc.sessions.front().tracks[1];

		if(t.control & (uint8_t)ChannelQ::Control::DATA)
		{
			auto write_offset = track_offset_by_sync(t.lba_start, t_next.lba_start, state_fs, scm_fs);
			if(write_offset)
			{
				uint32_t file_offset = (t.lba_start - LBA_START) * CD_DATA_SIZE + *write_offset * CD_SAMPLE_SIZE;
				auto form1_reader = std::make_unique<Image_BIN_Form1Reader>(scm_fs, file_offset, t_next.lba_start - t.lba_start, !scrap);

				iso9660::PrimaryVolumeDescriptor pvd;
				if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
				{
					auto root_directory = iso9660::Browser::rootDirectory(form1_reader.get(), pvd);
					auto entry = root_directory->subEntry("00000001.TMP");
					if(entry)
					{
						std::vector<State> state(CD_DATA_SIZE_SAMPLES);

						int32_t lba_start = entry->sectorsOffset() + entry->sectorsSize();
						int32_t lba_end = pvd.volume_space_size.lsb;
						auto entries = root_directory->entries();
						for(auto &e : entries)
						{
							if(e->isDirectory())
								continue;

							auto entry_offset = e->sectorsOffset();
							if(entry_offset <= lba_start)
								continue;

							if(entry_offset < lba_end)
								lba_end = entry_offset;
						}

						std::vector<int32_t> errors;

						for(int32_t lba = lba_start; lba < lba_end; ++lba)
						{
							read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -*write_offset, (uint8_t)State::ERROR_SKIP);

							if(std::any_of(state.begin(), state.end(), [](State s){ return s == State::ERROR_C2; }))
								errors.push_back(lba);
						}

						if(!errors.empty())
						{
							protection = std::format("SafeDisc {}, C2: {}, gap range: {}-{}", entry->name(), errors.size(), lba_start, lba_end - 1);

							for(auto e : errors)
								ctx.protection.emplace_back(lba_to_sample(e, *write_offset), lba_to_sample(e + 1, *write_offset));

							//FIXME: remove after switch to dumpnew code
							auto skip_ranges = string_to_ranges(options.skip);
							for(auto e : errors)
								skip_ranges.emplace_back(e, e + 1);
							options.skip = ranges_to_string(skip_ranges);
						}
					}
				}
			}
		}
	}


	// PS2 Datel
	// only one data track
	if(toc.sessions.size() == 1 && toc.sessions.front().tracks.size() == 2)
	{
		auto &t = toc.sessions.front().tracks[0];
		auto &t_next = toc.sessions.front().tracks[1];

		if(t.control & (uint8_t)ChannelQ::Control::DATA)
		{
			std::vector<State> state(CD_DATA_SIZE_SAMPLES);

			auto write_offset = track_offset_by_sync(t.lba_start, t_next.lba_start, state_fs, scm_fs);
			if(write_offset)
			{
				// preliminary check
				bool candidate = false;
				{
					constexpr int32_t lba_check = 50;
					if(lba_check >= t.lba_start && lba_check < t_next.lba_start)
					{
						read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_check - LBA_START, 1, -*write_offset, (uint8_t)State::ERROR_SKIP);
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
					constexpr int32_t first_file_offset = 23;

					std::string protected_filename;
					{
						uint32_t file_offset = (t.lba_start - LBA_START) * CD_DATA_SIZE + *write_offset * CD_SAMPLE_SIZE;
						auto form1_reader = std::make_unique<Image_BIN_Form1Reader>(scm_fs, file_offset, t_next.lba_start - t.lba_start, !scrap);

						iso9660::PrimaryVolumeDescriptor pvd;
						if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
						{
							auto root_directory = iso9660::Browser::rootDirectory(form1_reader.get(), pvd);

							static const std::string datel_files[] = { "DATA.DAT", "BIG.DAT", "DUMMY.ZIP" };
							for(auto const &f : datel_files)
							{
								// protection file exists
								auto entry = root_directory->subEntry(f);
								if(!entry)
									continue;

								// first file on disc and starts from LBA 23
								if(entry->sectorsOffset() == first_file_offset)
								{
									protected_filename = entry->name();
									break;
								}
							}
						}
					}

					if(!protected_filename.empty())
					{
						std::pair<int32_t, int32_t> range(0, 0);
						for(int32_t lba = first_file_offset, lba_end = std::min(t_next.lba_start, 5000); lba < lba_end; ++lba)
						{
							read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -*write_offset, (uint8_t)State::ERROR_SKIP);

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

							ctx.protection.emplace_back(lba_to_sample(range.first, *write_offset), lba_to_sample(range.second, *write_offset));

							//FIXME: remove after switch to dumpnew code
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
}

}
