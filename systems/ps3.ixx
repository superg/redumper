module;
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps3;

import filesystem.iso9660;
import readers.sector_reader;
import utils.endian;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemPS3 : public System
{
public:
    std::string getName() override
    {
        return "PS3";
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

        // Parse SFB and SFO files, if present
        auto ps3_disc_sfb = loadSFB(root_directory, "PS3_DISC.SFB");
        auto param_sfo = loadSFO(root_directory, "PS3_GAME/PARAM.SFO");

        // Print version, prefer SFB over SFO
        auto it = ps3_disc_sfb.find("VERSION");
        if(it != ps3_disc_sfb.end())
            os << std::format("  version: {}", it->second) << std::endl;
        else
        {
            auto it = param_sfo.find("VERSION");
            if(it != param_sfo.end())
                os << std::format("  version: {}", it->second) << std::endl;
        }

        // Print serial, prefer SFB over SFO
        it = ps3_disc_sfb.find("TITLE_ID");
        if(it != ps3_disc_sfb.end())
            os << std::format("  serial: {}", it->second) << std::endl;
        else
        {
            it = param_sfo.find("TITLE_ID");
            if(it != param_sfo.end())
                os << std::format("  serial: {}", it->second.insert(4, "-")) << std::endl;
        }
    }

private:
    static constexpr std::array<char, 4> _SFB_MAGIC = { 0x2E, 0x53, 0x46, 0x42 };
    static constexpr std::array<char, 4> _SFO_MAGIC = { 0x00, 0x50, 0x53, 0x46 };

    struct SFBHeader
    {
        char magic[4];
        uint32_t version;
        char unknown[24];
        struct
        {
            char key[16];
            uint32_t offset;
            uint32_t length;
            char reserved[8];
        } field[15];
    };


    struct SFOHeader
    {
        char magic[4];
        uint32_t version;
        uint32_t key_table;
        uint32_t value_table;
        uint32_t param_count;
    };


    struct SFOParam
    {
        uint16_t key_offset;
        uint16_t value_format;
        uint32_t value_length;
        uint32_t value_max_len;
        uint32_t value_offset;
    };


    std::map<std::string, std::string> loadSFB(std::shared_ptr<iso9660::Entry> root_directory, std::string sfb_file) const
    {
        std::map<std::string, std::string> sfb;

        auto sfb_entry = root_directory->subEntry(sfb_file);
        if(sfb_entry)
        {
            auto sfb_raw = sfb_entry->read();
            if(sfb_raw.size() < sizeof(SFBHeader))
                return sfb;

            auto sfb_header = (SFBHeader *)sfb_raw.data();

            if(memcmp(sfb_header->magic, _SFB_MAGIC.data(), _SFB_MAGIC.size()))
                return sfb;

            for(int i = 0; i < 15; ++i)
            {
                std::string key(sfb_header->field[i].key, sizeof(sfb_header->field[i].key));
                erase_all_inplace(key, '\0');
                trim_inplace(key);
                if(key.empty())
                    return sfb;

                uint32_t offset = endian_swap(sfb_header->field[i].offset);
                uint32_t length = endian_swap(sfb_header->field[i].length);
                if(sfb_raw.size() < offset + length)
                    return sfb;

                std::string value(sfb_raw.begin() + offset, sfb_raw.begin() + offset + length);
                erase_all_inplace(value, '\0');

                sfb.emplace(key, value);
            }
        }

        return sfb;
    }


    std::map<std::string, std::string> loadSFO(std::shared_ptr<iso9660::Entry> root_directory, std::string sfo_file) const
    {
        std::map<std::string, std::string> sfo;

        auto sfo_entry = root_directory->subEntry(sfo_file);
        if(sfo_entry)
        {
            auto sfo_raw = sfo_entry->read();
            if(sfo_raw.size() < sizeof(SFOHeader))
                return sfo;

            auto sfo_header = (SFOHeader *)sfo_raw.data();

            if(memcmp(sfo_header->magic, _SFO_MAGIC.data(), _SFO_MAGIC.size()))
                return sfo;
            if(sfo_header->param_count > 255)
                return sfo;
            if(sfo_raw.size() < sizeof(SFOHeader) + sfo_header->param_count * sizeof(SFOParam))
                return sfo;

            for(int i = 0; i < sfo_header->param_count; ++i)
            {
                auto param = (SFOParam *)(sfo_raw.data() + sizeof(SFOHeader) + i * sizeof(SFOParam));

                uint32_t key_length;
                if(i == sfo_header->param_count - 1)
                    key_length = sfo_header->value_table - sfo_header->key_table - param->key_offset;
                else
                {
                    auto next_param = (SFOParam *)(sfo_raw.data() + sizeof(SFOHeader) + (i + 1) * sizeof(SFOParam));
                    key_length = next_param->key_offset - param->key_offset;
                }

                if(sfo_raw.size() < sfo_header->key_table + param->key_offset + key_length)
                    return sfo;

                std::string key(sfo_raw.begin() + sfo_header->key_table + param->key_offset, sfo_raw.begin() + sfo_header->key_table + param->key_offset + key_length);
                erase_all_inplace(key, '\0');
                trim_inplace(key);

                std::string value;
                if(param->value_format == 0x0404)
                {
                    if(sfo_raw.size() < sfo_header->value_table + param->value_offset + 4)
                        return sfo;

                    uint32_t value_num = *(uint32_t *)(sfo_raw.data() + sfo_header->value_table + param->value_offset);
                    value.assign(std::to_string(value_num));
                }
                else
                {
                    if(sfo_raw.size() < sfo_header->value_table + param->value_offset + param->value_length)
                        return sfo;
                    value.assign(sfo_raw.begin() + sfo_header->value_table + param->value_offset, sfo_raw.begin() + sfo_header->value_table + param->value_offset + param->value_length);
                }
                erase_all_inplace(value, '\0');

                sfo.emplace(key, value);
            }
        }

        return sfo;
    }
};

}
