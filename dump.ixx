module;
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include "throw_line.hh"

export module dump;

import cd.cd;
import cd.cdrom;
import cd.scrambler;
import cd.subcode;
import cd.toc;
import drive;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export struct Errors
{
    uint32_t scsi;
    uint32_t c2;
    uint32_t q;
};


export struct Context
{
    GET_CONFIGURATION_FeatureCode_ProfileList current_profile;
    std::shared_ptr<SPTD> sptd;
    DriveConfig drive_config;

    std::optional<std::vector<std::pair<int32_t, int32_t>>> rings;
    std::optional<Errors> dump_errors;
    std::vector<std::pair<int32_t, int32_t>> protection;
    std::optional<bool> protection_trim;
    std::optional<bool> refine;
    std::optional<std::vector<std::string>> dat;
};


export enum class DumpMode
{
    DUMP,
    VERIFY,
    REFINE
};


export constexpr int32_t LBA_START = -45150; // MSVC internal compiler error: MSF_to_LBA(MSF_LEADIN_START); // -45150


export enum class State : uint8_t
{
    ERROR_SKIP, // must be first to support random offset file writes
    ERROR_C2,
    SUCCESS_C2_OFF,
    SUCCESS_SCSI_OFF,
    SUCCESS
};


export void image_check_empty(const Options &options)
{
    if(options.image_name.empty())
        throw_line("image name is not provided");
}


export void image_check_overwrite(const Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    std::string state_path(image_prefix + ".state");

    if(!options.overwrite && std::filesystem::exists(state_path))
        throw_line("dump already exists (image name: {})", options.image_name);
}


export void print_toc(const TOC &toc)
{
    std::stringstream ss;
    toc.print(ss);

    std::string line;
    while(std::getline(ss, line))
        LOG("{}", line);
}


export int32_t sample_offset_a2r(uint32_t absolute)
{
    return absolute + (LBA_START * CD_DATA_SIZE_SAMPLES);
}


export uint32_t sample_offset_r2a(int32_t relative)
{
    return relative - (LBA_START * CD_DATA_SIZE_SAMPLES);
}


export int32_t lba_to_sample(int32_t lba, int32_t offset = 0)
{
    return lba * CD_DATA_SIZE_SAMPLES + offset;
}


export int32_t sample_to_lba(int32_t sample, int32_t offset = 0)
{
    return scale_left(sample - offset, CD_DATA_SIZE_SAMPLES);
}


export std::vector<std::pair<int32_t, int32_t>> get_protection_sectors(const Context &ctx, int32_t offset)
{
    std::vector<std::pair<int32_t, int32_t>> protection;

    for(auto const &e : ctx.protection)
        protection.emplace_back(sample_to_lba(e.first, -offset), sample_to_lba(e.second, -offset));

    return protection;
}


export TOC toc_choose(const std::vector<uint8_t> &toc_buffer, const std::vector<uint8_t> &full_toc_buffer)
{
    TOC toc(toc_buffer, false);

    if(!full_toc_buffer.empty())
    {
        TOC toc_full(full_toc_buffer, true);

        // [PSX] Motocross Mania
        // [ENHANCED-CD] Vanishing Point
        // PX-W5224TA: incorrect FULL TOC data in some cases
        toc_full.deriveINDEX(toc);

        // prefer TOC for single session discs and FULL TOC for multisession discs
        if(toc_full.sessions.size() > 1)
            toc = toc_full;
    }

    return toc;
}


export bool drive_is_plextor4824(const DriveConfig &drive_config)
{
    return drive_config.vendor_id == "PLEXTOR" && drive_config.product_id == "CD-R PX-W4824A";
}


export bool toc_enable_cdtext(const Context &ctx, const TOC &toc, const Options &options)
{
    if(options.disable_cdtext)
        return false;
    else if(options.force_cdtext_reading)
        return true;
    else
        return !drive_is_plextor4824(ctx.drive_config) || toc.sessions.size() <= 1;
}


export void subcode_load_subpq(std::vector<ChannelP> &subp, std::vector<ChannelQ> &subq, const std::filesystem::path &sub_path)
{
    uint32_t sectors_count = std::filesystem::file_size(sub_path) / CD_SUBCODE_SIZE;
    subp.resize(sectors_count);
    subq.resize(sectors_count);

    std::fstream fs(sub_path, std::fstream::in | std::fstream::binary);
    if(!fs.is_open())
        throw_line("unable to open file ({})", sub_path.filename().string());


    std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
    for(uint32_t lba_index = 0; lba_index < subq.size(); ++lba_index)
    {
        read_entry(fs, sub_buffer.data(), (uint32_t)sub_buffer.size(), lba_index, 1, 0, 0);

        subcode_extract_channel((uint8_t *)&subp[lba_index], sub_buffer.data(), Subchannel::P);
        subcode_extract_channel((uint8_t *)&subq[lba_index], sub_buffer.data(), Subchannel::Q);
    }
}


