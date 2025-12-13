module;
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include "throw_line.hh"

export module filesystem.iso9660:browser;

import :defs;
import :entry;

import cd.cd;
import cd.cdrom;
import readers.data_reader;
import utils.strings;



export namespace gpsxre::iso9660
{

class Browser
{
public:
    static std::vector<uint8_t> readSystemArea(DataReader *data_reader)
    {
        std::vector<uint8_t> system_area(SYSTEM_AREA_SIZE * FORM1_DATA_SIZE);

        uint32_t sectors_count = data_reader->read(system_area.data(), data_reader->sectorsBase() + 0, SYSTEM_AREA_SIZE);
        system_area.resize(sectors_count * FORM1_DATA_SIZE);

        return system_area;
    }


    static bool findDescriptor(VolumeDescriptor &descriptor, DataReader *data_reader, VolumeDescriptorType type)
    {
        bool found = false;

        for(uint32_t s = SYSTEM_AREA_SIZE; data_reader->read((uint8_t *)&descriptor, data_reader->sectorsBase() + s, 1) == 1; ++s)
        {
            if(auto si(to_string_view(descriptor.standard_identifier)); si != DESCRIPTOR_ID_CD && si != DESCRIPTOR_ID_CDI)
                break;

            if(descriptor.type == type)
            {
                found = true;
                break;
            }
            else if(descriptor.type == VolumeDescriptorType::SET_TERMINATOR)
                break;
        }

        return found;
    }


    static std::shared_ptr<Entry> rootDirectory(DataReader *data_reader, const PrimaryVolumeDescriptor &pvd)
    {
        return std::make_shared<Entry>(data_reader, std::string(""), 1, pvd.root_directory_record);
    }


    static bool iterate(std::shared_ptr<Entry> entry, std::function<bool(std::string, std::shared_ptr<Entry>)> callback)
    {
        bool interrupted = false;

        std::queue<std::pair<std::string, std::shared_ptr<Entry>>> q;
        q.emplace(std::string(""), entry);

        while(!q.empty())
        {
            auto p = q.front();
            q.pop();

            if(p.second->isDirectory())
                for(auto dd : p.second->entries())
                    q.push(std::pair<std::string, std::shared_ptr<Entry>>(dd->isDirectory() ? (p.first.empty() ? "" : p.first + "/") + dd->name() : p.first, dd));
            else
            {
                if(callback(p.first, p.second))
                {
                    interrupted = true;
                    break;
                }
            }
        }

        return interrupted;
    }
};

}
