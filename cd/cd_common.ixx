module;
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <list>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "throw_line.hh"

export module cd.common;

import cd.cd;
import cd.cdrom;
import cd.scrambler;
import cd.subcode;
import cd.toc;
import common;
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

export constexpr int32_t LBA_START = -45150; // MSVC internal compiler error: MSF_to_LBA(MSF_LEADIN_START); // -45150
export constexpr uint32_t LEADOUT_OVERREAD_COUNT = 100;


export enum class TrackType
{
    AUDIO,
    MODE1_2352,
    MODE2_2352,
    CDI_2352,
    MODE1_2048,
    MODE2_2336,
    CDI_2336,
    CDG,
    MODE0_2352,
    MODE2_2048,
    MODE2_2324,
    UNKNOWN
};


export bool track_type_is_data_raw(TrackType track_type)
{
    return track_type == TrackType::MODE1_2352 || track_type == TrackType::MODE2_2352 || track_type == TrackType::CDI_2352 || track_type == TrackType::MODE0_2352;
}


export bool track_type_is_data_iso(TrackType track_type)
{
    return track_type == TrackType::MODE1_2048 || track_type == TrackType::MODE2_2048;
}


export bool track_type_is_data(TrackType track_type)
{
    return track_type_is_data_raw(track_type) || track_type_is_data_iso(track_type);
}


export int32_t sample_offset_a2r(uint32_t absolute)
{
    return absolute + (LBA_START * CD_DATA_SIZE_SAMPLES);
}


export uint32_t sample_offset_r2a(int32_t relative)
{
    return relative - (LBA_START * CD_DATA_SIZE_SAMPLES);
}


export int32_t lba_to_sample(int32_t lba, int32_t offset)
{
    return lba * CD_DATA_SIZE_SAMPLES + offset;
}


export int32_t sample_to_lba(int32_t sample, int32_t offset)
{
    return scale_left(sample - offset, CD_DATA_SIZE_SAMPLES);
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


export TOC toc_process(Context &ctx, const Options &options, bool store)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::string toc_path(image_prefix + ".toc");
    std::string fulltoc_path(image_prefix + ".fulltoc");
    std::string pma_path(image_prefix + ".pma");
    std::string atip_path(image_prefix + ".atip");
    std::string cdtext_path(image_prefix + ".cdtext");

    SPTD::Status status;

    std::vector<uint8_t> toc_buffer;
    status = cmd_read_toc(*ctx.sptd, toc_buffer, false, READ_TOC_Format::TOC, 1);
    if(status.status_code)
        throw_line("failed to read TOC, SCSI ({})", SPTD::StatusMessage(status));

    // optional
    std::vector<uint8_t> full_toc_buffer;
    status = cmd_read_toc(*ctx.sptd, full_toc_buffer, true, READ_TOC_Format::FULL_TOC, 1);
    if(status.status_code)
        LOG("warning: FULL_TOC is unavailable (no multisession information), SCSI ({})", SPTD::StatusMessage(status));

    auto toc = toc_choose(toc_buffer, full_toc_buffer);

    // store TOC information
    if(store)
    {
        // TOC / FULL_TOC
        write_vector(toc_path, toc_buffer);
        if(full_toc_buffer.size() > sizeof(CMD_ParameterListHeader))
            write_vector(fulltoc_path, full_toc_buffer);

        // PMA
        std::vector<uint8_t> pma_buffer;
        status = cmd_read_toc(*ctx.sptd, pma_buffer, true, READ_TOC_Format::PMA, 0);
        if(!status.status_code && pma_buffer.size() > sizeof(CMD_ParameterListHeader))
            write_vector(pma_path, pma_buffer);

        // ATIP
        std::vector<uint8_t> atip_buffer;
        status = cmd_read_toc(*ctx.sptd, atip_buffer, true, READ_TOC_Format::ATIP, 0);
        if(!status.status_code && atip_buffer.size() > sizeof(CMD_ParameterListHeader))
            write_vector(atip_path, atip_buffer);

        // CD-TEXT
        if(options.disable_cdtext)
            LOG("warning: CD-TEXT disabled");
        else
        {
            std::vector<uint8_t> cd_text_buffer;
            status = cmd_read_toc(*ctx.sptd, cd_text_buffer, false, READ_TOC_Format::CD_TEXT, 0);
            if(status.status_code)
                LOG("warning: unable to read CD-TEXT, SCSI ({})", SPTD::StatusMessage(status));

            if(cd_text_buffer.size() > sizeof(CMD_ParameterListHeader))
                write_vector(cdtext_path, cd_text_buffer);
        }
    }
    // compare disc / file TOC to make sure it's the same disc
    else if(!options.force_refine)
    {
        std::vector<uint8_t> toc_buffer_file = read_vector(toc_path);
        if(toc_buffer != toc_buffer_file)
            throw_line("disc / file TOC don't match, refining from a different disc?");
    }

    return toc;
}


