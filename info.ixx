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
import readers.sector_reader;
import readers.form1_reader;
import readers.image_bin_form1_reader;
import readers.image_iso_form1_reader;
import readers.image_raw_reader;
import readers.raw_reader;
import systems.systems;
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
		std::shared_ptr<SectorReader> raw_reader;
		std::shared_ptr<SectorReader> form1_reader;
		if(t.second == TrackType::ISO)
		{
			form1_reader = std::make_shared<Image_ISO_Form1Reader>(t.first);
		}
		else
		{
			raw_reader = std::make_shared<Image_RawReader>(t.first);

			if(t.second == TrackType::DATA)
				form1_reader = std::make_shared<Image_BIN_Form1Reader>(t.first);
		}

		for(auto const &s : Systems::get())
		{
			auto system = s();

			auto reader = system->getType() == System::Type::ISO ? form1_reader : raw_reader;
			if(!reader)
				continue;

			std::stringstream ss;
			//FIXME: pass image_prefix
			system->printInfo(ss, reader.get(), t.first);

			if(ss.rdbuf()->in_avail())
			{
				if(separate_nl)
					LOG("");
				separate_nl = true;

				LOG("{} [{}]:", system->getName(), t.first.filename().string());
				LOG_F("{}", ss.str());
			}
		}
	}
}

}
