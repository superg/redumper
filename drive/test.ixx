module;
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>
#include "throw_line.hh"

export module drive.test;

import cd.cd;
import cd.common;
import cd.subcode;
import cd.toc;
import common;
import drive.mediatek;
import drive.plextor;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

const std::map<READ_CD_ErrorField, std::string> ERROR_FIELD_STRING = {
    { READ_CD_ErrorField::NONE,   "NONE"  },
    { READ_CD_ErrorField::C2,     "C2"    },
    { READ_CD_ErrorField::C2_BEB, "C2BEB" }
};

const std::map<READ_CD_SubChannel, std::string> SUB_CHANNEL_STRING = {
    { READ_CD_SubChannel::NONE, "NONE"  },
    { READ_CD_SubChannel::RAW,  "SUB"   },
    { READ_CD_SubChannel::Q,    "SUBQ"  },
    { READ_CD_SubChannel::RW,   "SUBRW" },
};

const std::map<READ_CDDA_SubCode, std::string> SUB_CODE_STRING = {
    { READ_CDDA_SubCode::DATA,        "DATA"        },
    { READ_CDDA_SubCode::DATA_SUBQ,   "DATA_SUBQ"   },
    { READ_CDDA_SubCode::DATA_SUB,    "DATA_SUB"    },
    { READ_CDDA_SubCode::DATA_C2_SUB, "DATA_C2_SUB" }
};


