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

import cd.toc;
import common;
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
    { READ_CD_SubChannel::PW,   "SUBPW" },
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
                status = cmd_read_cd(*ctx.sptd, sector_buffer.data(), std::get<1>(t), sector_buffer.size(), 1, std::get<2>(t), ef.first, sc.first);
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

        LOG("BE read command ({}): {}", std::get<0>(t), sector_orders.empty() ? "no" : "yes");
        for(auto so : sector_orders)
            LOG("  {}", so);
        LOG("");
    }

    std::vector<std::tuple<std::string, int32_t>> d8_tests{
        { "audio", *audio_lba },
        { "data",  *data_lba }
    };

    for(auto t : d8_tests)
    {
        std::set<std::string> sector_orders;
        for(auto sc : SUB_CODE_STRING)
        {
            std::vector<uint8_t> sector_buffer(CD_DATA_SIZE + CD_C2_SIZE + CD_SUBCODE_SIZE);
            status = cmd_read_cdda(*ctx.sptd, sector_buffer.data(), std::get<1>(t), sector_buffer.size(), 1, sc.first);
            if(status.status_code)
            {
                if(options.verbose)
                    LOG("[LBA: {:6}] SCSI error ({})", std::get<1>(t), SPTD::StatusMessage(status));
            }
            else
            {
                sector_orders.emplace(sc.second);
            }
        }

        LOG("D8 read command ({}): {}", std::get<0>(t), sector_orders.empty() ? "no" : "yes");
        for(auto so : sector_orders)
            LOG("  {}", so);
        LOG("");
    }

    return exit_code;
}

}