export std::vector<uint8_t> subcode_correct_subp(const ChannelP *subp_raw, uint32_t sectors_count)
{
    std::vector<uint8_t> subp(sectors_count);

    for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
    {
        uint32_t p_bits = 0;
        for(uint32_t i = 0; i < CD_SUBCODE_SIZE / CHAR_BIT; ++i)
            p_bits += std::popcount(subp_raw[lba_index].pause[i]);

        subp[lba_index] = p_bits >= CD_SUBCODE_SIZE / 2 ? 1 : 0;
    }

    return subp;
}


export bool subcode_correct_subq(ChannelQ *subq, uint32_t sectors_count)
{
    uint32_t mcn = sectors_count;
    std::map<uint8_t, uint32_t> isrc;
    ChannelQ q_empty;
    memset(&q_empty, 0, sizeof(q_empty));

    bool invalid_subq = true;
    uint8_t tno = 0;
    for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
    {
        if(!subq[lba_index].isValid())
            continue;

        invalid_subq = false;

        if(subq[lba_index].adr == 1)
            tno = subq[lba_index].mode1.tno;
        else if(subq[lba_index].adr == 2 && mcn == sectors_count)
            mcn = lba_index;
        else if(subq[lba_index].adr == 3 && tno && isrc.find(tno) == isrc.end())
            isrc[tno] = lba_index;
    }

    if(invalid_subq)
        return false;

    uint32_t q_prev = sectors_count;
    uint32_t q_next = 0;
    for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
    {
        if(!memcmp(&subq[lba_index], &q_empty, sizeof(q_empty)))
            continue;

        // treat unexpected MSF as invalid (SecuROM)
        if(subq[lba_index].isValid(lba_index + LBA_START))
        {
            if(subq[lba_index].adr == 1)
            {
                if(subq[lba_index].mode1.tno)
                    q_prev = lba_index;
                else
                    q_prev = sectors_count;
            }
        }
        else
        {
            // find next valid Q
            if(lba_index >= q_next && q_next != sectors_count)
            {
                q_next = lba_index + 1;
                for(; q_next < sectors_count; ++q_next)
                    if(subq[q_next].isValid(q_next + LBA_START))
                    {
                        if(subq[q_next].adr == 1)
                        {
                            if(!subq[q_next].mode1.tno)
                                q_next = 0;

                            break;
                        }
                    }
            }

            std::vector<ChannelQ> candidates;
            if(q_prev < lba_index)
            {
                // mode 1
                candidates.emplace_back(subq[q_prev].generateMode1(lba_index - q_prev));

                // mode 2
                if(mcn != sectors_count)
                    candidates.emplace_back(subq[q_prev].generateMode23(subq[mcn], lba_index - q_prev));

                // mode 3
                if(!isrc.empty())
                {
                    auto it = isrc.find(subq[q_prev].mode1.tno);
                    if(it != isrc.end())
                        candidates.emplace_back(subq[q_prev].generateMode23(subq[it->second], lba_index - q_prev));
                }
            }

            if(q_next > lba_index && q_next != sectors_count)
            {
                // mode 1
                candidates.emplace_back(subq[q_next].generateMode1(lba_index - q_next));

                // mode 2
                if(mcn != sectors_count)
                    candidates.emplace_back(subq[q_next].generateMode23(subq[mcn], lba_index - q_next));

                // mode 3
                if(!isrc.empty())
                {
                    auto it = isrc.find(subq[q_next].mode1.tno);
                    if(it != isrc.end())
                        candidates.emplace_back(subq[q_next].generateMode23(subq[it->second], lba_index - q_next));
                }
            }

            if(!candidates.empty())
            {
                uint32_t c = 0;
                for(uint32_t j = 0; j < (uint32_t)candidates.size(); ++j)
                    if(bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[j], sizeof(ChannelQ) / sizeof(uint32_t))
                        < bit_diff((uint32_t *)&subq[lba_index], (uint32_t *)&candidates[c], sizeof(ChannelQ) / sizeof(uint32_t)))
                        c = j;

                subq[lba_index] = candidates[c];
            }
        }
    }

    return true;
}


