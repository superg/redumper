module;
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include "throw_line.hh"

export module debug;

import cd.cd;
import cd.subcode;
import cd.toc;
import drive;
import dump;
import utils.file_io;
import options;
import scsi.sptd;
import utils.logger;



namespace gpsxre
{

export void debug_subchannel(Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::filesystem::path sub_path(image_prefix + ".subcode");

	uint32_t sectors_count = check_file(sub_path, CD_SUBCODE_SIZE);
	std::fstream sub_fs(sub_path, std::fstream::in | std::fstream::binary);
	if(!sub_fs.is_open())
		throw_line("unable to open file ({})", sub_path.filename().string());

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
			int32_t lbaq = BCDMSF_to_LBA(Q.mode1.a_msf);

			LOG("[LBA: {:6}, LBAQ: {:6}] {}", LBA_START + (int32_t)lba_index, lbaq, Q.Decode());
			empty = false;
		}
		else if(!empty)
		{
			LOG("...");
			empty = true;
		}
	}
}


export void debug(Options &options)
{
	std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
	std::filesystem::path state_path(image_prefix + ".state");
	std::filesystem::path cache_path(image_prefix + ".asus");
	std::filesystem::path toc_path(image_prefix + ".toc");
	std::filesystem::path cdtext_path(image_prefix + ".cdtext");
	std::filesystem::path cue_path(image_prefix + ".cue");

	/*
		// popcnt test
		if(1)
		{
			for(uint32_t i = 0; i < 0xffffffff; ++i)
			{
				uint32_t test = __popcnt(i);
				uint32_t test2 = bits_count(i);

				if(test != test2)
					LOG("{} <=> {}", test, test2);
			}
		}
	*/
	// CD-TEXT debug
	if(0)
	{
		std::vector<uint8_t> toc_buffer = read_vector(toc_path);
		TOC toc(toc_buffer, false);

		std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);
		toc.updateCDTEXT(cdtext_buffer);

		std::fstream fs(cue_path, std::fstream::out);
		if(!fs.is_open())
			throw_line("unable to create file ({})", cue_path.string());
		toc.printCUE(fs, options.image_name, 0);

		LOG("");
	}

	// LG/ASUS cache read
	if(0)
	{
		/*
		SPTD sptd(options.drive);
		auto drive_config = drive_init(sptd, DiscType::CD, options);

		auto cache = asus_cache_read(sptd, drive_config.type);
		*/
	}

	// LG/ASUS cache dump extract
	if(1)
	{
		auto drive_type = DriveConfig::Type::LG_ASU3;
		std::vector<uint8_t> cache = read_vector(cache_path);

		asus_cache_print_subq(cache, drive_type);

		//		auto asd = asus_cache_unroll(cache);
		//		auto asd = asus_cache_extract(cache, 128224, 0);
		auto asus_leadout_buffer = asus_cache_extract(cache, 292353, 100, drive_type);
		uint32_t entries_count = (uint32_t)asus_leadout_buffer.size() / CD_RAW_DATA_SIZE;

		LOG("entries count: {}", entries_count);

		std::ofstream ofs_data(image_prefix + ".asus.data", std::ofstream::binary);
		std::ofstream ofs_c2(image_prefix + ".asus.c2", std::ofstream::binary);
		std::ofstream ofs_sub(image_prefix + ".asus.sub", std::ofstream::binary);
		for(uint32_t i = 0; i < entries_count; ++i)
		{
			uint8_t *entry = &asus_leadout_buffer[CD_RAW_DATA_SIZE * i];

			ofs_data.write((char *)entry, CD_DATA_SIZE);
			ofs_c2.write((char *)entry + CD_DATA_SIZE, CD_C2_SIZE);
			ofs_sub.write((char *)entry + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);
		}
	}


	// convert old state file to new state file
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

}
