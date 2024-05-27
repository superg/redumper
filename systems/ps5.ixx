module;
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
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

        auto param_json = loadJSON(root_directory->subEntry("bd/param.json"));

        if(auto it = param_json.find("masterVersion"); it != param_json.end())
            os << std::format("  version: {}", it->second) << std::endl;

        if(auto it = param_json.find("masterDataId"); it != param_json.end())
            os << std::format("  serial: {}", it->second.insert(4, "-")) << std::endl;
    }

private:
    std::map<std::string, std::string> loadJSON(std::shared_ptr<iso9660::Entry> json_entry) const
    {
        std::map<std::string, std::string> json;

        if(!json_entry)
            return json;

        const uint32_t payload_skip = 0x800;
        auto data = json_entry->read();
        if(data.size() <= payload_skip)
            return json;

        auto json_raw = std::span<uint8_t>(data.begin() + payload_skip, data.end());

        // Parse JSON into key/value pairs
        for(size_t cur = 0; cur < json_raw.size();)
        {
            // Find start of key
            while(cur < json_raw.size())
            {
                if(json_raw[cur] == '"')
                    break;
                ++cur;
            }
            if(cur >= json_raw.size())
                break;
            ++cur;
            size_t keyStart = cur;

            // Find end of key
            while(cur < json_raw.size())
            {
                if(json_raw[cur] == '"')
                    break;
                ++cur;
            }
            if(cur >= json_raw.size())
                break;
            size_t keyEnd = cur;
            ++cur;

            // Find start of value
            while(cur < json_raw.size())
            {
                if(json_raw[cur] == ':')
                    break;
                ++cur;
            }
            if(cur >= json_raw.size())
                break;
            ++cur;
            size_t valueStart = cur;

            // Find end of value
            while(cur < json_raw.size())
            {
                if(json_raw[cur] == '[' || json_raw[cur] == ',' || json_raw[cur] == '}')
                    break;
                ++cur;
            }
            if(cur >= json_raw.size())
                break;
            size_t valueEnd = cur;
            ++cur;

            // Don't parse arrays, treat JSON as flat
            if(json_raw[cur - 1] == '[')
                continue;

            // Assign key and value
            std::string key(json_raw.begin() + keyStart, json_raw.begin() + keyEnd);
            std::string value(json_raw.begin() + valueStart, json_raw.begin() + valueEnd);
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
                value = std::string_view(value.data() + 1, value.size() - 2);

            // Add key/value pair to map
            json.emplace(key, value);
        }

        return json;
    }
};

}
