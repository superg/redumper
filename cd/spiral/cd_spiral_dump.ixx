module;
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "throw_line.hh"



export module cd.spiral;

import cd.cd;
import cd.common;
import cd.spiral_refine;
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
import utils.misc;
import utils.signal;



namespace gpsxre
{

constexpr uint16_t C4_TIMEOUT_MS = 5000;
// wobble envelope, slot N covers the Nth 24th of the time from the previous frame event to this one
constexpr uint32_t C4_WBL_SIZE = 24;
// the wobble of a frame is sampled this many sectors ahead of its subcode, measured only on PX-W5224A
constexpr uint32_t C4_WBL_LEAD_SECTORS = 2;


void seek_spiral_start(SPTD &sptd)
{
    // the firmware moves to the first lead-in pits of pressed media or to the ATIP address before the lead-in of recordable media
    LOG("seeking spiral start");
    if(auto status = cmd_spiraldrive(sptd, nullptr, 0, SpiralDrive_Operation::REWIND, 0, 0); status.status_code)
        throw_line("REWIND failed (SCSI: {})", SPTD::StatusMessage(status));
}


// the wobble carrier keys a serial stream, carrier present is a 0 bit and each character has one start, eight data and two stop bits
// returns each character with the envelope slot where its start bit begins
std::vector<std::pair<size_t, uint8_t>> decode_spiral_wobble(std::span<const uint8_t> envelope)
{
    constexpr uint8_t CARRIER_LEVEL = 45;
    constexpr size_t CHARACTER_BITS = 11;
    // one bit lasts 0.3 sector at any drive speed
    auto bit_offset = [](size_t bit) { return (bit * C4_WBL_SIZE * 3 + 5) / 10; };

    std::vector<std::pair<size_t, uint8_t>> characters;
    for(size_t i = 1; i + bit_offset(CHARACTER_BITS) <= envelope.size(); ++i)
    {
        // the start bit begins where the carrier appears, a single slot above the level is noise
        if(envelope[i - 1] >= CARRIER_LEVEL || envelope[i] < CARRIER_LEVEL || envelope[i + 1] < CARRIER_LEVEL)
            continue;

        uint32_t bits = 0;
        for(size_t bit = 0; bit < CHARACTER_BITS; ++bit)
        {
            auto cell = envelope.subspan(i + bit_offset(bit), bit_offset(bit + 1) - bit_offset(bit));
            auto carrier = (size_t)std::count_if(cell.begin(), cell.end(), [](uint8_t level) { return level >= CARRIER_LEVEL; });
            if(carrier * 2 <= cell.size())
                bits |= 1 << bit;
        }

        // start bit 0 and both stop bits 1
        if((bits & 0x601) == 0x600)
        {
            characters.emplace_back(i, (uint8_t)(bits >> 1));
            // resume inside the last stop bit so the next start bit is found by its own edge
            i += bit_offset(CHARACTER_BITS - 1);
        }
    }

    return characters;
}


// the wobble capture is its own stream, placed by the Q packets captured with it
void convert_spiral_wobble(const std::string &image_prefix, int32_t batch_size)
{
    std::fstream fs_wbl(image_prefix + ".wbl.raw", std::ios::in | std::ios::binary);
    std::fstream fs_subq(image_prefix + ".subq.raw", std::ios::in | std::ios::binary);
    if(!fs_wbl.is_open() || !fs_subq.is_open())
        throw_line("unable to open wobble capture files ({})", image_prefix);
    fs_wbl.exceptions(std::ios::badbit | std::ios::failbit);
    fs_subq.exceptions(std::ios::badbit | std::ios::failbit);

    std::vector<uint8_t> subq_buffer((size_t)batch_size * sizeof(ChannelQ));
    auto sectors_count = (int32_t)scale_up(std::filesystem::file_size(image_prefix + ".subq.raw"), sizeof(ChannelQ));
    auto segments = std::get<1>(analyze_spiral_subcode(fs_subq, sizeof(ChannelQ), sectors_count, subq_buffer));

    // characters span several sectors, so the whole envelope is decoded in capture order
    std::vector<uint8_t> wbl_buffer((size_t)sectors_count * C4_WBL_SIZE);
    read_entry(fs_wbl, wbl_buffer.data(), C4_WBL_SIZE, 0, sectors_count, 0, 0);
    auto characters = decode_spiral_wobble(wbl_buffer);
    LOG("wobble characters: {}", characters.size());

    // one byte per character time, 3.3 sectors, on the disc scale from LBA_START, zero where no character is found
    constexpr int64_t CHARACTER_SLOTS_X10 = C4_WBL_SIZE * 33;
    // the file covers the capture up to its last sector and is written even without characters
    int64_t sectors_end = 0;
    for(size_t i = 0; i < segments.size(); ++i)
        sectors_end = std::max<int64_t>(sectors_end, segments[i].second - LBA_START + (i + 1 < segments.size() ? segments[i + 1].first : sectors_count) - segments[i].first);
    std::vector<uint8_t> wobble_buffer((size_t)(sectors_end * C4_WBL_SIZE * 10 / CHARACTER_SLOTS_X10));
    size_t segment_index = 0;
    size_t previous_slot = 0;
    int64_t previous_index = -1;
    for(auto [slot, character] : characters)
    {
        bool adjacent = previous_index >= 0 && (slot - previous_slot) * 10 < CHARACTER_SLOTS_X10 + CHARACTER_SLOTS_X10 / 11;
        for(; segment_index + 1 < segments.size() && (size_t)segments[segment_index + 1].first * C4_WBL_SIZE <= slot; adjacent = false)
            ++segment_index;

        const auto &[source_index, lba] = segments[segment_index];
        // a character that starts within one bit of the end of the previous one takes the next byte, so start bit jitter cannot split a string
        int64_t index = adjacent ? previous_index + 1 : ((int64_t)(lba - LBA_START - source_index) * C4_WBL_SIZE + (int64_t)slot) * 10 / CHARACTER_SLOTS_X10;
        previous_slot = slot;
        previous_index = index;
        if(index < 0)
            continue;

        if(wobble_buffer.size() <= (size_t)index)
            wobble_buffer.resize(index + 1);
        wobble_buffer[index] = character;
    }
    write_vector(image_prefix + ".wobble", wobble_buffer);
}


// the frame position is predicted from the latest confirmed Q, the end limit is exclusive and an explicit one replaces the limits found in Q
bool spiral_end_reached(const Options &options, std::optional<int32_t> lba_end, std::optional<std::pair<int32_t, uint32_t>> q_lba, uint32_t lba_index)
{
    if(!q_lba)
        return false;

    int32_t lba = q_lba->first + (int32_t)(lba_index - q_lba->second);

    return options.lba_end ? lba >= *options.lba_end : lba_end && lba >= *lba_end;
}


// a dump reads DATA/C2/SUB frames and a wobble capture reads SUBQ/WBL frames, both end by the same Q limits
int capture_spiral(Context &ctx, Options &options, bool wobble)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::CD)
        return exit_code;

    {
        auto spiraldrive_version = is_spiraldrive_firmware(ctx.drive_config);
        if(spiraldrive_version)
        {
            if(spiraldrive_version < spiraldrive_minimum_version())
                throw_line("SpiralDrive firmware {} or newer is required", spiraldrive_version_string(spiraldrive_minimum_version()));
        }
        else
            throw_line("a drive running SpiralDrive firmware is required");
    }

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();
    const std::vector<std::string> extensions = wobble ? std::vector<std::string>{ ".subq.raw", ".wbl.raw" } : std::vector<std::string>{ ".main.raw", ".c2.raw", ".sub.raw" };
    for(const auto &extension : extensions)
        if(!options.overwrite && std::filesystem::exists(image_prefix + extension))
            throw_line("dump already exists (image name: {})", options.image_name);
    if(wobble && !options.overwrite && std::filesystem::exists(image_prefix + ".wobble"))
        throw_line("dump already exists (image name: {})", options.image_name);

    seek_spiral_start(*ctx.sptd);

    // the demodulated output belongs to the wobble capture and is replaced with it, a failed positioning keeps the previous one
    if(wobble)
        std::filesystem::remove(image_prefix + ".wobble");

    std::vector<std::fstream> files;
    for(const auto &extension : extensions)
    {
        files.emplace_back(image_prefix + extension, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        files.back().exceptions(std::ios::badbit | std::ios::failbit);
    }

    if(options.spiraldrive_batch_size < 1)
        throw_line("invalid batch size (value: {})", options.spiraldrive_batch_size);

    uint32_t frame_size = wobble ? sizeof(ChannelQ) + C4_WBL_SIZE : CD_RAW_DATA_SIZE;
    std::vector<uint8_t> buffer((size_t)options.spiraldrive_batch_size * frame_size);

    uint32_t errors_c2 = 0;
    uint32_t errors_q = 0;
    std::optional<std::pair<int32_t, uint32_t>> q_lba;
    std::optional<std::pair<int32_t, uint32_t>> q_candidate;
    std::optional<int32_t> lba_end;
    if(options.spiraldrive_dreamcast)
        lba_end = 45000;

    bool last_session_message = false;
    SignalINT signal;
    uint32_t lba_index = 0;
    // checked before each command as well, so no batch is read when the previous one ended right at the limit
    while(!spiral_end_reached(options, lba_end, q_lba, lba_index) && !signal.interrupt())
    {
        uint32_t transferred_size = 0;
        auto status = cmd_spiraldrive(*ctx.sptd, buffer.data(), (uint32_t)buffer.size(), wobble ? SpiralDrive_Operation::Q_WOBBLE : SpiralDrive_Operation::READ, C4_TIMEOUT_MS,
            options.spiraldrive_batch_size, &transferred_size);

        // a short transfer holds the frames captured before a stall or a gap, which the firmware reports with the next command
        uint32_t frames_count = transferred_size / frame_size;
        if(!status.status_code && !frames_count)
            status.status_code = SPTD::HOST_SHORT_TRANSFER;
        if(status.status_code)
        {
            frames_count = 1;
            std::fill_n(buffer.begin(), frame_size, 0);
            if(!wobble)
                std::fill_n(buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE, 0xFF);

            LOG_R("READ failed (SCSI: {})", SPTD::StatusMessage(status));
        }

        for(uint32_t i = 0; i < frames_count; ++i)
        {
            // the remaining frames of the batch lie past the end limit
            if(spiral_end_reached(options, lba_end, q_lba, lba_index))
                break;

            // the parts of the other frame layout are empty
            uint8_t *sector = buffer.data() + i * frame_size;
            std::span<uint8_t> sector_data(sector, wobble ? 0 : CD_DATA_SIZE);
            std::span<uint8_t> sector_c2(sector + sector_data.size(), wobble ? 0 : CD_C2_SIZE);
            std::span<uint8_t> sector_sub(sector + sector_data.size() + sector_c2.size(), wobble ? 0 : CD_SUBCODE_SIZE);
            std::span<uint8_t> sector_subq(sector, wobble ? sizeof(ChannelQ) : 0);
            std::span<uint8_t> sector_wbl(sector + sector_subq.size(), wobble ? C4_WBL_SIZE : 0);

            ChannelQ q = wobble ? *(ChannelQ *)sector_subq.data() : subcode_extract_q(sector_sub.data());
            if(q.isValid())
            {
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

            if(wobble)
            {
                write_entry_bit(files[0], sector_subq.data(), sector_subq.size(), lba_index, 1, 0, false);
                write_entry_bit(files[1], sector_wbl.data(), sector_wbl.size(), lba_index + C4_WBL_LEAD_SECTORS, 1, 0, false);
            }
            else
            {
                // compensate the drive read offset and C2 shift, where one C2 bit covers one data byte
                write_entry_bit(files[0], sector_data.data(), sector_data.size(), lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE * CHAR_BIT, false);
                write_entry_bit(files[1], sector_c2.data(), sector_c2.size(), lba_index, 1, ctx.drive_config.read_offset * CD_SAMPLE_SIZE + (int32_t)ctx.drive_config.c2_shift * CHAR_BIT, true);
                write_entry_bit(files[2], sector_sub.data(), sector_sub.size(), lba_index, 1, 0, false);
            }

            ++lba_index;
        }

        if(wobble)
            LOGC_RF("{} sector: {:6}, LBA: {:>6}, errors: {{ Q: {} }}", spinner_animation(), lba_index, q_lba ? q_lba->first + (int32_t)(lba_index - q_lba->second) : LBA_START, errors_q);
        else
            LOGC_RF("{} sector: {:6}, LBA: {:>6}, errors: {{ C2: {}, Q: {} }}", spinner_animation(), lba_index, q_lba ? q_lba->first + (int32_t)(lba_index - q_lba->second) : LBA_START, errors_c2,
                errors_q);
    }

    LOGC_RF("");
    LOGC("");

    // report close failures and finish cleanup before raising an interrupt
    for(auto &file : files)
        file.close();

    LOG("media errors: ");
    if(!wobble)
        LOG("  C2: {}", errors_c2);
    LOG("  Q: {}", errors_q);

    // an interrupted capture is demodulated as well, nothing else can do it later
    if(wobble)
    {
        LOG("");
        convert_spiral_wobble(image_prefix, options.spiraldrive_batch_size);
    }

    if(signal.interrupt())
        signal.raiseDefault();

    return exit_code;
}


export int redumper_spiral_dump(Context &ctx, Options &options)
{
    return capture_spiral(ctx, options, false);
}


export int redumper_spiral_wobble(Context &ctx, Options &options)
{
    return capture_spiral(ctx, options, true);
}
}
