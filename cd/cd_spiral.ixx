module;
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
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

constexpr uint16_t SPIRAL_TIMEOUT_MS = 5000;
constexpr uint32_t SPIRAL_READ_FRAMES = 16;
constexpr uint32_t SPIRAL_SEARCH_MOVES = 256;
constexpr uint32_t SPIRAL_MAX_STAGED_FRAMES = 100000;
constexpr int32_t SPIRAL_SEEK_INWARD = -200;


bool is_leadin(const uint8_t *frame)
{
    auto q = subcode_extract_q(frame + CD_DATA_SIZE + CD_C2_SIZE);
    return q.isValid() && q.adr == 1 && q.mode1.tno == 0;
}


std::optional<int32_t> track_lba(const uint8_t *frame)
{
    auto q = subcode_extract_q(frame + CD_DATA_SIZE + CD_C2_SIZE);
    if(!q.isValid() || q.adr != 1 || q.mode1.tno == 0 || q.mode1.tno > 0x99 || (q.mode1.tno & 0x0f) > 9 || !BCDMSF_valid(q.mode1.a_msf))
        return std::nullopt;

    return BCDMSF_to_LBA(q.mode1.a_msf);
}


void check_good_transfer(const std::pair<SPTD::Status, uint32_t> &result, uint32_t expected)
{
    if(!result.first.status_code && result.second != expected)
        throw_line("SpiralDrive short transfer (expected: {}, transferred: {})", expected, result.second);
}

}


export using SpiralDriveSend = std::function<std::pair<SPTD::Status, uint32_t>(uint8_t *, uint32_t, SpiralDrive_Operation, uint16_t, int32_t)>;