export int redumper_drive_test(Context &ctx, Options &options)
{
    int exit_code = 0;

    SPTD::Status status;

    std::vector<uint8_t> toc_buffer;
    status = cmd_read_toc(*ctx.sptd, toc_buffer, false, READ_TOC_Format::TOC, 1);
    if(status.status_code)
        throw_line("failed to read TOC, SCSI ({})", SPTD::StatusMessage(status));
    TOC toc(toc_buffer, false);

    std::optional<int32_t> data_lba, audio_lba;
    for(auto const &s : toc.sessions)
        for(auto const &t : s.tracks)
        {
            // data
            if(t.control & (uint8_t)ChannelQ::Control::DATA)
            {
                if(!data_lba)
                    data_lba = t.lba_start;
            }
            // audio
            else
            {
                if(!audio_lba)
                    audio_lba = t.lba_start;
            }

            if(data_lba && audio_lba)
                break;
        }

    if(!data_lba)
        throw_line("no data tracks detected, please use known error free mixed mode disc");
    if(!audio_lba)
        throw_line("no audio tracks detected, please use known error free mixed mode disc");

    std::vector<std::tuple<std::string, int32_t, READ_CD_ExpectedSectorType>> be_tests{
        { "audio",     *audio_lba, READ_CD_ExpectedSectorType::CD_DA     },
        { "data",      *data_lba,  READ_CD_ExpectedSectorType::ALL_TYPES },
        { "scrambled", *data_lba,  READ_CD_ExpectedSectorType::CD_DA     }
    };

    for(auto t : be_tests)
    {
        std::set<std::string> sector_orders;
        for(auto ef : ERROR_FIELD_STRING)
        {
            for(auto sc : SUB_CHANNEL_STRING)
            {
                std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
                status = cmd_read_cd(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), std::get<1>(t), 1, std::get<2>(t), ef.first, sc.first);
                if(status.status_code)
                {
                    if(options.verbose)
                        LOG("[LBA: {:6}] SCSI error ({})", std::get<1>(t), SPTD::StatusMessage(status));
                }
                else
                {
                    // RAW/Q can't be zeroed
                    std::span<const uint8_t> sector_c2_sub(&sector_buffer[CD_DATA_SIZE], CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
                    if(sc.first == READ_CD_SubChannel::RAW || sc.first == READ_CD_SubChannel::Q)
                    {
                        if(std::all_of(sector_c2_sub.begin(), sector_c2_sub.end(), [](uint8_t value) { return value == 0; }))
                        {
                            if(options.verbose)
                                LOG("warning: subcode is zeroed, skipping (sector type: {}, error field: {}, sub channel: {})", std::get<0>(t), ef.second, sc.second);
                            continue;
                        }
                    }

                    std::string message1, message2;
                    if(ef.first != READ_CD_ErrorField::NONE)
                        message1 = std::format("_{}", ef.second);
                    if(sc.first != READ_CD_SubChannel::NONE)
                        message2 = std::format("_{}", sc.second);

                    // detect C2/SUB order
                    if(ef.first != READ_CD_ErrorField::NONE && sc.first != READ_CD_SubChannel::NONE)
                    {
                        std::span<const uint8_t> sector_c2(&sector_buffer[CD_DATA_SIZE], ef.first == READ_CD_ErrorField::C2 ? CD_C2_SIZE : CD_C2BEB_SIZE);

                        if(std::any_of(sector_c2.begin(), sector_c2.end(), [](uint8_t value) { return value != 0; }))
                            message1.swap(message2);
                    }

                    sector_orders.emplace(std::format("DATA{}{}", message1, message2));
                }
            }
        }

        LOG("READ CD (BE) command ({}): {}", std::get<0>(t), sector_orders.empty() ? "no" : "yes");
        for(auto so : sector_orders)
            LOG("  {}", so);
        LOG("");
    }

    for(auto t : be_tests)
    {
        std::set<std::string> sector_orders;
        for(auto ef : ERROR_FIELD_STRING)
        {
            for(auto sc : SUB_CHANNEL_STRING)
            {
                std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
                status = cmd_read_cd_msf(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), LBA_to_MSF(std::get<1>(t)), LBA_to_MSF(std::get<1>(t) + 1), std::get<2>(t), ef.first, sc.first);
                if(status.status_code)
                {
                    if(options.verbose)
                        LOG("[LBA: {:6}] SCSI error ({})", std::get<1>(t), SPTD::StatusMessage(status));
                }
                else
                {
                    // RAW/Q can't be zeroed
                    std::span<const uint8_t> sector_c2_sub(&sector_buffer[CD_DATA_SIZE], CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
                    if(sc.first == READ_CD_SubChannel::RAW || sc.first == READ_CD_SubChannel::Q)
                    {
                        if(std::all_of(sector_c2_sub.begin(), sector_c2_sub.end(), [](uint8_t value) { return value == 0; }))
                        {
                            if(options.verbose)
                                LOG("warning: subcode is zeroed, skipping (sector type: {}, error field: {}, sub channel: {})", std::get<0>(t), ef.second, sc.second);
                            continue;
                        }
                    }

                    std::string message1, message2;
                    if(ef.first != READ_CD_ErrorField::NONE)
                        message1 = std::format("_{}", ef.second);
                    if(sc.first != READ_CD_SubChannel::NONE)
                        message2 = std::format("_{}", sc.second);

                    // detect C2/SUB order
                    if(ef.first != READ_CD_ErrorField::NONE && sc.first != READ_CD_SubChannel::NONE)
                    {
                        std::span<const uint8_t> sector_c2(&sector_buffer[CD_DATA_SIZE], ef.first == READ_CD_ErrorField::C2 ? CD_C2_SIZE : CD_C2BEB_SIZE);

                        if(std::any_of(sector_c2.begin(), sector_c2.end(), [](uint8_t value) { return value != 0; }))
                            message1.swap(message2);
                    }

                    sector_orders.emplace(std::format("DATA{}{}", message1, message2));
                }
            }
        }

        LOG("READ CD MSF (B9) command ({}): {}", std::get<0>(t), sector_orders.empty() ? "no" : "yes");
        for(auto so : sector_orders)
            LOG("  {}", so);
        LOG("");
    }

    for(auto t : be_tests)
    {
        std::set<std::string> sector_orders;
        for(auto ef : ERROR_FIELD_STRING)
        {
            for(auto sc : SUB_CHANNEL_STRING)
            {
                std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
                status = cmd_read_cd_msf_d5(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), LBA_to_MSF(std::get<1>(t)), LBA_to_MSF(std::get<1>(t) + 1), std::get<2>(t), ef.first, sc.first);
                if(status.status_code)
                {
                    if(options.verbose)
                        LOG("[LBA: {:6}] SCSI error ({})", std::get<1>(t), SPTD::StatusMessage(status));
                }
                else
                {
                    // RAW/Q can't be zeroed
                    std::span<const uint8_t> sector_c2_sub(&sector_buffer[CD_DATA_SIZE], CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
                    if(sc.first == READ_CD_SubChannel::RAW || sc.first == READ_CD_SubChannel::Q)
                    {
                        if(std::all_of(sector_c2_sub.begin(), sector_c2_sub.end(), [](uint8_t value) { return value == 0; }))
                        {
                            if(options.verbose)
                                LOG("warning: subcode is zeroed, skipping (sector type: {}, error field: {}, sub channel: {})", std::get<0>(t), ef.second, sc.second);
                            continue;
                        }
                    }

                    std::string message1, message2;
                    if(ef.first != READ_CD_ErrorField::NONE)
                        message1 = std::format("_{}", ef.second);
                    if(sc.first != READ_CD_SubChannel::NONE)
                        message2 = std::format("_{}", sc.second);

                    // detect C2/SUB order
                    if(ef.first != READ_CD_ErrorField::NONE && sc.first != READ_CD_SubChannel::NONE)
                    {
                        std::span<const uint8_t> sector_c2(&sector_buffer[CD_DATA_SIZE], ef.first == READ_CD_ErrorField::C2 ? CD_C2_SIZE : CD_C2BEB_SIZE);

                        if(std::any_of(sector_c2.begin(), sector_c2.end(), [](uint8_t value) { return value != 0; }))
                            message1.swap(message2);
                    }

                    sector_orders.emplace(std::format("DATA{}{}", message1, message2));
                }
            }
        }

        LOG("READ CD MSF (D5) command ({}): {}", std::get<0>(t), sector_orders.empty() ? "no" : "yes");
        for(auto so : sector_orders)
            LOG("  {}", so);
        LOG("");
    }

    std::vector<std::tuple<std::string, int32_t>> d8_tests{
        { "audio", *audio_lba },
        { "data",  *data_lba  }
    };

    bool d8 = false;
    for(auto t : d8_tests)
    {
        std::set<std::string> sector_orders;
        for(auto sc : SUB_CODE_STRING)
        {
            std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2_SIZE + CD_SUBCODE_SIZE);
            status = cmd_read_cdda(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), std::get<1>(t), 1, sc.first);
            if(status.status_code)
            {
                if(options.verbose)
                    LOG("[LBA: {:6}] SCSI error ({})", std::get<1>(t), SPTD::StatusMessage(status));
            }
            else
            {
                sector_orders.emplace(sc.second);

                if(sc.first == READ_CDDA_SubCode::DATA_SUB)
                    d8 = true;
            }
        }

        LOG("READ CDDA (D8) command ({}): {}", std::get<0>(t), sector_orders.empty() ? "no" : "yes");
        for(auto so : sector_orders)
            LOG("  {}", so);
        LOG("");
    }

    bool plextor_leadin = false;
    if(!options.drive_test_skip_plextor_leadin && d8)
    {
        LOG("PLEXTOR: attempting to read lead-in");
        auto leadin = plextor_leadin_read(*ctx.sptd, 0);
        auto errors_count = std::count_if(leadin.begin(), leadin.end(), [](const std::pair<SPTD::Status, std::vector<uint8_t>> &value) { return value.first.status_code != 0; });

        plextor_leadin = errors_count < leadin.size();

        LOG("");
    }
    LOG("PLEXTOR lead-in: {}", options.drive_test_skip_plextor_leadin ? "skipped" : (plextor_leadin ? "yes" : "no"));
    uint32_t pregap_count = 0;
    for(int32_t lba = plextor_leadin ? -75 : -135; lba < 0; ++lba)
    {
        std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
        status = cmd_read_cd(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), lba, 1, READ_CD_ExpectedSectorType::ALL_TYPES, READ_CD_ErrorField::NONE, READ_CD_SubChannel::NONE);
        if(status.status_code)
        {
            if(options.verbose)
                LOG("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
        }
        else
        {
            ++pregap_count;
        }
    }
    LOG("lead-in/pre-gap: {}", pregap_count ? std::format("{} sectors", pregap_count) : "no");

    uint32_t leadout_count = 0;
    for(uint32_t i = 0; i < OVERREAD_COUNT + 10; ++i)
    {
        int32_t lba = toc.sessions.back().tracks.back().lba_start + i;

        std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2BEB_SIZE + CD_SUBCODE_SIZE);
        status = cmd_read_cd(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), lba, 1, READ_CD_ExpectedSectorType::CD_DA, READ_CD_ErrorField::NONE, READ_CD_SubChannel::NONE);
        if(status.status_code)
            status = cmd_read_cd(*ctx.sptd, sector_buffer.data(), sector_buffer.size(), lba, 1, READ_CD_ExpectedSectorType::ALL_TYPES, READ_CD_ErrorField::NONE, READ_CD_SubChannel::NONE);

        if(status.status_code)
        {
            if(options.verbose)
                LOG("[LBA: {:6}] SCSI error ({})", lba, SPTD::StatusMessage(status));
            break;
        }
        else
        {
            ++leadout_count;
        }
    }
    bool leadout_more = leadout_count > OVERREAD_COUNT;
    if(leadout_more)
        leadout_count = OVERREAD_COUNT;
    LOG("lead-out: {}", leadout_count ? std::format("{}{} sectors", leadout_count, leadout_more ? "+" : "") : "no");

    bool mt_cache_read = false;
    if(!options.drive_test_skip_cache_read)
    {
        std::vector<uint8_t> cache;
        status = mediatek_cache_read(*ctx.sptd, cache, 32 * CHUNK_1MB);
        if(status.status_code)
        {
            if(options.verbose)
                LOG("read cache failed, SCSI ({})", SPTD::StatusMessage(status));
        }
        else
        {
            uint32_t cache_size = mediatek_find_cache_size(cache, 256 * 1024, 95);

            LOG("MEDIATEK memory space: {}Mb", cache_size / 1024 / 1024);

            mt_cache_read = true;
        }
    }
    LOG("MEDIATEK cache read (F1): {}", options.drive_test_skip_cache_read ? "skipped" : (mt_cache_read ? "yes" : "no"));

    return exit_code;
}

}
