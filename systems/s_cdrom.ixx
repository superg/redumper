module;
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <numeric>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>
#include "system.hh"
#include "throw_line.hh"

export module systems.cdrom;

import cd.cd;
import cd.cdrom;
import cd.ecc;
import cd.edc;
import readers.data_reader;
import utils.file_io;



namespace gpsxre
{

export class SystemCDROM : public System
{
public:
    std::string getName() override
    {
        return "CD-ROM";
    }

    Type getType() override
    {
        return Type::RAW_DATA;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &, bool verbose) const override
    {
        std::vector<std::pair<int32_t, int32_t>> invalid_sync;
        uint32_t mode2_form1 = 0;
        uint32_t mode2_form2 = 0;
        uint32_t mode2_form2_edc = 0;
        std::vector<std::pair<int32_t, int32_t>> generated;
        std::vector<std::pair<int32_t, int32_t>> msf_errors;
        std::vector<std::pair<int32_t, int32_t>> invalid_modes;
        std::vector<std::pair<int32_t, int32_t>> ecc_errors;
        std::vector<std::pair<int32_t, int32_t>> ecc_nc_errors;
        std::vector<std::pair<int32_t, int32_t>> edc_errors;
        std::vector<std::pair<int32_t, int32_t>> subheader_mismatches;
        uint32_t redump_errors = 0;

        std::vector<uint32_t> modes(3);
        std::optional<int32_t> lba_base;

        Sector sector;
        uint32_t s = 0;
        for(; data_reader->read((uint8_t *)&sector, data_reader->sectorsBase() + s, 1) == 1; ++s)
        {
            int32_t lba_positional = data_reader->sectorsBase() + s;

            if(memcmp(sector.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
            {
                ranges_append(invalid_sync, lba_positional);
                continue;
            }

            int32_t lba = BCDMSF_to_LBA(sector.header.address);
            if(lba_base)
            {
                if(lba - *lba_base != s)
                    ranges_append(msf_errors, lba_positional);
            }
            else
                lba_base = lba;

            if(std::all_of(sector.mode2.user_data, sector.mode2.user_data + sizeof(sector.mode2.user_data), [](uint8_t v) { return v == 0x55; }))
                ranges_append(generated, lba_positional);

            if(sector.header.mode < modes.size())
                ++modes[sector.header.mode];
            else
                ranges_append(invalid_modes, lba_positional);

            if(sector.header.mode == 0)
            {
                ;
            }
            else if(sector.header.mode == 1)
            {
                bool error_detected = false;

                Sector::ECC ecc(ECC().Generate((uint8_t *)&sector.header));
                if(memcmp(ecc.p_parity, sector.mode1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode1.ecc.q_parity, sizeof(ecc.q_parity)))
                {
                    ranges_append(ecc_errors, lba_positional);
                    error_detected = true;
                }

                uint32_t edc = EDC().update((uint8_t *)&sector, offsetof(Sector, mode1.edc)).final();
                if(edc != sector.mode1.edc)
                {
                    ranges_append(edc_errors, lba_positional);
                    error_detected = true;
                }

                // log dual ECC/EDC mismatch as one error
                if(error_detected)
                    ++redump_errors;
            }
            // XA Mode2 EDC covers subheader, subheader copy and user data, user data size depends on Form1 / Form2 flag
            else if(sector.header.mode == 2)
            {
                // subheader mismatch, just a warning
                if(memcmp(&sector.mode2.xa.sub_header, &sector.mode2.xa.sub_header_copy, sizeof(sector.mode2.xa.sub_header)))
                {
                    ranges_append(subheader_mismatches, lba_positional);
                    ++redump_errors;
                }

                // Form2
                if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
                {
                    ++mode2_form2;

                    // Form2 EDC can be zero depending on mastering utility
                    if(sector.mode2.xa.form2.edc)
                    {
                        uint32_t edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header, offsetof(Sector, mode2.xa.form2.edc) - offsetof(Sector, mode2.xa.sub_header)).final();

                        if(edc != sector.mode2.xa.form2.edc)
                        {
                            ranges_append(edc_errors, lba_positional);
                            ++redump_errors;
                        }

                        ++mode2_form2_edc;
                    }
                }
                // Form1
                else
                {
                    ++mode2_form1;

                    bool error_detected = false;

                    // EDC
                    uint32_t edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header, offsetof(Sector, mode2.xa.form1.edc) - offsetof(Sector, mode2.xa.sub_header)).final();
                    if(edc != sector.mode2.xa.form1.edc)
                    {
                        ranges_append(edc_errors, lba_positional);
                        error_detected = true;
                    }

                    // ECC
                    // modifies sector, make sure sector data is not used after ECC calculation, otherwise header has to be restored
                    Sector::Header header = sector.header;
                    std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0);

                    Sector::ECC ecc(ECC().Generate((uint8_t *)&sector.header));
                    if(memcmp(ecc.p_parity, sector.mode2.xa.form1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode2.xa.form1.ecc.q_parity, sizeof(ecc.q_parity)))
                    {
                        // [PSX] On multi-track discs with zeroed Form2 EDC, the last sector of the data track has ECC calculated with a non-zeroed header
                        sector.header = header;
                        ecc = ECC().Generate((uint8_t *)&sector.header);
                        if(memcmp(ecc.p_parity, sector.mode2.xa.form1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode2.xa.form1.ecc.q_parity, sizeof(ecc.q_parity)))
                        {
                            ranges_append(ecc_errors, lba_positional);
                        }
                        else
                        {
                            ranges_append(ecc_nc_errors, lba_positional);
                        }

                        error_detected = true;
                    }

                    // restore modified sector header
                    sector.header = header;

                    // log dual ECC/EDC mismatch as one error
                    if(error_detected)
                        ++redump_errors;
                }
            }
        }