export std::ostream &redump_print_subq(std::ostream &os, int32_t lba, const ChannelQ &Q)
{
    MSF msf = LBA_to_MSF(lba);
    os << std::format("MSF: {:02}:{:02}:{:02} Q-Data: {:X}{:X}{:02X}{:02X} {:02X}:{:02X}:{:02X} {:02X} {:02X}:{:02X}:{:02X} {:04X}", msf.m, msf.s, msf.f, (uint8_t)Q.control, (uint8_t)Q.adr,
        Q.mode1.tno, Q.mode1.point_index, Q.mode1.msf.m, Q.mode1.msf.s, Q.mode1.msf.f, Q.mode1.zero, Q.mode1.a_msf.m, Q.mode1.a_msf.s, Q.mode1.a_msf.f, endian_swap<uint16_t>(Q.crc))
       << std::endl;

    return os;
}


export bool profile_is_cd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_R || profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW;
}


export bool profile_is_dvd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL_LJR
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW_DL || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R_DL;
}


export bool profile_is_bluray(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_R || profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_RW;
}


export bool profile_is_hddvd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RAM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R_DL || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW_DL;
}


// OBSOLETE: remove after migrating to new CD dump code
export SPTD::Status read_sector(SPTD &sptd, uint8_t *sector, const DriveConfig &drive_config, int32_t lba)
{
    auto layout = sector_order_layout(drive_config.sector_order);

    // PLEXTOR: C2 is shifted 294/295 bytes late, read as much sectors as needed to get whole C2
    // as a consequence, lead-out overread will fail a few sectors earlier
    uint32_t sectors_count = drive_config.c2_shift / CD_C2_SIZE + (drive_config.c2_shift % CD_C2_SIZE ? 1 : 0) + 1;
    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE * sectors_count);

    SPTD::Status status;
    // D8
    if(drive_config.read_method == DriveConfig::ReadMethod::D8)
    {
        status = cmd_read_cdda(sptd, sector_buffer.data(), lba, sectors_count,
            drive_config.sector_order == DriveConfig::SectorOrder::DATA_SUB ? READ_CDDA_SubCode::DATA_SUB : READ_CDDA_SubCode::DATA_C2_SUB);
    }
    // BE
    else
    {
        status = cmd_read_cd(sptd, sector_buffer.data(), lba, sectors_count,
            drive_config.read_method == DriveConfig::ReadMethod::BE_CDDA ? READ_CD_ExpectedSectorType::CD_DA : READ_CD_ExpectedSectorType::ALL_TYPES,
            layout.c2_offset == CD_RAW_DATA_SIZE ? READ_CD_ErrorField::NONE : READ_CD_ErrorField::C2, layout.subcode_offset == CD_RAW_DATA_SIZE ? READ_CD_SubChannel::NONE : READ_CD_SubChannel::RAW);
    }

    if(!status.status_code)
    {
        memset(sector, 0x00, CD_RAW_DATA_SIZE);

        // copy data
        if(layout.data_offset != CD_RAW_DATA_SIZE)
            memcpy(sector + 0, sector_buffer.data() + layout.data_offset, CD_DATA_SIZE);

        // copy C2
        if(layout.c2_offset != CD_RAW_DATA_SIZE)
        {
            // compensate C2 shift
            std::vector<uint8_t> c2_buffer(CD_C2_SIZE * sectors_count);
            for(uint32_t i = 0; i < sectors_count; ++i)
                memcpy(c2_buffer.data() + CD_C2_SIZE * i, sector_buffer.data() + layout.size * i + layout.c2_offset, CD_C2_SIZE);

            memcpy(sector + CD_DATA_SIZE, c2_buffer.data() + drive_config.c2_shift, CD_C2_SIZE);
        }

        // copy subcode
        if(layout.subcode_offset != CD_RAW_DATA_SIZE)
            memcpy(sector + CD_DATA_SIZE + CD_C2_SIZE, sector_buffer.data() + layout.subcode_offset, CD_SUBCODE_SIZE);
    }

    return status;
}


