module;
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps4;

import filesystem.iso9660;
import readers.data_reader;
import systems.ps3;



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

        std::string content_ids = getContentIds(root_directory, "app.pkg");

        if(content_ids.empty())
            return;

        os << std::format("  content ID(s): {}", content_ids) << std::endl;
    }

protected:
    std::string getContentIds(std::shared_ptr<iso9660::Entry> root_directory, const std::string pkg_name) const
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

            auto app_pkg_entry = e->subEntry(pkg_name);
            if(!app_pkg_entry)
                continue;

            auto app_pkg_raw = app_pkg_entry->read();

            if(app_pkg_raw.size() < _PKG_HEADER_SIZE)
                continue;

            uint32_t app_pkg_magic = 
                (static_cast<uint32_t>(app_pkg_raw[_PKG_MAGIC_OFFSET + 0]) << 24) |
                (static_cast<uint32_t>(app_pkg_raw[_PKG_MAGIC_OFFSET + 1]) << 16) |
                (static_cast<uint32_t>(app_pkg_raw[_PKG_MAGIC_OFFSET + 2]) << 8)  |
                (static_cast<uint32_t>(app_pkg_raw[_PKG_MAGIC_OFFSET + 3]) << 0);

            if(app_pkg_magic != _PKG_MAGIC)
                continue;

            std::span<const uint8_t> app_pkg_content_id(app_pkg_raw.data() + _PKG_CONTENT_ID_OFFSET, _PKG_CONTENT_ID_SIZE);

            std::string_view app_pkg_content_id_text(reinterpret_cast<const char*>(app_pkg_content_id.data()), _PKG_CONTENT_ID_SIZE);

            if(!content_ids.empty())
                content_ids += ", ";

            content_ids += app_pkg_content_id_text;
        }

        return content_ids;
    }

private:
    static constexpr uint32_t _PKG_HEADER_SIZE = 0x1000;
    static constexpr uint32_t _PKG_MAGIC = 0x7F434E54;
    static constexpr uint32_t _PKG_MAGIC_OFFSET = 0x00;
    static constexpr uint32_t _PKG_CONTENT_ID_OFFSET = 0x40;
    static constexpr uint32_t _PKG_CONTENT_ID_SIZE = 36;
};

}
