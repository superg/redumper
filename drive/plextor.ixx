module;
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include <vector>
#include "throw_line.hh"

export module drive.plextor;

import cd.cd;
import cd.common;
import cd.subcode;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

// Plextor firmware blocked LBA ranges:
// BE [-inf .. -20000], (-1000 .. -75)
// D8 [-inf .. -20150], (-1150 .. -75)
//
// BE is having trouble reading LBA close to -1150].
// It is possible to read large negative offset ranges (starting from 0x80000000 - smallest negative 32-bit integer) using BE command with disabled C2 if the disc first track is a data track.
// D8 range boundaries are shifted left by 150 sectors comparing to BE.
//
// Regardless of the read command and a starting point, negative range reads are virtualized by Plextor drive and will always start somewhere in the lead-in TOC area and sequentially read until
// eventually the drive will wrap around and start in the lead-in TOC area again. This process will continue until drive reaches firmware blocked LBA ranges specified here. However, any external
// pause between sequential reads (debugger pause, sleep() call etc.) will lead to another wrap around.
// For multisession CD, there is no direct control which session lead-in will be chosen. Usually it's the first session, sometimes it's the latter ones.
//
// The following range, while preserving the above behavior, is unlocked for both BE and D8 commands with disabled C2:
const std::pair<int32_t, int32_t> PLEXTOR_TOC_RANGE = { -20150, -1150 };


export using PlextorLeadIn = std::vector<std::pair<SPTD::Status, std::vector<uint8_t>>>;


export PlextorLeadIn plextor_leadin_read(SPTD &sptd, uint32_t tail_size)
{
    PlextorLeadIn sectors;

    int32_t neg_start = PLEXTOR_TOC_RANGE.first + 1;
    int32_t neg_limit = PLEXTOR_TOC_RANGE.second + 1;
    int32_t neg_end = neg_limit;

    for(int32_t neg = neg_start; neg < neg_end; ++neg)
    {
        LOGC_RF("{} [LBA: {}]", spinner_animation(), neg);

        std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
        SPTD::Status status = cmd_read_cdda(sptd, sector_buffer.data(), neg, sector_buffer.size(), 1, READ_CDDA_SubCode::DATA_SUB);

        if(!status.status_code)
        {
            std::span<const uint8_t> sector_subcode(&sector_buffer[CD_DATA_SIZE], CD_SUBCODE_SIZE);

            ChannelQ Q = subcode_extract_q(sector_subcode.data());

            // DEBUG
            // Logger::get().carriageReturn();
            // LOGC("{}", Q.Decode());

            if(Q.isValid() && Q.adr == 1 && Q.mode1.tno && neg_end == neg_limit)
                neg_end = neg + tail_size;
        }

        sectors.emplace_back(status, sector_buffer);
    }

    LOGC_RF("");

    return sectors;
}


export std::optional<std::pair<int32_t, int32_t>> plextor_leadin_identify(const PlextorLeadIn &leadin)
{
    std::optional<std::pair<int32_t, int32_t>> lba_range;

    for(auto const &s : leadin)
    {
        if(s.first.status_code)
            continue;

        std::span<const uint8_t> sector_subcode(&s.second[CD_DATA_SIZE], CD_SUBCODE_SIZE);

        ChannelQ Q = subcode_extract_q(sector_subcode.data());
        if(Q.isValid() && Q.adr == 1 && Q.mode1.tno)
        {
            int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
            if(lba_range)
                lba_range->second = lba;
            else
                lba_range.emplace(lba, lba);
        }
    }

    return lba_range;
}


export bool plextor_leadin_match(const PlextorLeadIn &leadin1, const PlextorLeadIn &leadin2)
{
    bool match = true;

    auto sectors_count = (uint32_t)std::min(leadin1.size(), leadin2.size());
    for(uint32_t i = 1; i <= sectors_count; ++i)
    {
        auto const &s1 = leadin1[leadin1.size() - i];
        auto const &s2 = leadin2[leadin2.size() - i];
        if(s1.first.status_code || s2.first.status_code)
            continue;

        std::span<const uint8_t> sector1_data(&s1.second[0], CD_DATA_SIZE);
        std::span<const uint8_t> sector1_subcode(&s1.second[CD_DATA_SIZE], CD_SUBCODE_SIZE);
        std::span<const uint8_t> sector2_data(&s2.second[0], CD_DATA_SIZE);
        std::span<const uint8_t> sector2_subcode(&s2.second[CD_DATA_SIZE], CD_SUBCODE_SIZE);

        // data check
        if(!std::equal(sector1_data.begin(), sector1_data.end(), sector2_data.begin()))
        {
            match = false;
            break;
        }

        ChannelQ Q1 = subcode_extract_q(sector1_subcode.data());
        ChannelQ Q2 = subcode_extract_q(sector2_subcode.data());
        if(!Q1.isValid() || !Q2.isValid())
            continue;

        // subcode check
        if(!std::equal(Q1.raw, Q1.raw + sizeof(Q1.raw), Q2.raw))
        {
            match = false;
            break;
        }
    }

    return match;
}


export int32_t plextor_leadin_find_start(const PlextorLeadIn &leadin, std::fstream &fs_subcode, uint32_t leadin_overlap)
{
    std::optional<int32_t> lba_base;
    for(uint32_t i = 0; i < leadin.size(); ++i)
    {
        std::span<const uint8_t> sector_subcode_leadin(&leadin[i].second[CD_DATA_SIZE], CD_SUBCODE_SIZE);

        ChannelQ Q = subcode_extract_q(sector_subcode_leadin.data());
        if(Q.isValid() && Q.adr == 1 && Q.mode1.tno)
        {
            lba_base = BCDMSF_to_LBA(Q.mode1.a_msf) - i;
            break;
        }
    }
    if(!lba_base)
        throw_line("unexpected");

    uint32_t sectors_count = leadin_overlap + leadin.size() + leadin_overlap;
    std::vector<uint8_t> subcode_buffer_file(sectors_count * CD_SUBCODE_SIZE);
    read_entry(fs_subcode, subcode_buffer_file.data(), CD_SUBCODE_SIZE, *lba_base - leadin_overlap - LBA_START, sectors_count, 0, 0);

    std::vector<uint32_t> score(leadin_overlap * 2);
    for(uint32_t i = 0; i < score.size(); ++i)
    {
        for(uint32_t j = 0; j < leadin.size(); ++j)
        {
            std::span<const uint8_t> sector_subcode_leadin(&leadin[j].second[CD_DATA_SIZE], CD_SUBCODE_SIZE);
            std::span<const uint8_t> sector_subcode_file(&subcode_buffer_file[(j + i) * CD_SUBCODE_SIZE], CD_SUBCODE_SIZE);
            ChannelQ Q = subcode_extract_q(sector_subcode_leadin.data());
            ChannelQ Q_file = subcode_extract_q(sector_subcode_file.data());

            if(!Q.isValid() || !Q_file.isValid())
                continue;

            if(std::equal(Q.raw, Q.raw + sizeof(Q.raw), Q_file.raw))
                ++score[i];
        }
    }

    if(auto it = std::max_element(score.begin(), score.end()); it != score.end() && *it)
        *lba_base += std::distance(score.begin(), it) - leadin_overlap;

    return *lba_base;
}

}