export SPTD::Status read_sector_new(SPTD &sptd, uint8_t *sector, bool &all_types, const DriveConfig &drive_config, int32_t lba)
{
    SPTD::Status status;

    auto layout = sector_order_layout(drive_config.sector_order);

    // PLEXTOR: C2 is shifted 294/295 bytes late (drive dependent), read as much sectors as needed to get whole C2
    // as a consequence, lead-out overread will fail a few sectors earlier
    uint32_t sectors_count = 1 + scale_up(drive_config.c2_shift, CD_C2_SIZE);

    // cmd_read_cdda / cmd_read_cd functions internally "knows" this buffer size
    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE * sectors_count);

    // D8
    if(drive_config.read_method == DriveConfig::ReadMethod::D8)
    {
        auto sub_code = drive_config.sector_order == DriveConfig::SectorOrder::DATA_SUB ? READ_CDDA_SubCode::DATA_SUB : READ_CDDA_SubCode::DATA_C2_SUB;
        status = cmd_read_cdda(sptd, sector_buffer.data(), lba, sectors_count, sub_code);
    }
    else
    {
        auto error_field = layout.c2_offset == CD_RAW_DATA_SIZE ? READ_CD_ErrorField::NONE : READ_CD_ErrorField::C2;
        auto sub_channel = layout.subcode_offset == CD_RAW_DATA_SIZE ? READ_CD_SubChannel::NONE : READ_CD_SubChannel::RAW;

        bool read_all_types = false;
        if(all_types)
        {
            read_all_types = true;
        }
        // read as audio (according to MMC-3 standard, the CD-DA sector type support is optional)
        else
        {
            status = cmd_read_cd(sptd, sector_buffer.data(), lba, sectors_count, READ_CD_ExpectedSectorType::CD_DA, error_field, sub_channel);
            if(status.status_code)
            {
                read_all_types = true;
            }
        }

        // read failed, either data sector is encountered (likely) or CD-DA sector type call is unsupported (unlikely)
        if(read_all_types)
        {
            // read without filter
            status = cmd_read_cd(sptd, sector_buffer.data(), lba, sectors_count, READ_CD_ExpectedSectorType::ALL_TYPES, error_field, sub_channel);

            // read success
            if(!status.status_code && layout.data_offset != CD_RAW_DATA_SIZE)
            {
                auto data = sector_buffer.data() + layout.data_offset;

                // rule out audio sector if CD-DA sector type call is unsupported
                if(std::equal(data, data + sizeof(CD_DATA_SYNC), CD_DATA_SYNC))
                {
                    // scramble data back
                    Scrambler::process(data, data, 0, CD_DATA_SIZE);
                    all_types = true;
                }
            }
        }
    }

    if(!status.status_code)
    {
        // compensate C2 shift
        if(layout.c2_offset != CD_RAW_DATA_SIZE)
        {
            std::vector<uint8_t> c2_buffer(CD_C2_SIZE * sectors_count);

            for(uint32_t i = 0; i < sectors_count; ++i)
            {
                auto src = sector_buffer.data() + layout.size * i + layout.c2_offset;
                std::copy(src, src + CD_C2_SIZE, c2_buffer.data() + CD_C2_SIZE * i);
            }

            {
                auto src = c2_buffer.data() + drive_config.c2_shift;
                std::copy(src, src + CD_C2_SIZE, sector_buffer.data() + layout.c2_offset);
            }
        }

        auto dst = sector;

        auto copy_or_clear = [&](uint32_t offset, uint32_t size)
        {
            if(offset == CD_RAW_DATA_SIZE)
                std::fill(dst, dst + size, 0x00);
            else
            {
                auto src = sector_buffer.data() + offset;
                std::copy(src, src + size, dst);
            }
            dst += size;
        };

        copy_or_clear(layout.data_offset, CD_DATA_SIZE);
        copy_or_clear(layout.c2_offset, CD_C2_SIZE);
        copy_or_clear(layout.subcode_offset, CD_SUBCODE_SIZE);
    }

    return status;
}


export std::optional<int32_t> sector_offset_by_sync(std::span<uint8_t> data, int32_t lba)
{
    std::optional<int32_t> offset;

    if(auto it = std::search(data.begin(), data.end(), std::begin(CD_DATA_SYNC), std::end(CD_DATA_SYNC)); it != data.end())
    {
        std::span<uint8_t> sector(it, data.end());

        // enough data for MSF
        if(sector.size() >= sizeof(CD_DATA_SYNC) + sizeof(MSF))
        {
            MSF msf;
            Scrambler scrambler;
            scrambler.process((uint8_t *)&msf, (uint8_t *)&sector[sizeof(CD_DATA_SYNC)], sizeof(CD_DATA_SYNC), sizeof(MSF));

            if(BCDMSF_valid(msf))
            {
                int32_t sector_lba = BCDMSF_to_LBA(msf);
                offset = ((int32_t)(it - data.begin()) - (sector_lba - lba) * (int32_t)CD_DATA_SIZE) / (int32_t)CD_SAMPLE_SIZE;
            }
        }
    }

    return offset;
}