export int redumper_dump_spiral_with_transport(Context &ctx, Options &options, const SpiralDriveSend &send)
{
    if(options.lba_start)
        throw_line("dump::spiral does not support --lba-start");
    if(options.force_omnidrive)
        throw_line("dump::spiral does not support --force-omnidrive");
    if(ctx.disc_type != DiscType::CD)
        throw_line("dump::spiral requires CD media");

    auto firmware_version = is_spiraldrive_firmware(ctx.drive_config);
    if(!firmware_version || *firmware_version < spiraldrive_minimum_version())
        throw_line("dump::spiral requires SpiralDrive firmware v1.0.0 or newer");
    if(options.lba_end && (*options.lba_end <= LBA_START || *options.lba_end > (int32_t)LBA_LIMIT))
        throw_line("dump::spiral --lba-end is outside the CD LBA range ({})", *options.lba_end);

    image_check_overwrite(options);
    if(!options.image_path.empty())
        std::filesystem::create_directories(options.image_path);

    SignalINT signal;
    std::vector<uint8_t> probe(CD_RAW_DATA_SIZE);
    bool found_leadin = false;

    for(uint32_t move = 0; move <= SPIRAL_SEARCH_MOVES; ++move)
    {
        if(signal.interrupt())
            signal.raiseDefault();

        auto result = send(probe.data(), CD_RAW_DATA_SIZE, SpiralDrive_Operation::READ, SPIRAL_TIMEOUT_MS, 1);
        check_good_transfer(result, CD_RAW_DATA_SIZE);
        if(!result.first.status_code && is_leadin(probe.data()))
        {
            found_leadin = true;
            LOG("SpiralDrive lead-in found after {} inward moves", move);
            break;
        }

        if(move == SPIRAL_SEARCH_MOVES)
            break;

        auto seek = send(nullptr, 0, SpiralDrive_Operation::SEEK, SPIRAL_TIMEOUT_MS, SPIRAL_SEEK_INWARD);
        if(seek.first.status_code)
            throw_line("SpiralDrive inward SEEK failed (SCSI: {})", SPTD::StatusMessage(seek.first));
        check_good_transfer(seek, 0);
    }

    if(!found_leadin)
        throw_line("SpiralDrive lead-in was not found after {} inward moves", SPIRAL_SEARCH_MOVES);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    std::fstream fs_scram;
    std::fstream fs_state;
    std::fstream fs_subcode;

    std::vector<uint8_t> staged(probe.begin(), probe.end());
    std::vector<uint8_t> read_buffer(SPIRAL_READ_FRAMES * CD_RAW_DATA_SIZE);
    uint64_t c2_errors = 0;
    uint64_t q_errors = 0;
    uint64_t q_mismatches = 0;
    uint64_t frames_written = 0;
    int64_t next_lba = 0;
    bool anchored = false;
    bool done = false;

    auto write_frame = [&](const uint8_t *frame)
    {
        if(next_lba < LBA_START || next_lba >= LBA_LIMIT)
            throw_line("SpiralDrive stream LBA is outside the CD file range ({})", next_lba);

        if(auto q_lba = track_lba(frame); q_lba && *q_lba != next_lba)
        {
            ++q_mismatches;
            if(options.verbose)
                LOGC("SpiralDrive Q mismatch (stream LBA: {}, Q LBA: {})", next_lba, *q_lba);
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
    };

    auto accept_frame = [&](const uint8_t *frame)
    {
        if(anchored)
        {
            write_frame(frame);
            return;
        }

        if(staged.size() / CD_RAW_DATA_SIZE >= SPIRAL_MAX_STAGED_FRAMES)
            throw_line("SpiralDrive stream exceeded {} frames without an LBA anchor", SPIRAL_MAX_STAGED_FRAMES);
        staged.insert(staged.end(), frame, frame + CD_RAW_DATA_SIZE);
        auto q_lba = track_lba(frame);
        if(!q_lba)
            return;

        int64_t staged_frames = (int64_t)(staged.size() / CD_RAW_DATA_SIZE);
        int64_t first_lba = (int64_t)*q_lba - (staged_frames - 1);
        if(first_lba < LBA_START || (int64_t)*q_lba >= LBA_LIMIT)
            throw_line("SpiralDrive lead-in cannot be placed in the standard CD LBA range (first: {}, anchor: {})", first_lba, *q_lba);
        if(options.lba_end && *options.lba_end <= first_lba)
            throw_line("dump::spiral --lba-end precedes the first acquired frame (first: {}, end: {})", first_lba, *options.lba_end);

        auto mode = std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc;
        fs_scram.open(image_prefix + ".scram", mode);
        fs_state.open(image_prefix + ".state", mode);
        fs_subcode.open(image_prefix + ".subcode", mode);
        if(!fs_scram || !fs_state || !fs_subcode)
            throw_line("unable to create SpiralDrive dump files (image name: {})", options.image_name);

        anchored = true;
        next_lba = first_lba;
        LOG("SpiralDrive LBA anchor: {} ({} staged frames from LBA {})", *q_lba, staged_frames, first_lba);
        for(uint64_t i = 0; i < (uint64_t)staged_frames; ++i)
        {
            if(options.lba_end && next_lba >= *options.lba_end)
            {
                done = true;
                break;
            }
            write_frame(staged.data() + i * CD_RAW_DATA_SIZE);
        }
        staged.clear();
    };

    for(;;)
    {
        if(signal.interrupt())
            signal.raiseDefault();
        if(done || (anchored && options.lba_end && next_lba >= *options.lba_end))
            break;

        uint32_t count = SPIRAL_READ_FRAMES;
        if(anchored && options.lba_end)
            count = (uint32_t)std::min<int64_t>(count, (int64_t)*options.lba_end - next_lba);

        uint32_t bytes = count * CD_RAW_DATA_SIZE;
        auto result = send(read_buffer.data(), bytes, SpiralDrive_Operation::READ, SPIRAL_TIMEOUT_MS, count);
        if(result.first.status_code)
        {
            LOG("SpiralDrive READ ended (SCSI: {}, transferred: {} bytes; response discarded)", SPTD::StatusMessage(result.first), result.second);
            break;
        }
        check_good_transfer(result, bytes);

        for(uint32_t i = 0; i < count && !done; ++i)
        {
            accept_frame(read_buffer.data() + i * CD_RAW_DATA_SIZE);
            if(anchored && options.lba_end && next_lba >= *options.lba_end)
                done = true;
        }

        if(anchored)
            LOGC_RF("SpiralDrive LBA: {}, frames: {}, C2 samples: {}, invalid Q: {}", next_lba, frames_written, c2_errors, q_errors);
    }

    if(!anchored)
        throw_line("SpiralDrive stream ended before a track Q supplied an LBA anchor");

    LOGC_RF("");
    LOGC("");
    LOG("SpiralDrive captured {} frames; C2: {} samples; invalid Q: {}; Q mismatches: {}", frames_written, c2_errors, q_errors, q_mismatches);

    return c2_errors || (options.refine_subchannel && q_errors);
}


export int redumper_dump_spiral(Context &ctx, Options &options)
{
    return redumper_dump_spiral_with_transport(ctx, options,
        [&](uint8_t *buffer, uint32_t size, SpiralDrive_Operation operation, uint16_t timeout, int32_t value) { return cmd_spiraldrive(*ctx.sptd, buffer, size, operation, timeout, value); });
}

}
