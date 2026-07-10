module;
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

export module drive.detect;

import cd.cd;
import cd.common;
import cd.subcode;
import cd.toc;
import common;
import drive;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.logger;
import utils.strings;


namespace gpsxre
{


export bool detect_sector_order(SPTD &sptd, DriveConfig &drive_config, int32_t lba)
{
    std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);

    for(auto const &order : SECTOR_ORDER_STRING)
    {
        auto test_layout = sector_order_layout(order.first);

        auto error_field = test_layout.c2_offset == CD_RAW_DATA_SIZE ? READ_CD_ErrorField::NONE : READ_CD_ErrorField::C2;
        auto sub_channel = test_layout.subcode_offset == CD_RAW_DATA_SIZE ? READ_CD_SubChannel::NONE : READ_CD_SubChannel::RAW;

        // try read as-audio first, then fallback to as-data
        auto status = cmd_read_cd(sptd, sector_buffer.data(), CD_RAW_DATA_SIZE, lba, 1, READ_CD_ExpectedSectorType::CD_DA, error_field, sub_channel);
        if(status.status_code)
            status = cmd_read_cd(sptd, sector_buffer.data(), CD_RAW_DATA_SIZE, lba, 1, READ_CD_ExpectedSectorType::ALL_TYPES, error_field, sub_channel);
        if(status.status_code)
            continue;

        // validate by checking subcode Q CRC at the expected offset
        if(test_layout.subcode_offset != CD_RAW_DATA_SIZE)
        {
            if(subcode_extract_q(sector_buffer.data() + test_layout.subcode_offset).isValid())
            {
                // C2 should be zeroed assuming a clean first sector
                if(test_layout.c2_offset != CD_RAW_DATA_SIZE)
                {
                    auto c2_start = sector_buffer.data() + test_layout.c2_offset;
                    if(std::any_of(c2_start, c2_start + CD_C2_SIZE, [](uint8_t v) { return v != 0; }))
                        continue;
                }

                drive_config.sector_order = order.first;
                LOG("GENERIC: auto-detected sector order: {}", order.second);
                return true;
            }
        }
        else
        {
            // no subcode, validate DATA_C2 order
            if(test_layout.c2_offset != CD_RAW_DATA_SIZE)
            {
                auto c2_start = sector_buffer.data() + test_layout.c2_offset;
                if(std::all_of(c2_start, c2_start + CD_C2_SIZE, [](uint8_t v) { return v == 0; }))
                {
                    drive_config.sector_order = order.first;
                    LOG("GENERIC: auto-detected sector order: {} (no subcode validation)", order.second);
                    return true;
                }
            }
        }
    }

    return false;
}


export int redumper_drive_detect(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    if(ctx.drive_config.type == Type::GENERIC && ctx.drive_config.read_method == ReadMethod::BE && !options.drive_sector_order)
    {
        auto toc_buffer = toc_read(*ctx.sptd);
        TOC toc(toc_buffer, false);
        int32_t lba = toc.sessions.front().tracks.front().lba_start;
        if(!detect_sector_order(*ctx.sptd, ctx.drive_config, lba))
            LOG("warning: sector order detection failed, using default");
        LOG("");
    }

    return exit_code;
}

}
