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

            data.erase(data.begin(), data.begin() + 0x800);

            // Parse JSON into key/value pairs
            size_t cur = 0;
            while(cur < data.size())
            {
                // Find start of key
                while(cur < data.size())
                {
                    if(data[cur] == '"')
                        break;
                    ++cur;
                }
                if(cur >= data.size())
                    break;
                ++cur;
                size_t keyStart = cur;

                // Find end of key
                while(cur < data.size())
                {
                    if(data[cur] == '"')
                        break;
                    ++cur;
                }
                if(cur >= data.size())
                    break;
                size_t keyEnd = cur;
                ++cur;

                // Find start of value
                while(cur < data.size())
                {
                    if(data[cur] == ':')
                        break;
                    ++cur;
                }
                if(cur >= data.size())
                    break;
                ++cur;
                size_t valueStart = cur;

                // Find end of value
                while(cur < data.size())
                {
                    if(data[cur] == '[' || data[cur] == ',' || data[cur] == '}')
                        break;
                    ++cur;
                }
                if(cur >= data.size())
                    break;
                size_t valueEnd = cur;
                ++cur;

                // Don't parse arrays, treat JSON as flat
                if(data[cur - 1] == '[')
                    continue;

                // Assign key and value
                std::string key(data.begin() + keyStart, data.begin() + keyEnd);
                std::string value(data.begin() + valueStart, data.begin() + valueEnd);
                erase_all_inplace(key, '\0');
                erase_all_inplace(key, '\r');
                erase_all_inplace(key, '\n');
                erase_all_inplace(value, '\0');
                erase_all_inplace(value, '\r');
                erase_all_inplace(value, '\n');
                trim_inplace(key);
                trim_inplace(value);

                // Remove leading/trailing quotes if present
                if(value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);

                // Add key/value pair to map
                json.emplace(key, value);
            }
        }

        return json;
    }
};

}
