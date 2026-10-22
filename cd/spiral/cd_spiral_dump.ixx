module;
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "throw_line.hh"



export module cd.spiral;

import cd.cd;
import cd.common;
import cd.subcode;
import common;
import drive;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.file_io;
import utils.logger;
import utils.signal;



namespace gpsxre
{

constexpr uint16_t C4_TIMEOUT_MS = 5000;


void seek_spiral_start(SPTD &sptd, const Options &options)
{
    constexpr int32_t INWARD_SEEK_STEP = 50;
    constexpr int32_t OUTWARD_SEEK_STEP = 1;
    constexpr uint32_t MAX_SEEKS = 0x7FFF;

    LOG("seeking inner pickup limit");
    SpiralDrive_SeekResult seek_result = SpiralDrive_SeekResult::NONE;
    for(uint32_t i = 0; i < MAX_SEEKS; ++i)
    {
        seek_result = SpiralDrive_SeekResult::NONE;
        if(auto status = cmd_spiraldrive(sptd, (uint8_t *)&seek_result, sizeof(seek_result), SpiralDrive_Operation::SEEK, C4_TIMEOUT_MS, -INWARD_SEEK_STEP); status.status_code)
            throw_line("inward SEEK failed (step: {}, iteration: {}, SCSI: {})", INWARD_SEEK_STEP, i + 1, SPTD::StatusMessage(status));

        if(seek_result == SpiralDrive_SeekResult::NONE)
        {
            if(options.verbose)
                LOG("inward SEEK success (step: {}, iteration: {})", INWARD_SEEK_STEP, i + 1);
        }
        else if(seek_result == SpiralDrive_SeekResult::INNER_LIMIT)
        {
            LOG("inward SEEK inner limit reached (step: {}, iterations: {})", INWARD_SEEK_STEP, i + 1);
            break;
        }
        else
            throw_line("inward SEEK returned an invalid result (result: {}, step: {}, iteration: {})", (uint8_t)seek_result, INWARD_SEEK_STEP, i + 1);
    }
    if(seek_result != SpiralDrive_SeekResult::INNER_LIMIT)
        throw_line("inward SEEK inner limit was not reached (step: {}, iteration: {})", INWARD_SEEK_STEP, MAX_SEEKS);

    LOG("seeking readable start");
    bool read_success = false;
    std::vector<uint8_t> buffer(CD_RAW_DATA_SIZE);
    for(uint32_t i = 0; i < MAX_SEEKS; ++i)
    {
        // seek
        if(i)
        {
            if(auto status = cmd_spiraldrive(sptd, (uint8_t *)&seek_result, sizeof(seek_result), SpiralDrive_Operation::SEEK, C4_TIMEOUT_MS, OUTWARD_SEEK_STEP); status.status_code)
                throw_line("outward SEEK failed (step: {}, SCSI: {})", OUTWARD_SEEK_STEP, SPTD::StatusMessage(status));
            if(seek_result != SpiralDrive_SeekResult::NONE)
                throw_line("outward SEEK returned an invalid result (result: {}, step: {}, iteration: {})", (uint8_t)seek_result, OUTWARD_SEEK_STEP, i + 1);

            if(options.verbose)
                LOG("outward SEEK success (step: {}, iteration: {})", OUTWARD_SEEK_STEP, i + 1);
        }

        // read
        if(auto status = cmd_spiraldrive(sptd, buffer.data(), (uint32_t)buffer.size(), SpiralDrive_Operation::READ, C4_TIMEOUT_MS, 1); status.status_code)
        {
            if(options.verbose)
                LOG("outward READ failed (step: {}, iteration: {}, SCSI: {})", OUTWARD_SEEK_STEP, i + 1, SPTD::StatusMessage(status));
        }
        else
        {
            LOG("outward READ success (step: {}, iterations: {})", OUTWARD_SEEK_STEP, i + 1);
            read_success = true;
            break;
        }
    }

    if(!read_success)
        throw_line("outward SEEK readable start was not reached (step: {}, iteration: {})", OUTWARD_SEEK_STEP, MAX_SEEKS);
}


export int redumper_spiral_dump(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    {
        auto spiraldrive_version = is_spiraldrive_firmware(ctx.drive_config);
        if(spiraldrive_version)
        {
            if(spiraldrive_version < spiraldrive_minimum_version())
                throw_line("spiral::dump requires SpiralDrive firmware {} or newer", spiraldrive_version_string(spiraldrive_minimum_version()));
        }
        else
            throw_line("spiral::dump requires a drive running SpiralDrive firmware");
    }

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    if(!options.overwrite && (std::filesystem::exists(image_prefix + ".main.raw") || std::filesystem::exists(image_prefix + ".c2.raw") || std::filesystem::exists(image_prefix + ".sub.raw")))
        throw_line("dump already exists (image name: {})", options.image_name);

    std::fstream fs_main(image_prefix + ".main.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_c2(image_prefix + ".c2.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_sub(image_prefix + ".sub.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    fs_main.exceptions(std::ios::badbit | std::ios::failbit);
    fs_c2.exceptions(std::ios::badbit | std::ios::failbit);
    fs_sub.exceptions(std::ios::badbit | std::ios::failbit);

    std::vector<uint8_t> sector(CD_RAW_DATA_SIZE);
    std::span<uint8_t> sector_data(sector.data(), CD_DATA_SIZE);
    std::span<uint8_t> sector_c2(sector.data() + CD_DATA_SIZE, CD_C2_SIZE);
    std::span<uint8_t> sector_subcode(sector.data() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);

    seek_spiral_start(*ctx.sptd, options);

    uint32_t errors_c2 = 0;
    uint32_t errors_q = 0;
    std::optional<std::pair<int32_t, uint32_t>> q_lba;
    std::optional<int32_t> lba_end;
    if(options.spiraldrive_dreamcast)
        lba_end = 45000;

    SignalINT signal;
    for(uint32_t lba_index = 0; !signal.interrupt(); ++lba_index)
    {
        if(q_lba)
        {
            int32_t lba = q_lba->first + (int32_t)(lba_index - q_lba->second);

            if(options.lba_end)
            {
                if(lba >= *options.lba_end)
                    break;
            }
            else if(lba_end)
            {
                if(lba >= *lba_end)
                    break;
            }
        }

        auto status = cmd_spiraldrive(*ctx.sptd, sector.data(), CD_RAW_DATA_SIZE, SpiralDrive_Operation::READ, C4_TIMEOUT_MS, 1);
        if(status.status_code)
        {
            std::fill(sector_data.begin(), sector_data.end(), 0);
            std::fill(sector_c2.begin(), sector_c2.end(), 0xFF);
            std::fill(sector_subcode.begin(), sector_subcode.end(), 0);
            
            LOG_R("READ failed (SCSI: {})", SPTD::StatusMessage(status));
        }

        ChannelQ q = subcode_extract_q(sector_subcode.data());
        if(q.isValid())
        {
            if(q.adr == 1)
            {
                if(q.mode1.tno)
                {
                    q_lba = { BCDMSF_to_LBA(q.mode1.a_msf), lba_index };
                }
                // lead-in
                else
                {
                    if(q.mode1.point_index == 0xA2)
                    {
                        int32_t lba = BCDMSF_to_LBA(q.mode1.a_msf);
                        if(!lba_end || *lba_end < lba)
                        {
                            lba_end = lba + OVERREAD_COUNT;
                            LOG_R("session lead-out detected (LBA: {:6})", lba);
                        }
                    }
                }
            }
        }
        else
            ++errors_q;

        if(auto c2_bits = c2_bits_count(sector_c2); c2_bits)
        {
            ++errors_c2;

            if(options.verbose)
                LOGC_R("[LBA: {:>6}] C2 error (bits: {:4})", q_lba ? q_lba->first + (int32_t)(lba_index - q_lba->second) : LBA_START, c2_bits);
        }

        write_entry_bit(fs_main, sector_data.data(), sector_data.size(), lba_index, 1, 0, false);
        write_entry_bit(fs_c2, sector_c2.data(), sector_c2.size(), lba_index, 1, 0, true);
        write_entry_bit(fs_sub, sector_subcode.data(), sector_subcode.size(), lba_index, 1, 0, false);

        LOGC_RF("{} sector: {:6}, LBA: {:>6}, errors: {{ C2: {}, Q: {} }}", spinner_animation(), lba_index, q_lba ? q_lba->first + (int32_t)(lba_index - q_lba->second) : LBA_START,
            errors_c2, errors_q);
    }

    LOGC_RF("");
    LOGC("");

    // report close failures and finish cleanup before raising an interrupt
    fs_main.close();
    fs_c2.close();
    fs_sub.close();

    LOG("media errors: ");
    LOG("  C2: {}", errors_c2);
    LOG("  Q: {}", errors_q);

    if(signal.interrupt())
        signal.raiseDefault();

    return exit_code;
}
}