        os << std::format("  sectors count: {}", s) << std::endl;
        for(uint32_t i = 0; i + 1 < modes.size(); ++i)
            if(modes[i])
                os << std::format("  mode{} sectors: {}", i, modes[i]) << std::endl;

        if(mode2_form1)
            os << std::format("  mode2 (form 1) sectors: {}", mode2_form1) << std::endl;
        if(mode2_form2)
        {
            os << std::format("  mode2 (form 2) sectors: {}", mode2_form2) << std::endl;
            os << std::format("  mode2 (form 2) EDC: {}", mode2_form2_edc ? "yes" : "no") << std::endl;
        }
        if(auto count = ranges_count(invalid_sync); count)
            os << std::format("  invalid sync sectors: {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(invalid_sync)) : "") << std::endl;
        if(auto count = ranges_count(invalid_modes); count)
            os << std::format("  invalid mode sectors: {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(invalid_modes)) : "") << std::endl;
        if(auto count = ranges_count(generated); count)
            os << std::format("  generated sectors (0x55): {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(generated)) : "") << std::endl;
        if(auto count = ranges_count(msf_errors); count)
            os << std::format("  MSF errors: {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(msf_errors)) : "") << std::endl;
        if(auto count = ranges_count(ecc_nc_errors); count)
            os << std::format("  ECC errors (non-compliant): {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(ecc_nc_errors)) : "") << std::endl;
        if(auto count = ranges_count(ecc_errors); count)
            os << std::format("  ECC errors: {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(ecc_errors)) : "") << std::endl;
        if(auto count = ranges_count(edc_errors); count)
            os << std::format("  EDC errors: {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(edc_errors)) : "") << std::endl;
        if(auto count = ranges_count(subheader_mismatches); count)
            os << std::format("  CD-XA subheader mismatches: {}{}", count, verbose ? std::format(" (LBA: {})", ranges_to_string(subheader_mismatches)) : "") << std::endl;
        os << std::endl;
        os << std::format("  REDUMP.ORG errors: {}", redump_errors) << std::endl;
    }

private:
    static void ranges_append(std::vector<std::pair<int32_t, int32_t>> &ranges, int32_t lba)
    {
        if(ranges.empty() || ranges.back().second + 1 != lba)
            ranges.emplace_back(lba, lba);
        else
            ranges.back().second = lba;
    }

    static std::string ranges_to_string(const std::vector<std::pair<int32_t, int32_t>> &ranges)
    {
        std::string line;

        std::string delimiter;
        for(const auto &r : ranges)
        {
            line += delimiter + (r.first == r.second ? std::format("{}", r.first) : std::format("{}-{}", r.first, r.second));

            delimiter = ", ";
        }

        return line;
    }

    static uint64_t ranges_count(const std::vector<std::pair<int32_t, int32_t>> &ranges)
    {
        return std::accumulate(ranges.begin(), ranges.end(), (uint64_t)0, [](uint64_t s, const auto &r) { return s + r.second - r.first + 1; });
    }
};

}
