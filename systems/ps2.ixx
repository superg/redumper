module;
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <regex>
#include <set>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps2;

import filesystem.iso9660;
import readers.data_reader;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemPS2 : public System
{
public:
    std::string getName() override
    {
        return "PS2";
    }


    Type getType() override
    {
        return Type::ISO;
    }


    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &) const override
    {
        iso9660::PrimaryVolumeDescriptor pvd;
        if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
            return;
        auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);

        auto system_cnf = loadCNF(root_directory, "SYSTEM.CNF");
        auto it = system_cnf.find("BOOT2");
        if(it == system_cnf.end())
            return;

        std::smatch matches;
        std::regex_match(it->second, matches, std::regex("^cdrom0?:\\\\*(.*?)(?:;.*|$)"));
        if(matches.size() != 2)
            return;

        auto exe_path = str_uppercase(matches.str(1));

        auto exe_file = root_directory->subEntry(exe_path);
        if(!exe_file)
            return;

        auto exe = exe_file->read();
        if(exe.size() < _EXE_MAGIC.size() || !std::equal(_EXE_MAGIC.cbegin(), _EXE_MAGIC.cend(), exe.cbegin()))
            return;

        os << std::format("  EXE: {}", exe_path) << std::endl;

        {
            time_t t = exe_file->dateTime();
            std::stringstream ss;
            ss << std::put_time(localtime(&t), "%Y-%m-%d");
            os << std::format("  EXE date: {}", ss.str()) << std::endl;
        }

        it = system_cnf.find("VER");
        if(it != system_cnf.end())
            os << std::format("  version: {}", it->second) << std::endl;

        auto serial = deduceSerial(exe_path);
        if(!serial.first.empty() && !serial.second.empty())
            os << std::format("  serial: {}-{}", serial.first, serial.second) << std::endl;

        auto region = detectRegion(serial.first);
        if(!region.empty())
            os << std::format("  region: {}", region) << std::endl;
    }

private:
    static constexpr std::array<uint8_t, 4> _EXE_MAGIC = { 0x7F, 0x45, 0x4C, 0x46 };


    std::map<std::string, std::string> loadCNF(std::shared_ptr<iso9660::Entry> root_directory, std::string cnf_file) const
    {
        std::map<std::string, std::string> cnf;

        auto cnf_entry = root_directory->subEntry(cnf_file);
        if(cnf_entry)
        {
            auto data = cnf_entry->read();
            auto lines = tokenize(std::string(data.begin(), data.end()), "\r\n", nullptr);

            for(auto const &l : lines)
            {
                auto key_value = tokenize(l, "=", nullptr);
                if(key_value.size() == 2)
                    cnf.emplace(trim(key_value.front()), trim(key_value.back()));
            }
        }

        return cnf;
    }


    std::pair<std::string, std::string> deduceSerial(std::string exe_path) const
    {
        std::pair<std::string, std::string> serial;

        std::smatch matches;
        std::regex_match(exe_path, matches, std::regex("(.*\\\\)*([A-Z]*)(_|-)?([A-Z]?[0-9]+)\\.([0-9]+[A-Z]?)"));
        if(matches.size() == 6)
        {
            serial.first = matches.str(2);
            serial.second = matches.str(4) + matches.str(5);
        }

        return serial;
    }


    std::string detectRegion(std::string prefix) const
    {
        std::string region;

        // All Internal serials currently in redump.org
        const std::set<std::string> REGION_J{ "PAPX", "PCPX", "PDPX", "PSXC", "SCAJ", "SCPM", "SCPN", "SCPS", "SLAJ", "SLPM", "SLPS", "SRPM" };
        const std::set<std::string> REGION_K{ "SCKA", "SLKA" };
        const std::set<std::string> REGION_C{ "SCCS" };
        const std::set<std::string> REGION_U{ "PUPX", "SCUS", "SLUS" };
        const std::set<std::string> REGION_E{ "SCED", "SCES", "SLED", "SLES", "TCES" };
        // multi region: "PBPX"
        // preprod only: "ABCD", "XXXX"

        if(REGION_J.find(prefix) != REGION_J.end())
            region = "Japan";
        else if(REGION_K.find(prefix) != REGION_K.end())
            region = "South Korea";
        else if(REGION_C.find(prefix) != REGION_C.end())
            region = "China";
        else if(REGION_U.find(prefix) != REGION_U.end())
            region = "USA";
        else if(REGION_E.find(prefix) != REGION_E.end())
            region = "Europe";

        return region;
    }
};

}