export void print_toc(const TOC &toc)
{
    std::stringstream ss;
    toc.print(ss);

    std::string line;
    while(std::getline(ss, line))
        LOG("{}", line);
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

        if(subq[lba_index].isValid())
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
                    if(subq[q_next].isValid())
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
        status = cmd_read_cdda(sptd, sector_buffer.data(), CD_RAW_DATA_SIZE, lba, sectors_count, sub_code);
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
            status = cmd_read_cd(sptd, sector_buffer.data(), CD_RAW_DATA_SIZE, lba, sectors_count, READ_CD_ExpectedSectorType::CD_DA, error_field, sub_channel);
            if(status.status_code)
            {
                read_all_types = true;
            }
        }

        // read failed, either data sector is encountered (likely) or CD-DA sector type call is unsupported (unlikely)
        if(read_all_types)
        {
            // read without filter
            status = cmd_read_cd(sptd, sector_buffer.data(), CD_RAW_DATA_SIZE, lba, sectors_count, READ_CD_ExpectedSectorType::ALL_TYPES, error_field, sub_channel);

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


export uint32_t c2_bits_count(std::span<const uint8_t> c2_data)
{
    return std::accumulate(c2_data.begin(), c2_data.end(), 0, [](uint32_t accumulator, uint8_t c2) { return accumulator + std::popcount(c2); });
}


export std::vector<State> c2_to_state(const uint8_t *c2_data, State state_default)
{
    std::vector<State> state(CD_DATA_SIZE_SAMPLES);

    // sample based granularity (4 bytes), if any C2 bit within 1 sample is set, mark whole sample as bad
    for(uint32_t i = 0; i < state.size(); ++i)
    {
        uint8_t c2_quad = c2_data[i / 2];
        if(i % 2)
            c2_quad &= 0x0F;
        else
            c2_quad >>= 4;

        state[i] = c2_quad ? State::ERROR_C2 : state_default;
    }

    return state;
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
            bool all_types = false;
            auto status = read_sector_new(*ctx.sptd, sector_buffer.data(), all_types, ctx.drive_config, lba + i + j);
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


export std::list<std::pair<std::string, TrackType>> cue_get_entries(const std::filesystem::path &cue_path)
{
    std::list<std::pair<std::string, TrackType>> entries;

    std::fstream fs(cue_path, std::fstream::in);
    if(!fs.is_open())
        throw_line("unable to open file ({})", cue_path.filename().string());

    std::pair<std::string, TrackType> entry;
    std::string line;
    while(std::getline(fs, line))
    {
        auto tokens(tokenize(line, " \t\r", "\"\""));
        if(tokens.size() == 3)
        {
            if(tokens[0] == "FILE")
                entry.first = tokens[1];
            else if(tokens[0] == "TRACK" && !entry.first.empty())
            {
                if(tokens[2] == "AUDIO")
                    entry.second = TrackType::AUDIO;
                else if(tokens[2] == "MODE1/2352")
                    entry.second = TrackType::MODE1_2352;
                else if(tokens[2] == "MODE2/2352")
                    entry.second = TrackType::MODE2_2352;
                else if(tokens[2] == "CDI/2352")
                    entry.second = TrackType::CDI_2352;
                else if(tokens[2] == "MODE1/2048")
                    entry.second = TrackType::MODE1_2048;
                else if(tokens[2] == "MODE2/2336")
                    entry.second = TrackType::MODE2_2336;
                else if(tokens[2] == "CDI/2336")
                    entry.second = TrackType::CDI_2336;
                else if(tokens[2] == "CDG")
                    entry.second = TrackType::CDG;
                else if(tokens[2] == "MODE0/2352")
                    entry.second = TrackType::MODE0_2352;
                else if(tokens[2] == "MODE2/2048")
                    entry.second = TrackType::MODE2_2048;
                else if(tokens[2] == "MODE2/2324")
                    entry.second = TrackType::MODE2_2324;
                else
                    entry.second = TrackType::UNKNOWN;
                entries.push_back(entry);
                entry.first.clear();
            }
        }
    }

    return entries;
}


export std::vector<std::pair<int32_t, int32_t>> get_protection_sectors(const Context &ctx, int32_t offset)
{
    std::vector<std::pair<int32_t, int32_t>> protection;

    for(auto const &e : ctx.protection_hard)
        protection.emplace_back(sample_to_lba(e.first, -offset), sample_to_lba(e.second, -offset));

    return protection;
}

}
