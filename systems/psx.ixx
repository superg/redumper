module;
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <regex>
#include <set>
#include "system.hh"
#include "throw_line.hh"

export module systems.psx;

import cd.cd;
import cd.cdrom;
import cd.common;
import cd.subcode;
import filesystem.iso9660;
import readers.data_reader;
import utils.endian;
import utils.file_io;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemPSX : public System
{
public:
    std::string getName() override
    {
        return "PSX";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &track_path) const override
    {
        iso9660::PrimaryVolumeDescriptor pvd;
        if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
            return;
        auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);

        auto exe_path = findEXE(root_directory);
        if(exe_path.empty())
            return;

        auto exe_file = root_directory->subEntry(exe_path);
        if(!exe_file)
            return;

        auto exe = exe_file->read();
        if(exe.size() < _EXE_MAGIC.length() || std::string((char *)exe.data(), _EXE_MAGIC.length()) != _EXE_MAGIC)
            return;

        os << std::format("  EXE: {}", exe_path) << std::endl;

        {
            time_t t = exe_file->dateTime();
            std::stringstream ss;
            ss << std::put_time(localtime(&t), "%Y-%m-%d");
            os << std::format("  EXE date: {}", ss.str()) << std::endl;
        }

        auto serial = deduceSerial(exe_path);
        if(!serial.first.empty() && !serial.second.empty())
            os << std::format("  serial: {}-{}", serial.first, serial.second) << std::endl;

        auto region = detectRegion(serial.first);
        if(!region.empty())
            os << std::format("  region: {}", region) << std::endl;

        {
            std::stringstream ss;
            bool antimod = findAntiModchipStrings(ss, root_directory);
            os << std::format("  anti-modchip: {}", antimod ? "yes" : "no") << std::endl;
            if(antimod)
                os << ss.str() << std::endl;
        }

        std::filesystem::path sub_path = track_extract_basename(track_path.string()) + ".subcode";
        if(std::filesystem::exists(sub_path))
        {
            std::stringstream ss;
            bool libcrypt = detectLibCrypt(ss, sub_path);
            os << std::format("  libcrypt: {}", libcrypt ? "yes" : "no") << std::endl;
            if(libcrypt)
                os << ss.str() << std::endl;
        }
    }

