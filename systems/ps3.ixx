module;
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
#include <string_view>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps3;

import filesystem.iso9660;
import readers.data_reader;
import utils.endian;
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


    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &, bool) const override
    {
        iso9660::PrimaryVolumeDescriptor pvd;
        if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
            return;
        auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);

        auto ps3_disc_sfb = loadSFB(root_directory->subEntry("PS3_DISC.SFB"));

        std::string serial;
        if(auto it = ps3_disc_sfb.find("TITLE_ID"); it != ps3_disc_sfb.end())
            serial = it->second;

        std::string version;
        if(auto it = ps3_disc_sfb.find("VERSION"); it != ps3_disc_sfb.end())
            version = it->second;

        // update missing info from SFO
        if(serial.empty() || version.empty())
        {
            auto sfo_entry = root_directory->subEntry("PS3_GAME/PARAM.SFO");
            if(sfo_entry)
            {
                auto sfo_raw = sfo_entry->read();
                auto param_sfo = parseSFO(sfo_raw);

                if(auto it = param_sfo.find("TITLE_ID"); it != param_sfo.end() && serial.empty())
                {
                    serial = it->second;
                    serial.insert(4, "-");
                }

                if(auto it = param_sfo.find("VERSION"); it != param_sfo.end() && version.empty())
                    version = it->second;
            }
        }

        if(!version.empty())
            os << std::format("  version: {}", version) << std::endl;

        if(!serial.empty())
            os << std::format("  serial: {}", serial) << std::endl;
    }

private:
    struct SFBHeader
    {
        uint8_t magic[4];
        uint32_t version;
        uint8_t reserved[24];
        struct Field
        {
            uint8_t key[16];
            uint32_t offset;
            uint32_t length;
            uint8_t reserved[8];
        } field[15];
    };

    struct SFOHeader
    {
        uint8_t magic[4];
        uint32_t version;
        uint32_t key_table;
        uint32_t value_table;
        uint32_t param_count;
    };

    struct SFOParam
    {
        uint16_t key_offset;
        enum class ValueFormat : uint16_t
        {
            UTF8_S = 0x0400,
            UTF8 = 0x0402,
            NUMERIC = 0x0404
        } value_format;
        uint32_t value_length;
        uint32_t value_max_len;
        uint32_t value_offset;
    };


    std::map<std::string, std::string> loadSFB(std::shared_ptr<iso9660::Entry> sfb_entry) const
    {
        std::map<std::string, std::string> sfb;

        if(!sfb_entry)
            return sfb;

        auto sfb_raw = sfb_entry->read();
        if(sfb_raw.size() < sizeof(SFBHeader))
            return sfb;

        auto sfb_header = (SFBHeader &)sfb_raw[0];
        if(std::string_view((char *)sfb_header.magic, sizeof(sfb_header.magic)) != ".SFB")
            return sfb;

        for(auto const &f : sfb_header.field)
        {
            std::string key(std::string((char *)f.key, sizeof(f.key)).c_str());
            if(key.empty())
                break;

            uint32_t offset = endian_swap(f.offset);
            uint32_t length = endian_swap(f.length);
            if(offset + length > sfb_raw.size())
                continue;

            std::string value(std::string((char *)&sfb_raw[offset], length).c_str());

            sfb.emplace(key, value);
        }

        return sfb;
    }

protected:
    std::map<std::string, std::string> parseSFO(std::span<uint8_t> sfo_raw) const
    {
        std::map<std::string, std::string> sfo;

        if(sfo_raw.size() < sizeof(SFOHeader))
            return sfo;

        auto sfo_header = (SFOHeader &)sfo_raw[0];
        if(std::string_view((char *)sfo_header.magic, sizeof(sfo_header.magic)) != std::string("\0PSF", 4))
            return sfo;

        if(sfo_header.param_count > 255 || sizeof(SFOHeader) + sfo_header.param_count * sizeof(SFOParam) > sfo_raw.size())
            return sfo;

        auto params = (SFOParam *)&sfo_raw[sizeof(SFOHeader)];
        for(uint32_t i = 0; i < sfo_header.param_count; ++i)
        {
            auto const &p = params[i];

            uint32_t key_length = (i + 1 == sfo_header.param_count ? sfo_header.value_table - sfo_header.key_table : params[i + 1].key_offset) - p.key_offset;

            bool numeric = p.value_format == SFOParam::ValueFormat::NUMERIC;
            uint32_t value_length = numeric ? sizeof(uint32_t) : p.value_length;

            if(sfo_header.key_table + p.key_offset + key_length > sfo_raw.size() || sfo_header.value_table + p.value_offset + value_length > sfo_raw.size())
                continue;

            std::string key(std::string((char *)&sfo_raw[sfo_header.key_table + p.key_offset], key_length).c_str());
            std::string value(
                numeric ? std::to_string((uint32_t &)sfo_raw[sfo_header.value_table + p.value_offset]) : std::string((char *)&sfo_raw[sfo_header.value_table + p.value_offset], value_length).c_str());

            sfo.emplace(key, value);
        }

        return sfo;
    }
};

}
