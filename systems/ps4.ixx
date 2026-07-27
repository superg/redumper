module;
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps4;

import cd.cdrom;
import filesystem.iso9660;
import readers.data_reader;
import systems.ps3;
import utils.endian;



namespace gpsxre
{

export class SystemPS4 : public SystemPS3
{
public:
    std::string getName() override
    {
        return "PS4";
    }


    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &, bool) const override
    {
        iso9660::PrimaryVolumeDescriptor pvd;
        if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
            return;
        auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);

        auto sfo_entry = root_directory->subEntry("bd/param.sfo");
        if(!sfo_entry)
            return;

        const uint32_t payload_skip = 0x800;
        auto sfo_raw = sfo_entry->read();
        if(sfo_raw.size() < payload_skip)
            return;

        auto param_sfo = parseSFO(std::span<uint8_t>(sfo_raw.begin() + payload_skip, sfo_raw.end()));

        if(auto it = param_sfo.find("VERSION"); it != param_sfo.end())
            os << std::format("  version: {}", it->second) << std::endl;

        if(auto it = param_sfo.find("TITLE_ID"); it != param_sfo.end())
        {
            auto serial = it->second;
            serial.insert(4, "-");
            os << std::format("  serial: {}", serial) << std::endl;
        }

        std::string content_ids = getContentIds(data_reader, root_directory, _PKG_FILE_NAMES);

        if(content_ids.empty())
            return;

        os << std::format("  content ID(s): {}", content_ids) << std::endl;
    }

protected:
    std::string getContentIds(DataReader *data_reader, std::shared_ptr<iso9660::Entry> root_directory, std::span<const std::string> pkg_file_names) const
    {
        auto app_directory = root_directory->subEntry("app");
        if(!app_directory)
            return "";

        auto app_directory_entries = app_directory->entries();
        std::string content_ids;

        for(auto &e : app_directory_entries)
        {
            if(!e->isDirectory())
                continue;

            for(const auto &pkg_file_name : pkg_file_names)
            {
                auto app_pkg_entry = e->subEntry(pkg_file_name);
                if(!app_pkg_entry)
                    continue;

                std::vector<uint8_t> app_pkg_header(FORM1_DATA_SIZE);
                data_reader->read((uint8_t *)app_pkg_header.data(), app_pkg_entry->sectorsLBA(), 1);
                auto pkg_header = (PkgHeader *)app_pkg_header.data();
                pkg_header->swapEndianness();

                if(pkg_header->pkg_magic != _PKG_MAGIC)
                    continue;

                if(!content_ids.empty())
                    content_ids += ", ";

                content_ids += std::string(reinterpret_cast<const char *>(pkg_header->pkg_content_id), sizeof(pkg_header->pkg_content_id));

                break;
            }
        }

        return content_ids;
    }

private:
    static constexpr uint32_t _PKG_MAGIC = 0x7F434E54;
    static constexpr std::array<std::string, 3> _PKG_FILE_NAMES = { "app.pkg", "app_h.pkg", "app_0.pkg" };

    struct PkgHeader
    {
        uint32_t pkg_magic;
        uint32_t pkg_type;
        uint32_t pkg_0x008;
        uint32_t pkg_file_count;
        uint32_t pkg_entry_count;
        uint16_t pkg_sc_entry_count;
        uint16_t pkg_entry_count_2;
        uint32_t pkg_table_offset;
        uint32_t pkg_entry_data_size;
        uint64_t pkg_body_offset;
        uint64_t pkg_body_size;
        uint64_t pkg_content_offset;
        uint64_t pkg_content_size;
        unsigned char pkg_content_id[0x24];

        void swapEndianness()
        {
            pkg_magic = endian_swap(pkg_magic);
            pkg_type = endian_swap(pkg_type);
            pkg_0x008 = endian_swap(pkg_0x008);
            pkg_file_count = endian_swap(pkg_file_count);
            pkg_entry_count = endian_swap(pkg_entry_count);
            pkg_sc_entry_count = endian_swap(pkg_sc_entry_count);
            pkg_entry_count_2 = endian_swap(pkg_entry_count_2);
            pkg_table_offset = endian_swap(pkg_table_offset);
            pkg_entry_data_size = endian_swap(pkg_entry_data_size);
            pkg_body_offset = endian_swap(pkg_body_offset);
            pkg_body_size = endian_swap(pkg_body_size);
            pkg_content_offset = endian_swap(pkg_content_offset);
            pkg_content_size = endian_swap(pkg_content_size);
        };
    };
};

}