private:
    static const std::string _EXE_MAGIC;
    static const std::vector<int32_t> _LIBCRYPT_SECTORS_BASE;
    static const std::vector<int32_t> _LIBCRYPT_SECTORS_BACKUP;
    static const int32_t _LIBCRYPT_SECTORS_MIRROR_SHIFT;
    static const uint32_t _LIBCRYPT_BITS_COUNT;
    static const uint32_t _LIBCRYPT_BITS_COUNT_NETYAROZE;

    std::string findEXE(std::shared_ptr<iso9660::Entry> root_directory) const
    {
        std::string exe_path;

        auto system_cnf = root_directory->subEntry("SYSTEM.CNF");
        if(system_cnf)
        {
            auto data = system_cnf->read();
            std::string data_str(data.begin(), data.end());
            std::stringstream ss(data_str);

            std::string line;
            while(std::getline(ss, line))
            {
                // examples:
                // BOOT = cdrom:\\SCUS_945.03;1\r"   // 1Xtreme (USA)
                // BOOT=cdrom:\\SCUS_944.23;1"       // Ape Escape (USA)
                // BOOT=cdrom:\\SLPS_004.35\r"       // Megatudo 2096 (Japan)
                // BOOT = cdrom:\SLPM803.96;1"       // Chouzetsu Daigirin '99-nen Natsu-ban (Japan)
                // BOOT = cdrom:\EXE\PCPX_961.61;1   // Wild Arms - 2nd Ignition (Japan) (Demo)

                std::smatch matches;
                std::regex_match(line, matches, std::regex("^\\s*BOOT.*=\\s*cdrom.?:\\\\*(.*?)(?:;.*\\s*|\\s*$)"));
                if(matches.size() == 2)
                {
                    exe_path = str_uppercase(matches.str(1));
                    break;
                }
            }
        }
        else
        {
            auto psx_exe = root_directory->subEntry("PSX.EXE");
            if(psx_exe)
                exe_path = psx_exe->name();
        }

        return exe_path;
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

            // Road Writer (USA)
            if(serial.first.empty() && serial.second == "907127001")
                serial.first = "LSP";
            // GameGenius Ver. 5.0 (Taiwan) (En,Zh) (Unl)
            else if(serial.first == "PAR" && serial.second == "90001")
            {
                serial.first.clear();
                serial.second.clear();
            }
        }

        return serial;
    }


    std::string detectRegion(std::string prefix) const
    {
        std::string region;

        const std::set<std::string> REGION_J{ "ESPM", "PAPX", "PCPX", "PDPX", "SCPM", "SCPS", "SCZS", "SIPS", "SLKA", "SLPM", "SLPS" };
        const std::set<std::string> REGION_U{ "LSP", "PUPX", "SCUS", "SLUS", "SLUSP" };
        const std::set<std::string> REGION_E{ "PEPX", "SCED", "SCES", "SLED", "SLES" };
        // multi: "DTL", "PBPX"

        if(REGION_J.find(prefix) != REGION_J.end())
            region = "Japan";
        else if(REGION_U.find(prefix) != REGION_U.end())
            region = "USA";
        else if(REGION_E.find(prefix) != REGION_E.end())
            region = "Europe";

        return region;
    }


    bool findAntiModchipStrings(std::ostream &os, std::shared_ptr<iso9660::Entry> root_directory) const
    {
        std::vector<std::string> entries;

        // taken from DIC
        const char ANTIMOD_MESSAGE_EN[] = "     SOFTWARE TERMINATED\nCONSOLE MAY HAVE BEEN MODIFIED\n     CALL 1-888-780-7690";
        // string is encoded with Shift JIS
        // clang-format off
        const uint8_t ANTIMOD_MESSAGE_JP[] =
        {
            // 強制終了しました。
            0x8b, 0xad, 0x90, 0xa7, 0x8f, 0x49, 0x97, 0xb9, 0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb5, 0x82, 0xbd, 0x81, 0x42, 0x0a,
            // 本体が改造されている
            0x96, 0x7b, 0x91, 0xcc, 0x82, 0xaa, 0x89, 0xfc, 0x91, 0xa2, 0x82, 0xb3, 0x82, 0xea, 0x82, 0xc4, 0x82, 0xa2, 0x82, 0xe9, 0x0a,
            // おそれがあります。
            0x82, 0xa8, 0x82, 0xbb, 0x82, 0xea, 0x82, 0xaa, 0x82, 0xa0, 0x82, 0xe8, 0x82, 0xdc, 0x82, 0xb7, 0x81, 0x42
        };
        // clang-format on

        iso9660::Browser::iterate(root_directory,
            [&](const std::string &path, std::shared_ptr<iso9660::Entry> d)
            {
                bool exit = false;

                auto fp((path.empty() ? "" : path + "/") + d->name());

                auto data = d->read();

                auto it_en = std::search(data.begin(), data.end(), std::begin(ANTIMOD_MESSAGE_EN), std::end(ANTIMOD_MESSAGE_EN));
                if(it_en != data.end())
                {
                    std::stringstream ss;
                    ss << fp << " @ 0x" << std::hex << it_en - data.begin() << ": EN";
                    entries.emplace_back(ss.str());
                }
                auto it_jp = std::search(data.begin(), data.end(), std::begin(ANTIMOD_MESSAGE_JP), std::end(ANTIMOD_MESSAGE_JP));
                if(it_jp != data.end())
                {
                    std::stringstream ss;
                    ss << fp << " @ 0x" << std::hex << it_jp - data.begin() << ": JP";
                    entries.emplace_back(ss.str());
                }

                return exit;
            });

        for(auto const &s : entries)
            os << s << std::endl;

        return !entries.empty();
    }

    static std::vector<bool> getLibCryptKey(std::fstream &fs, std::filesystem::path sub_path, const std::vector<int32_t> &sectors, uint32_t sector_shift)
    {
        std::vector<bool> key(sectors.size());

        std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
        int32_t lba_end = std::filesystem::file_size(sub_path) / CD_SUBCODE_SIZE + LBA_START;
        for(uint32_t i = 0; i < key.size(); ++i)
        {
            int32_t lba = sectors[i] + sector_shift;

            if(lba >= lba_end)
                continue;

            read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), lba - LBA_START, 1, 0, 0);
            auto Q = subcode_extract_q(sub_buffer.data());

            if(!Q.isValid())
                key[i] = true;
        }

        return key;
    }

    bool detectLibCrypt(std::ostream &os, std::filesystem::path sub_path) const
    {
        bool libcrypt = false;

        std::fstream fs(sub_path, std::fstream::in | std::fstream::binary);
        if(!fs.is_open())
            throw_line("unable to open file ({})", sub_path.filename().string());

        // read keys from all possible locations
        auto base = getLibCryptKey(fs, sub_path, _LIBCRYPT_SECTORS_BASE, 0);
        auto base_mirror = getLibCryptKey(fs, sub_path, _LIBCRYPT_SECTORS_BASE, _LIBCRYPT_SECTORS_MIRROR_SHIFT);
        auto backup = getLibCryptKey(fs, sub_path, _LIBCRYPT_SECTORS_BACKUP, 0);
        auto backup_mirror = getLibCryptKey(fs, sub_path, _LIBCRYPT_SECTORS_BACKUP, _LIBCRYPT_SECTORS_MIRROR_SHIFT);

        // filter out unintentional Q errors in backup / mirror keys using base key as a reference
        std::transform(base.begin(), base.end(), base_mirror.begin(), base_mirror.begin(), [](bool b1, bool b2) { return b1 && b2; });
        std::transform(base.begin(), base.end(), backup.begin(), backup.begin(), [](bool b1, bool b2) { return b1 && b2; });
        std::transform(base.begin(), base.end(), backup_mirror.begin(), backup_mirror.begin(), [](bool b1, bool b2) { return b1 && b2; });

        auto base_mirror_count = std::count(base_mirror.begin(), base_mirror.end(), true);
        auto backup_count = std::count(backup.begin(), backup.end(), true);
        auto backup_mirror_count = std::count(backup_mirror.begin(), backup_mirror.end(), true);

        bool base_mirror_exists = base_mirror_count == _LIBCRYPT_BITS_COUNT;
        bool backup_exists = backup_count == _LIBCRYPT_BITS_COUNT;
        bool backup_mirror_exists = backup_mirror_count == _LIBCRYPT_BITS_COUNT;

        // filter out unintentional Q errors in base key using backup / mirror keys as a reference
        if(base_mirror_exists)
            std::transform(base_mirror.begin(), base_mirror.end(), base.begin(), base.begin(), [](bool b1, bool b2) { return b1 && b2; });
        if(backup_exists)
            std::transform(backup.begin(), backup.end(), base.begin(), base.begin(), [](bool b1, bool b2) { return b1 && b2; });
        if(backup_mirror_exists)
            std::transform(backup_mirror.begin(), backup_mirror.end(), base.begin(), base.begin(), [](bool b1, bool b2) { return b1 && b2; });

        auto base_count = std::count(base.begin(), base.end(), true);

        std::set<int32_t> candidates;

        // strong check: base key exists and one of the backup / mirror keys exist
        // weak check: 6 sectors without backup / mirror (Net Yaroze disc)
        if(base_count == _LIBCRYPT_BITS_COUNT && (base_mirror_exists || backup_exists || backup_mirror_exists) || base_count == _LIBCRYPT_BITS_COUNT_NETYAROZE)
        {
            for(uint32_t i = 0; i < base.size(); ++i)
            {
                if(base[i])
                {
                    candidates.insert(_LIBCRYPT_SECTORS_BASE[i]);
                    if(base_mirror_exists)
                        candidates.insert(_LIBCRYPT_SECTORS_BASE[i] + _LIBCRYPT_SECTORS_MIRROR_SHIFT);

                    if(backup_exists)
                        candidates.insert(_LIBCRYPT_SECTORS_BACKUP[i]);
                    if(backup_mirror_exists)
                        candidates.insert(_LIBCRYPT_SECTORS_BACKUP[i] + _LIBCRYPT_SECTORS_MIRROR_SHIFT);
                }
            }
        }

        if(!candidates.empty())
        {
            std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);

            for(auto c : candidates)
            {
                read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), c - LBA_START, 1, 0, 0);
                auto Q = subcode_extract_q(sub_buffer.data());
                redump_print_subq(os, c, Q);
            }

            libcrypt = true;
        }

        return libcrypt;
    }
};


const std::string SystemPSX::_EXE_MAGIC("PS-X EXE");

const std::vector<int32_t> SystemPSX::_LIBCRYPT_SECTORS_BASE = { 13955, 14081, 14335, 14429, 14499, 14749, 14906, 14980, 15092, 15162, 15228, 15478, 15769, 15881, 15951, 16017 };

const std::vector<int32_t> SystemPSX::_LIBCRYPT_SECTORS_BACKUP = { 41895, 42016, 42282, 42430, 42521, 42663, 42862, 43027, 43139, 43204, 43258, 43484, 43813, 43904, 44009, 44162 };

const int32_t SystemPSX::_LIBCRYPT_SECTORS_MIRROR_SHIFT = 5;
const uint32_t SystemPSX::_LIBCRYPT_BITS_COUNT = 8;
const uint32_t SystemPSX::_LIBCRYPT_BITS_COUNT_NETYAROZE = 6;

}