export std::optional<int32_t> track_offset_by_sync(int32_t lba_start, int32_t lba_end, std::fstream &state_fs, std::fstream &scm_fs)
{
    std::optional<int32_t> offset;

    const uint32_t sectors_to_check = 2;
    std::vector<uint8_t> data(sectors_to_check * CD_DATA_SIZE);
    std::vector<State> state(sectors_to_check * CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);

    for(uint32_t i = 0; i < round_down(lba_end - lba_start, sectors_to_check); i += sectors_to_check)
    {
        read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba_start + i - LBA_START, sectors_to_check, 0, 0);
        read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_start + i - LBA_START, sectors_to_check, 0, (uint8_t)State::ERROR_SKIP);
        if(std::any_of(state.begin(), state.end(), [](State s) { return s == State::ERROR_SKIP || s == State::ERROR_C2; }))
            continue;

        offset = sector_offset_by_sync(data, lba_start + i);
        if(offset)
            break;
    }

    return offset;
}


export std::optional<int32_t> track_offset_by_sync(Context &ctx, uint32_t lba, uint32_t count)
{
    std::optional<int32_t> offset;

    const uint32_t sectors_to_check = 2;
    std::vector<uint8_t> data(sectors_to_check * CD_DATA_SIZE);
    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);

    for(uint32_t i = 0; i < round_down(count, sectors_to_check); i += sectors_to_check)
    {
        for(uint32_t j = 0; j < sectors_to_check; ++j)
        {
            auto status = read_sector(*ctx.sptd, sector_buffer.data(), ctx.drive_config, lba + i + j);
            if(status.status_code)
                throw_line("failed to read sector");

            std::copy(&sector_buffer[0], &sector_buffer[CD_DATA_SIZE], &data[j * CD_DATA_SIZE]);
        }

        auto o = sector_offset_by_sync(data, lba + i);
        if(o)
        {
            offset = *o - ctx.drive_config.read_offset;
            break;
        }
    }

    return offset;
}


export std::list<std::pair<std::string, bool>> cue_get_entries(const std::filesystem::path &cue_path)
{
    std::list<std::pair<std::string, bool>> entries;

    std::fstream fs(cue_path, std::fstream::in);
    if(!fs.is_open())
        throw_line("unable to open file ({})", cue_path.filename().string());

    std::pair<std::string, bool> entry;
    std::string line;
    while(std::getline(fs, line))
    {
        auto tokens(tokenize(line, " \t", "\"\""));
        if(tokens.size() == 3)
        {
            if(tokens[0] == "FILE")
                entry.first = tokens[1];
            else if(tokens[0] == "TRACK" && !entry.first.empty())
            {
                entry.second = tokens[2] != "AUDIO";
                entries.push_back(entry);
                entry.first.clear();
            }
        }
    }

    return entries;
}


// FIXME: just do regexp
export std::string track_extract_basename(std::string str)
{
    std::string basename = str;

    // strip extension
    {
        auto pos = basename.find_last_of('.');
        if(pos != std::string::npos)
            basename = std::string(basename, 0, pos);
    }

    // strip (Track X)
    {
        auto pos = str.find(" (Track ");
        if(pos != std::string::npos)
            basename = std::string(basename, 0, pos);
    }

    return basename;
}


export void debug_print_c2_scm_offsets(const uint8_t *c2_data, uint32_t lba_index, int32_t lba_start, int32_t drive_read_offset)
{
    uint32_t scm_offset = lba_index * CD_DATA_SIZE - drive_read_offset * CD_SAMPLE_SIZE;
    uint32_t state_offset = lba_index * CD_DATA_SIZE_SAMPLES - drive_read_offset;

    std::string offset_str;
    for(uint32_t i = 0; i < CD_DATA_SIZE; ++i)
    {
        uint32_t byte_offset = i / CHAR_BIT;
        uint32_t bit_offset = ((CHAR_BIT - 1) - i % CHAR_BIT);

        if(c2_data[byte_offset] & (1 << bit_offset))
            offset_str += std::format("{:08X} ", scm_offset + i);
    }
    LOG("");
    LOG("C2 [LBA: {}, SCM: {:08X}, STATE: {:08X}]: {}", (int32_t)lba_index + lba_start, scm_offset, state_offset, offset_str);
}


export uint32_t debug_get_scram_offset(int32_t lba, int32_t write_offset)
{
    return (lba - LBA_START) * CD_DATA_SIZE + write_offset * CD_SAMPLE_SIZE;
}

}
