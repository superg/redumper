module;
#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660:entry;

import :defs;
import readers.data_reader;
import cd.cdrom;
import utils.endian;
import utils.misc;
import utils.strings;



export namespace gpsxre::iso9660
{

class Entry
{
public:
    Entry(DataReader *data_reader, const std::string &name, uint32_t version, const iso9660::DirectoryRecord &directory_record)
        : _dataReader(data_reader)
        , _name(name)
        , _version(version)
        , _directoryRecord(directory_record)
    {
        ;
    }


    const std::string &name() const
    {
        return _name;
    }


    uint32_t version() const
    {
        return _version;
    }


    time_t dateTime() const
    {
        return convert_time(_directoryRecord.recording_date_time);
    }


    uint32_t sectorsLBA() const
    {
        return _directoryRecord.offset.lsb;
    }


    uint32_t sectorsSize() const
    {
        return scale_up(_directoryRecord.data_length.lsb, FORM1_DATA_SIZE);
    }


    bool isDirectory() const
    {
        return _directoryRecord.file_flags & (uint8_t)iso9660::DirectoryRecord::FileFlags::DIRECTORY;
    }


    std::list<std::shared_ptr<Entry>> entries()
    {
        std::list<std::shared_ptr<Entry>> entries;

        if(isDirectory())
        {
            // read whole directory record to memory
            std::vector<uint8_t> directory_extent(read());

            auto directory_records = directory_extent_get_records(directory_extent);
            for(auto const &dr : directory_records)
            {
                // skip current and parent records
                if(dr.first == std::string(1, (char)iso9660::Characters::DIR_CURRENT) || dr.first == std::string(1, (char)iso9660::Characters::DIR_PARENT))
                    continue;

                uint32_t version;
                std::string name = split_identifier(version, dr.first);

                entries.push_back(std::make_shared<Entry>(_dataReader, name, version, dr.second));
            }
        }

        return entries;
    }


    std::shared_ptr<Entry> subEntry(const std::string &path)
    {
        std::shared_ptr<Entry> entry;

        auto components = tokenize(path, "/\\", nullptr);
        for(auto const &c : components)
        {
            uint32_t version;
            std::string name = str_uppercase(split_identifier(version, c));

            bool found = false;

            auto directories = entry ? entry->entries() : entries();
            for(auto &d : directories)
            {
                if(name == str_uppercase(d->name()) && (!version || version == d->version()))
                {
                    entry = d;
                    found = true;
                    break;
                }
            }

            if(!found)
            {
                entry.reset();
                break;
            }
        }

        return entry;
    }


    std::vector<uint8_t> read(bool form2 = false, bool *form_hint = nullptr)
    {
        std::vector<uint8_t> sectors(sectorsSize() * _dataReader->sectorSize(form2));

        uint32_t sectors_read = _dataReader->read(sectors.data(), sectorsLBA(), sectorsSize(), form2, form_hint);

        // exclude form2 sectors as multiples of form1 size
        uint32_t size = form2 ? sectors_read * _dataReader->sectorSize(form2) : _directoryRecord.data_length.lsb - ((sectorsSize() - sectors_read) * _dataReader->sectorSize(form2));

        sectors.resize(size);

        return sectors;
    }

private:
    DataReader *_dataReader;
    std::string _name;
    uint32_t _version;
    iso9660::DirectoryRecord _directoryRecord;
};

}
