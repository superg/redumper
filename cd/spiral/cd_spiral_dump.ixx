module;
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
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
import utils.file_io;
import utils.logger;
import utils.signal;



namespace gpsxre
{

namespace
{

std::optional<int32_t> position_q_lba(const uint8_t *frame)
{
    auto q = subcode_extract_q(frame + CD_DATA_SIZE + CD_C2_SIZE);
    if(!q.isValid() || q.adr != 1 || q.mode1.tno == 0 || !BCDMSF_valid(q.mode1.a_msf))
        return std::nullopt;
    if(q.mode1.tno != CD_LEADOUT_TRACK_NUMBER && (q.mode1.tno > 0x99 || (q.mode1.tno & 0x0f) > 9))
        return std::nullopt;

    return BCDMSF_to_LBA(q.mode1.a_msf);
}


void check_good_transfer(const std::pair<SPTD::Status, uint32_t> &result, uint32_t expected)
{
    if(!result.first.status_code && result.second != expected)
        throw_line("short transfer (expected: {}, transferred: {})", expected, result.second);
}


void seek_spiral_start(SPTD &sptd, SignalINT &signal)
{
    constexpr uint16_t C4_TIMEOUT_MS = 5000;
    constexpr int32_t INWARD_SEEK_STEP = 50;
    constexpr uint32_t MAX_INWARD_SEEKS = 32767;
    constexpr int32_t MAX_SEEK_DISPLACEMENT = 32767;

    LOG("seeking inner pickup limit");
    int32_t inward_step = INWARD_SEEK_STEP;
    SpiralDrive_SeekResult seek_result = SpiralDrive_SeekResult::OK;
    for(uint32_t i = 0; i < MAX_INWARD_SEEKS && seek_result != SpiralDrive_SeekResult::INNER_LIMIT; ++i)
    {
        if(signal.interrupt())
            signal.raiseDefault();

        auto result = cmd_spiraldrive(sptd, (uint8_t *)&seek_result, sizeof(seek_result), SpiralDrive_Operation::SEEK, C4_TIMEOUT_MS, -inward_step);
        if(result.first.status_code)
            throw_line("inward SEEK({}) failed (SCSI: {})", -inward_step, SPTD::StatusMessage(result.first));
        check_good_transfer(result, sizeof(seek_result));

        if(seek_result == SpiralDrive_SeekResult::OK)
        {
            LOG("inward SEEK success (iteration: {}, step: {})", i + 1, inward_step);
        }
        else if(seek_result == SpiralDrive_SeekResult::FAILED)
        {
            if(inward_step == 1)
                throw_line("inward SEEK({}) failed; inner pickup limit is not confirmed", -inward_step);
            int32_t failed_step = inward_step;
            inward_step /= 2;
            LOG("inward SEEK({}) failed; reducing step to {}", -failed_step, -inward_step);
        }
        else if(seek_result != SpiralDrive_SeekResult::INNER_LIMIT)
            throw_line("invalid SEEK result ({})", (uint8_t)seek_result);
    }
    if(seek_result != SpiralDrive_SeekResult::INNER_LIMIT)
        throw_line("inner pickup limit was not reached after {} seeks", MAX_INWARD_SEEKS);

    std::vector<uint8_t> buffer(CD_RAW_DATA_SIZE);
    for(int32_t displacement = 0; displacement <= MAX_SEEK_DISPLACEMENT; ++displacement)
    {
        if(signal.interrupt())
            signal.raiseDefault();
        if(displacement)
        {
            auto result = cmd_spiraldrive(sptd, (uint8_t *)&seek_result, sizeof(seek_result), SpiralDrive_Operation::SEEK, C4_TIMEOUT_MS, 1);
            if(result.first.status_code)
                throw_line("outward SEEK failed (displacement: {}, SCSI: {})", displacement, SPTD::StatusMessage(result.first));
            check_good_transfer(result, sizeof(seek_result));
            if(seek_result != SpiralDrive_SeekResult::OK)
                throw_line("outward SEEK failed (displacement: {}, result: {})", displacement, (uint8_t)seek_result);
        }

        auto result = cmd_spiraldrive(sptd, buffer.data(), (uint32_t)buffer.size(), SpiralDrive_Operation::READ, C4_TIMEOUT_MS, 1);
        if(!result.first.status_code)
        {
            check_good_transfer(result, (uint32_t)buffer.size());
            LOG("readable start {} controller units outward from inner pickup limit", displacement);
            return;
        }
        const auto &status = result.first;
        if(status.status_code != 2 || (status.sense_key != 3 && status.sense_key != 4))
            throw_line("start probe failed (displacement: {}, SCSI: {})", displacement, SPTD::StatusMessage(status));
        LOGC_RF("searching for readable lead-in (outward displacement: {}, SCSI: {})", displacement, SPTD::StatusMessage(status));
    }

    throw_line("found no readable start within {} controller units of inner pickup limit", MAX_SEEK_DISPLACEMENT);
}

}


export int redumper_spiral_dump(Context &ctx, Options &options)
{
    constexpr uint16_t C4_TIMEOUT_MS = 1000;
    constexpr uint32_t SPIRAL_READ_FRAMES = 16;
    constexpr uint32_t SPIRAL_MAX_CONSECUTIVE_READ_ERRORS = 16;
    constexpr uint32_t SPIRAL_MAX_STAGED_FRAMES = 100000;

    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    auto spiraldrive_version = is_spiraldrive_firmware(ctx.drive_config);
    if(spiraldrive_version)
    {
        if(spiraldrive_version < spiraldrive_minimum_version())
            throw_line("spiral::dump requires SpiralDrive firmware {} or newer", spiraldrive_version_string(spiraldrive_minimum_version()));
    }
    else
        throw_line("spiral::dump requires a drive running SpiralDrive firmware");

    image_check_overwrite(options);

    int32_t lba_end = options.lba_end ? *options.lba_end : std::numeric_limits<int32_t>::max();

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    auto mode = std::fstream::out | std::fstream::binary | std::fstream::trunc;
    std::fstream fs_scram(image_prefix + ".scram", mode);
    std::fstream fs_state(image_prefix + ".state", mode);
    std::fstream fs_subcode(image_prefix + ".subcode", mode);
    if(!fs_scram || !fs_state || !fs_subcode)
        throw_line("unable to create dump files (image name: {})", options.image_name);

    SignalINT signal;
    seek_spiral_start(*ctx.sptd, signal);
    std::vector<uint8_t> staged;
    std::vector<uint8_t> read_buffer(SPIRAL_READ_FRAMES * CD_RAW_DATA_SIZE);

    uint64_t c2_errors = 0;
    uint64_t q_errors = 0;
    uint64_t q_mismatches = 0;
    uint64_t frames_written = 0;
    uint64_t read_errors = 0;
    uint32_t consecutive_read_errors = 0;
    int64_t next_lba = 0;
    int32_t subcode_shift = 0;
    std::optional<int32_t> current_q_lba;
    bool anchored = false;

    for(;;)
    {
        if(signal.interrupt())
            signal.raiseDefault();

        uint32_t count = SPIRAL_READ_FRAMES;
        if(anchored)
            count = (uint32_t)std::min<int64_t>(count, (int64_t)lba_end - next_lba);

        uint32_t bytes = count * CD_RAW_DATA_SIZE;
        for(;;)
        {
            if(signal.interrupt())
                signal.raiseDefault();
            auto result = cmd_spiraldrive(*ctx.sptd, read_buffer.data(), bytes, SpiralDrive_Operation::READ, C4_TIMEOUT_MS, count);
            if(result.first.status_code)
            {
                ++read_errors;
                ++consecutive_read_errors;
                if(consecutive_read_errors >= SPIRAL_MAX_CONSECUTIVE_READ_ERRORS)
                    throw_line("READ failed {} consecutive times; stream cannot advance (last SCSI: {})", consecutive_read_errors, SPTD::StatusMessage(result.first));
                LOG_R("warning: READ at stream LBA {} failed (SCSI: {}, transferred: {} bytes; response discarded); continuing; alignment may be lost", next_lba, SPTD::StatusMessage(result.first),
                    result.second);
                continue;
            }
            check_good_transfer(result, bytes);
            consecutive_read_errors = 0;
            break;
        }

        const uint8_t *frames = read_buffer.data();
        uint64_t batch_frames = count;
        if(!anchored)
        {
            for(uint32_t i = 0; i < count; ++i)
            {
                const uint8_t *frame = read_buffer.data() + i * CD_RAW_DATA_SIZE;
                if(staged.size() / CD_RAW_DATA_SIZE >= SPIRAL_MAX_STAGED_FRAMES)
                    throw_line("stream exceeded {} frames without an LBA anchor", SPIRAL_MAX_STAGED_FRAMES);
                staged.insert(staged.end(), frame, frame + CD_RAW_DATA_SIZE);
                auto q_lba = position_q_lba(frame);
                if(!q_lba)
                    continue;

                int64_t staged_frames = (int64_t)(staged.size() / CD_RAW_DATA_SIZE);
                int64_t first_lba = (int64_t)*q_lba - (staged_frames - 1);
                if(first_lba < LBA_START || (int64_t)*q_lba >= LBA_LIMIT)
                    throw_line("stream cannot be placed in the standard CD LBA range (first: {}, anchor: {})", first_lba, *q_lba);
                if(lba_end <= first_lba)
                    throw_line("spiral::dump --lba-end precedes the first acquired frame (first: {}, end: {})", first_lba, lba_end);

                anchored = true;
                next_lba = first_lba;
                LOG("LBA anchor: {} ({} staged frames from LBA {})", *q_lba, staged_frames, first_lba);
                staged.insert(staged.end(), read_buffer.data() + (i + 1) * CD_RAW_DATA_SIZE, read_buffer.data() + count * CD_RAW_DATA_SIZE);
                frames = staged.data();
                batch_frames = staged.size() / CD_RAW_DATA_SIZE;
                break;
            }
        }

        if(anchored)
        {
            for(uint64_t i = 0; i < batch_frames && next_lba < lba_end; ++i)
            {
                const uint8_t *frame = frames + i * CD_RAW_DATA_SIZE;
                if(next_lba < LBA_START || next_lba >= LBA_LIMIT)
                    throw_line("stream LBA is outside the CD file range ({})", next_lba);

                current_q_lba = position_q_lba(frame);
                if(current_q_lba)
                {
                    int32_t shift = *current_q_lba - (int32_t)next_lba;
                    if(shift)
                        ++q_mismatches;
                    if(subcode_shift != shift)
                    {
                        subcode_shift = shift;
                        if(options.verbose)
                            LOGC_R("[LBA: {:6}] subcode desync (shift: {:+})", next_lba, subcode_shift);
                    }
                }
                if(!subcode_extract_q(frame + CD_DATA_SIZE + CD_C2_SIZE).isValid())
                    ++q_errors;

                auto state = c2_to_state(frame + CD_DATA_SIZE, State::SUCCESS);
                c2_errors += std::count(state.begin(), state.end(), State::ERROR_C2);

                uint64_t index = (uint64_t)(next_lba - LBA_START);
                write_entry(fs_scram, frame, CD_DATA_SIZE, index, 1, 0);
                write_entry(fs_state, (const uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, index, 1, 0);
                write_entry(fs_subcode, frame + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE, index, 1, 0);
                ++next_lba;
                ++frames_written;
            }
            staged.clear();
            LOGC_RF("LBA: {}, Q LBA: {}, frames: {}, C2 samples: {}, invalid Q: {}", next_lba, current_q_lba ? std::to_string(*current_q_lba) : std::string("?"), frames_written, c2_errors, q_errors);
        }
        else
            LOGC_RF("LBA: ?, buffered frames: {} (waiting for LBA anchor)", staged.size() / CD_RAW_DATA_SIZE);

        if(anchored && next_lba >= lba_end)
            break;
    }

    if(!anchored)
        throw_line("stream ended before a track Q supplied an LBA anchor");

    LOGC_RF("");
    LOGC("");
    LOG("captured {} frames; C2: {} samples; invalid Q: {}; Q mismatches: {}; SCSI READ errors: {}", frames_written, c2_errors, q_errors, q_mismatches, read_errors);

    return exit_code;
}

}
