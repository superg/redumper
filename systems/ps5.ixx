module;
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps5;

import filesystem.iso9660;
import readers.sector_reader;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemPS5 : public System
{
public:
	std::string getName() override
	{
		return "PS5";
	}


	Type getType() override
	{
		return Type::ISO;
	}


	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override
	{
		iso9660::PrimaryVolumeDescriptor pvd;
		if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, sector_reader, iso9660::VolumeDescriptorType::PRIMARY))
			return;
		auto root_directory = iso9660::Browser::rootDirectory(sector_reader, pvd);

		auto param_json = loadJSON(root_directory, "bd/param.json");

		auto it = param_json.find("masterVersion");
		if(it != param_json.end())
			os << std::format("  version: {}", it->second) << std::endl;

		it = param_json.find("masterDataId");
		if(it != param_json.end())
			os << std::format("  serial: {}", it->second) << std::endl;
	}

private:
	std::map<std::string, std::string> loadJSON(std::shared_ptr<iso9660::Entry> root_directory, std::string json_file) const
	{
		std::map<std::string, std::string> json;

		auto json_entry = root_directory->subEntry(json_file);
		if(json_entry)
		{
			auto data = json_entry->read();
			if(data.size() <= 0x800)
				return json;
			size_t cur = 0x800;
			size_t keyStart;
			size_t keyEnd;
			size_t valueStart;
			size_t valueEnd;
			std::string key;
			std::string value;
			while(cur < data.size())
			{
				// Look for start of key
				while(cur < data.size())
				{
					if(data[cur] == '"')
						break;
					++cur;
				}
				if (cur >= data.size())
					break;
				++cur;
				keyStart = cur;

				// Look for end of key
				while (cur < data.size())
				{
					if (data[cur] == '"')
						break;
					++cur;
				}
				if (cur >= data.size())
					break;
				keyEnd = cur;
				++cur;

				// Look for start of value
				while (cur < data.size())
				{
					if(data[cur] == ':')
						break;
					++cur;
				}
				if (cur >= data.size())
					break;
				++cur;
				valueStart = cur;

				// Look for end of value
				while (cur < data.size())
				{
					if (data[cur] == '[' || data[cur] == ',' || data[cur] == '}')
						break;
					++cur;
				}
				if (cur >= data.size())
					break;
				valueEnd = cur;
				++cur;

				// Don't save arrays, treat JSON as flat
				if (data[cur - 1] == '[')
					continue;

				key.assign(reinterpret_cast<char*>(&data[keyStart]), keyEnd - keyStart);
				value.assign(reinterpret_cast<char*>(&data[valueStart]), valueEnd - valueStart);

				erase_all_inplace(key, '\0');
				erase_all_inplace(key, '\r');
				erase_all_inplace(key, '\n');
				erase_all_inplace(value, '\0');
				erase_all_inplace(value, '\r');
				erase_all_inplace(value, '\n');

				// Remove leading/trailing quotes if present
				trim_inplace(value);
				if(value.size() >= 2 && value[0] == '"')
					value = value.substr(1, value.size() - 2);

				json.emplace(trim(key), trim(value));
			}
		}

		return json;
	}
};

}
