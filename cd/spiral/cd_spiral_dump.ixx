module;
#include <algorithm>
#include <cstdint>
#include <cstring>
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
// wobble envelope, slot N covers the Nth 24th of the time from the previous frame event to this one
constexpr uint32_t C4_WBL_SIZE = 24;
// the wobble of a frame is sampled this many sectors ahead of its subcode, measured only on PX-W5224A
constexpr uint32_t C4_WBL_LEAD_SECTORS = 2;
constexpr uint32_t C4_FRAME_SIZE = CD_RAW_DATA_SIZE + sizeof(ChannelQ) + C4_WBL_SIZE;


void seek_spiral_start(SPTD &sptd)
{
    // the firmware moves to the first lead-in pits of pressed media or to the ATIP address before the lead-in of recordable media
    LOG("seeking spiral start");
    if(auto status = cmd_spiraldrive(sptd, nullptr, 0, SpiralDrive_Operation::REWIND, 0, 0); status.status_code)
        throw_line("REWIND failed (SCSI: {})", SPTD::StatusMessage(status));
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
    if(!options.overwrite
        && (std::filesystem::exists(image_prefix + ".main.raw") || std::filesystem::exists(image_prefix + ".c2.raw") || std::filesystem::exists(image_prefix + ".sub.raw")
            || std::filesystem::exists(image_prefix + ".subq.raw") || std::filesystem::exists(image_prefix + ".wbl.raw")))
        throw_line("dump already exists (image name: {})", options.image_name);

    seek_spiral_start(*ctx.sptd);

    std::fstream fs_main(image_prefix + ".main.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_c2(image_prefix + ".c2.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_sub(image_prefix + ".sub.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_subq(image_prefix + ".subq.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_wbl(image_prefix + ".wbl.raw", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    fs_main.exceptions(std::ios::badbit | std::ios::failbit);
    fs_c2.exceptions(std::ios::badbit | std::ios::failbit);
    fs_sub.exceptions(std::ios::badbit | std::ios::failbit);
    fs_subq.exceptions(std::ios::badbit | std::ios::failbit);
    fs_wbl.exceptions(std::ios::badbit | std::ios::failbit);

    std::vector<uint8_t> sector(C4_FRAME_SIZE);
    std::span<uint8_t> sector_data(sector.data(), CD_DATA_SIZE);
    std::span<uint8_t> sector_c2(sector.data() + CD_DATA_SIZE, CD_C2_SIZE);
    std::span<uint8_t> sector_sub(sector.data() + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);
    std::span<uint8_t> sector_subq(sector.data() + CD_RAW_DATA_SIZE, sizeof(ChannelQ));
    std::span<uint8_t> sector_wbl(sector.data() + CD_RAW_DATA_SIZE + sizeof(ChannelQ), C4_WBL_SIZE);

    uint32_t errors_c2 = 0;
    uint32_t errors_q = 0;
    std::optional<std::pair<int32_t, uint32_t>> q_lba;
    std::optional<std::pair<int32_t, uint32_t>> q_candidate;
    std::optional<int32_t> lba_end;
    if(options.spiraldrive_dreamcast)
        lba_end = 45000;

    bool q_mismatch_message = false;
    bool last_session_message = false;
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

        auto status = cmd_spiraldrive(*ctx.sptd, sector.data(), (uint32_t)sector.size(), SpiralDrive_Operation::READ, C4_TIMEOUT_MS, 1);
        if(status.status_code)
        {
            std::fill(sector_data.begin(), sector_data.end(), 0);
            std::fill(sector_c2.begin(), sector_c2.end(), 0xFF);
            std::fill(sector_sub.begin(), sector_sub.end(), 0);
            std::fill(sector_subq.begin(), sector_subq.end(), 0);
            std::fill(sector_wbl.begin(), sector_wbl.end(), 0);

            LOG_R("READ failed (SCSI: {})", SPTD::StatusMessage(status));
        }

        auto &q = (ChannelQ &)sector_subq[0];
        if(q.isValid())
        {
            if(!q_mismatch_message && !subcode_extract_q(sector_sub.data()).isValid())
            {
                LOG_R("raw subcode mismatch (LBA: {})", q_lba ? q_lba->first + (int32_t)(lba_index - q_lba->second) : LBA_START);
                q_mismatch_message = true;
            }

            if(q.adr == 1)
            {
                // a junk frame with a valid CRC, as recorded in the PCA, must not anchor the position, so two addresses have to agree
                if(q.mode1.tno)
                {
                    int32_t lba = BCDMSF_to_LBA(q.mode1.a_msf);
                    if(q_candidate && lba - q_candidate->first == (int32_t)(lba_index - q_candidate->second))
                        q_lba = { lba, lba_index };
                    q_candidate = { lba, lba_index };
                }
                // lead-in
                else
                {
                    if(q.mode1.point_index == 0xA2)
                    {
                        int32_t lba = BCDMSF_to_LBA(q.mode1.a_msf);
                        if(!lba_end || *lba_end < lba)
                        {
                            lba_end = lba + options.spiraldrive_leadout_sectors;
                            LOG_R("session lead-out detected (LBA: {:6})", lba);
                        }
                    }
                }
            }
            else if(q.adr == 5)
            {
                if(!q.mode1.tno && q.mode1.point_index == 0xB0)
                {
                    if(q.mode1.msf.m == 0xFF && q.mode1.msf.s == 0xFF && q.mode1.msf.f == 0xFF)
                    {
                        if(!last_session_message)
                        {
                            LOG_R("last session detected");
                            last_session_message = true;
                        }
                    }
                    else
                    {
                        int32_t lba = BCDMSF_to_LBA(q.mode1.msf);
                        if(!lba_end || *lba_end < lba)
                        {
                            lba_end = lba;
                            LOG_R("next session program area detected (LBA: {:6})", lba);
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
        write_entry_bit(fs_sub, sector_sub.data(), sector_sub.size(), lba_index, 1, 0, false);
        write_entry_bit(fs_subq, sector_subq.data(), sector_subq.size(), lba_index, 1, 0, false);
        write_entry_bit(fs_wbl, sector_wbl.data(), sector_wbl.size(), lba_index + C4_WBL_LEAD_SECTORS, 1, 0, false);

        LOGC_RF("{} sector: {:6}, LBA: {:>6}, errors: {{ C2: {}, Q: {} }}", spinner_animation(), lba_index, q_lba ? q_lba->first + (int32_t)(lba_index - q_lba->second) : LBA_START, errors_c2,
            errors_q);
    }

    LOGC_RF("");
    LOGC("");

    // report close failures and finish cleanup before raising an interrupt
    fs_main.close();
    fs_c2.close();
    fs_sub.close();
    fs_subq.close();
    fs_wbl.close();

    LOG("media errors: ");
    LOG("  C2: {}", errors_c2);
    LOG("  Q: {}", errors_q);

    if(signal.interrupt())
        signal.raiseDefault();

    return exit_code;
}
}
