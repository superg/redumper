module;

#include <filesystem>
#include <fstream>
#include <list>
#include <string>
#include <utility>
#include <sstream>
#include "throw_line.hh"

export module info;

import cd.cdrom;
import dump;
import filesystem.iso9660;
import options;
import readers.form1_reader;
import systems.system;
import utils.hex_bin;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

enum class TrackType
{
	DATA,
	AUDIO,
	ISO
};


std::list<std::pair<std::string, bool>> cue_get_entries(const std::filesystem::path &cue_path)
{
	std::list<std::pair<std::string, bool>> entries;

	std::fstream fs(cue_path, std::fstream::in);
	if(!fs.is_open())
		throw_line("unable to open file ({})", cue_path.filename().string());

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

/*
//TODO: rewrite after image browser rework
void iso_info(const std::filesystem::path &track_path)
{
	std::fstream fs(track_path, std::fstream::in | std::fstream::binary);
	if(!fs.is_open())
		throw_line("unable to open file ({})", track_path.filename().string());

	bool pvd_found = false;
	for(uint32_t lba = iso9660::SYSTEM_AREA_SIZE; ; ++lba)
	{
		std::vector<uint8_t> sector(FORM1_DATA_SIZE);
		fs.seekg(lba * FORM1_DATA_SIZE);
		if(fs.fail())
			throw_line("seek failed");
		fs.read((char *)sector.data(), FORM1_DATA_SIZE);
		if(fs.fail())
			throw_line("read failed");

		auto vd = (iso9660::VolumeDescriptor *)sector.data();
		if(memcmp(vd->standard_identifier, iso9660::STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)))
			break;

		if(vd->type == iso9660::VolumeDescriptor::Type::PRIMARY)
		{
			auto pvd = (iso9660::VolumeDescriptor *)vd;

			LOG("ISO9660 [{}]:", track_path.filename().string());

			auto volume_identifier = trim(pvd->primary.volume_identifier);
			if(!volume_identifier.empty())
				LOG("  volume identifier: {}", volume_identifier);
			LOG("  PVD:");
			LOG_F("{}", hexdump((uint8_t *)pvd, 0x320, 96));

			pvd_found = true;
			break;
		}
		else if(vd->type == iso9660::VolumeDescriptor::Type::SET_TERMINATOR)
			break;
	}

	if(!pvd_found)
		throw_line("PVD not found");
}
*/

export void redumper_info(Options &options)
{
	if(options.image_name.empty())
		throw_line("image name is not provided");

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	std::list<std::pair<std::filesystem::path, TrackType>> tracks;
	if(std::filesystem::exists(image_prefix + ".cue"))
	{
		for(auto const &t : cue_get_entries(image_prefix + ".cue"))
			tracks.emplace_back(std::filesystem::path(options.image_path) / t.first, t.second ? TrackType::DATA : TrackType::AUDIO);
	}
	else if(std::filesystem::exists(image_prefix + ".iso"))
	{
		tracks.emplace_back(image_prefix + ".iso", TrackType::ISO);
	}

	bool separate_nl = false;
	for(auto const &t : tracks)
	{
		std::shared_ptr<Form1Reader> raw_reader;
//		if(t.second != TrackType::ISO)
//			raw_reader = std::make_shared<>();

		for(auto const &s : Systems::get())
		{
			auto system = s.second(t.first);

			std::stringstream ss;
			system->printInfo(ss);

			if(ss.rdbuf()->in_avail())
			{
				if(separate_nl)
					LOG("");
				separate_nl = true;

				LOG("{} [{}]:", s.first, t.first.filename().string());
				LOG_F("{}", ss.str());
			}
		}
	}

	LOG("");
/*
	if(std::filesystem::exists(image_prefix + ".cue"))
	{
		auto tracks = cue_get_entries(image_prefix + ".cue");

		bool separate_nl = false;
		for(auto const &t : tracks)
		{
			auto track_path = std::filesystem::path(options.image_path) / t.first;
			for(auto const &s : Systems::get())
			{
				auto system = s.second(track_path);

				std::stringstream ss;
				system->printInfo(ss);

				if(ss.rdbuf()->in_avail())
				{
					if(separate_nl)
						LOG("");
					separate_nl = true;

					LOG("{} [{}]:", s.first, track_path.filename().string());
					LOG_F("{}", ss.str());
				}
			}
		}
	}
	else
	{
		if(std::filesystem::exists(image_prefix + ".iso"))
		{
			iso_info(image_prefix + ".iso");
		}
		else
			throw_line("unable to detect input file");
	}
*/
}

}
