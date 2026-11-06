module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "throw_line.hh"



export module cd.spiral_convert;

import cd.cd;
import cd.common;
import cd.subcode;
import common;
import options;
import scsi.mmc;
import utils.endian;
import utils.file_io;
import utils.logger;



namespace gpsxre
{

using SpiralTOCEntries = std::map<std::pair<uint8_t, uint8_t>, ChannelQ>;


FULL_TOC_Descriptor encode_spiral_full_toc_entry(uint8_t session_number, const ChannelQ &q)
{
    FULL_TOC_Descriptor descriptor{
        .session_number = session_number,
        .control = q.control,
        .adr = q.adr,
        .tno = q.mode1.tno,
        .point = q.mode1.point_index,
        .msf = { q.mode1.msf.m,   q.mode1.msf.s,   q.mode1.msf.f   },
        .zero = q.mode1.zero,
        .p_msf = { q.mode1.a_msf.m, q.mode1.a_msf.s, q.mode1.a_msf.f }
    };
    std::span<uint8_t> bytes((uint8_t *)&descriptor, sizeof(descriptor));
    auto decode_fields = [&bytes](std::initializer_list<size_t> offsets)
    {
        for(size_t offset : offsets)
            bytes[offset] = bcd_decode(bytes[offset]);
    };

    if(q.adr == 1)
    {
        if(q.mode1.point_index < 0xA0)
            decode_fields({ 3, 4, 5, 6, 8, 9, 10 });
        else if(q.mode1.point_index == 0xA0)
            decode_fields({ 4, 5, 6, 8, 10 });
        else if(q.mode1.point_index == 0xA1)
            decode_fields({ 4, 5, 6, 8 });
        else if(q.mode1.point_index == 0xA2)
            decode_fields({ 4, 5, 6, 8, 9, 10 });
    }
    else
    {
        // ADR 5 stores pointer data in the time fields; convert only its numeric fields
        if(q.mode1.point_index < 0xB0)
            decode_fields({ 3, 4, 5, 6, 8, 9, 10 });
        else if(q.mode1.point_index == 0xB0)
        {
            if(q.mode1.msf.m != 0xFF)
                decode_fields({ 4, 5, 6 });
            decode_fields({ 7, 8, 9, 10 });
        }
        else if(q.mode1.point_index >= 0xB1 && q.mode1.point_index <= 0xB4)
            decode_fields({ 4, 5, 6, 7, 8, 9, 10 });
        else if(q.mode1.point_index == 0xC0)
            decode_fields({ 8, 9, 10 });
    }

    return descriptor;
}


void encode_spiral_toc(const std::vector<SpiralTOCEntries> &sessions, std::vector<uint8_t> &toc_buffer, std::vector<uint8_t> &full_toc_buffer)
{
    toc_buffer.assign(sizeof(CMD_ParameterListHeader), 0);
    full_toc_buffer.assign(sizeof(CMD_ParameterListHeader), 0);
    std::map<uint8_t, TOC_Descriptor> tracks;
    for(size_t session_index = 0; session_index < sessions.size(); ++session_index)
    {
        const auto &entries = sessions[session_index];
        for(uint8_t point : { 0xA0, 0xA1, 0xA2 })
            if(!entries.contains({ 1, point }))
                throw_line("missing confirmed TOC entry (session: {}, POINT: {:02X})", session_index + 1, point);

        uint8_t first_track = bcd_decode(entries.at({ 1, 0xA0 }).mode1.a_msf.m);
        uint8_t last_track = bcd_decode(entries.at({ 1, 0xA1 }).mode1.a_msf.m);
        if(!first_track || first_track > last_track || last_track >= CD_TRACKS_COUNT)
            throw_line("invalid TOC track range (session: {}, first: {}, last: {})", session_index + 1, first_track, last_track);

        for(uint8_t track_number = first_track; track_number <= last_track; ++track_number)
        {
            auto entry = entries.find({ 1, bcd_encode(track_number) });
            if(entry == entries.end())
                throw_line("missing confirmed TOC track (session: {}, track: {})", session_index + 1, track_number);
            const auto &q = entry->second;
            if(!BCDMSF_valid(q.mode1.a_msf))
                throw_line("invalid TOC track address (session: {}, track: {})", session_index + 1, track_number);
            tracks[track_number] = { 0, q.control, q.adr, track_number, 0, endian_swap((uint32_t)BCDMSF_to_LBA(q.mode1.a_msf)) };
        }

        const auto &leadout = entries.at({ 1, 0xA2 });
        if(!BCDMSF_valid(leadout.mode1.a_msf))
            throw_line("invalid TOC lead-out address (session: {})", session_index + 1);
        tracks[CD_LEADOUT_TRACK_NUMBER] = { 0, leadout.control, leadout.adr, CD_LEADOUT_TRACK_NUMBER, 0, endian_swap((uint32_t)BCDMSF_to_LBA(leadout.mode1.a_msf)) };

        for(const auto &[key, q] : entries)
        {
            auto descriptor = encode_spiral_full_toc_entry((uint8_t)(session_index + 1), q);
            std::span<const uint8_t> bytes((const uint8_t *)&descriptor, sizeof(descriptor));
            full_toc_buffer.insert(full_toc_buffer.end(), bytes.begin(), bytes.end());
        }
    }

    for(const auto &[track_number, descriptor] : tracks)
    {
        std::span<const uint8_t> bytes((const uint8_t *)&descriptor, sizeof(descriptor));
        toc_buffer.insert(toc_buffer.end(), bytes.begin(), bytes.end());
    }

    if(full_toc_buffer.size() - sizeof(uint16_t) > std::numeric_limits<uint16_t>::max())
        throw_line("FULL TOC is too large");
    CMD_ParameterListHeader toc_header{
        endian_swap((uint16_t)(toc_buffer.size() - sizeof(uint16_t))), { tracks.begin()->first, std::prev(tracks.end(), 2)->first }
    };
    CMD_ParameterListHeader full_toc_header{
        endian_swap((uint16_t)(full_toc_buffer.size() - sizeof(uint16_t))), { 1, (uint8_t)sessions.size() }
    };
    std::memcpy(toc_buffer.data(), &toc_header, sizeof(toc_header));
    std::memcpy(full_toc_buffer.data(), &full_toc_header, sizeof(full_toc_header));
}


export int redumper_spiral_convert(Context &, Options &options)
{
    constexpr int32_t BATCH_SIZE = 256;
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    for(const char *extension : { ".toc", ".fulltoc", ".scram", ".state", ".subcode" })
        if(!options.overwrite && std::filesystem::exists(image_prefix + extension))
            throw_line("output already exists ({})", image_prefix + extension);

    std::fstream fs_sub(image_prefix + ".sub.raw", std::ios::in | std::ios::binary);
    std::fstream fs_main(image_prefix + ".main.raw", std::ios::in | std::ios::binary);
    std::fstream fs_c2(image_prefix + ".c2.raw", std::ios::in | std::ios::binary);
    if(!fs_sub.is_open() || !fs_main.is_open() || !fs_c2.is_open())
        throw_line("unable to open capture files (image name: {})", options.image_name);
    fs_sub.exceptions(std::ios::badbit | std::ios::failbit);
    fs_main.exceptions(std::ios::badbit | std::ios::failbit);
    fs_c2.exceptions(std::ios::badbit | std::ios::failbit);

    uint64_t sub_size = std::filesystem::file_size(image_prefix + ".sub.raw");
    uint64_t raw_sectors = sub_size / CD_SUBCODE_SIZE + (sub_size % CD_SUBCODE_SIZE != 0);
    if(raw_sectors > (uint64_t)(std::numeric_limits<int32_t>::max() - LBA_ZERO_INDEX))
        throw_line("capture is too large");
    int32_t sectors_count = (int32_t)raw_sectors;
    std::vector<uint8_t> sub_buffer(BATCH_SIZE * CD_SUBCODE_SIZE);
    std::vector<SpiralTOCEntries> sessions(1);
    std::optional<int32_t> zero_index;
    std::vector<std::pair<int32_t, int32_t>> segments;
    std::optional<ChannelQ> repeated_q;
    uint8_t repeat_count = 0;
    bool program_area_seen = false;
    LOG("analyzing subcode (sectors: {})", sectors_count);
    for(int32_t batch_index = 0; batch_index < sectors_count; batch_index += BATCH_SIZE)
    {
        int32_t count = std::min(BATCH_SIZE, sectors_count - batch_index);
        read_entry(fs_sub, sub_buffer.data(), CD_SUBCODE_SIZE, batch_index, count, 0, 0);
        for(int32_t i = 0; i < count; ++i)
        {
            int32_t sector_index = batch_index + i;
            std::span<const uint8_t> sector_sub(sub_buffer.data() + i * CD_SUBCODE_SIZE, CD_SUBCODE_SIZE);
            auto q = subcode_extract_q(sector_sub.data());
            if(!q.isValid())
            {
                repeated_q.reset();
                repeat_count = 0;
                continue;
            }

            if(q.adr == 1 && q.mode1.tno)
            {
                program_area_seen = true;
                if(!BCDMSF_valid(q.mode1.a_msf))
                    throw_line("invalid absolute Q position (sector: {})", sector_index);
                int32_t lba = BCDMSF_to_LBA(q.mode1.a_msf);
                int32_t current_zero_index = sector_index - lba;
                if(!zero_index)
                {
                    if(current_zero_index < 0 || current_zero_index >= sectors_count)
                        throw_line("LBA 0 is outside the capture (sector index: {})", current_zero_index);
                    zero_index = current_zero_index;
                    segments.emplace_back(0, -*zero_index);
                }
                else if(current_zero_index != segments.back().first - segments.back().second)
                    segments.emplace_back(sector_index, lba);
            }

            if((q.adr == 1 || q.adr == 5) && !q.mode1.tno)
            {
                if(program_area_seen)
                {
                    if(sessions.size() >= 99)
                        throw_line("too many captured sessions");
                    sessions.emplace_back();
                    program_area_seen = false;
                }

                ChannelQ comparison_q(q);
                if(q.adr == 1)
                    std::fill(std::begin(comparison_q.mode1.msf.raw), std::end(comparison_q.mode1.msf.raw), 0);
                if(repeated_q && !std::memcmp(repeated_q->raw, comparison_q.raw, sizeof(comparison_q.raw)))
                    repeat_count = std::min(repeat_count + 1, 3);
                else
                {
                    repeated_q = comparison_q;
                    repeat_count = 1;
                }
                if(repeat_count == 3)
                    sessions.back()[{ (uint8_t)q.adr, q.mode1.point_index }] = q;
            }
            else
            {
                repeated_q.reset();
                repeat_count = 0;
            }
        }
        LOGC_RF("analyzing subcode: {} / {}", batch_index + count, sectors_count);
    }
    LOGC_RF("");
    LOGC("");

    if(!zero_index)
        throw_line("no valid absolute Q position found");
    int32_t prefix_sectors = LBA_ZERO_INDEX - *zero_index;
    if(prefix_sectors < 0)
        throw_line("capture starts before the supported output origin (LBA: {})", -*zero_index);

    LOG("segments:");
    int32_t previous_end = segments.front().second;
    for(size_t segment_index = 0; segment_index < segments.size(); ++segment_index)
    {
        const auto &[source_index, lba] = segments[segment_index];
        int32_t source_end = segment_index + 1 < segments.size() ? segments[segment_index + 1].first : sectors_count;
        int32_t count = source_end - source_index;
        if(lba < LBA_START || count > std::numeric_limits<int32_t>::max() - (lba - LBA_START))
            throw_line("segment is outside the supported output range (sector: {}, LBA: {}, sectors: {})", source_index, lba, count);
        int32_t lba_end = lba + count;
        LOG("  LBA: [{:6} .. {:6}), count: {:6}, jump: {:+}", lba, lba_end, count, lba - previous_end);
        previous_end = lba_end;
    }

    LOG("");

    std::vector<uint8_t> toc_buffer;
    std::vector<uint8_t> full_toc_buffer;
    encode_spiral_toc(sessions, toc_buffer, full_toc_buffer);
    write_vector(image_prefix + ".toc", toc_buffer);
    write_vector(image_prefix + ".fulltoc", full_toc_buffer);

    std::fstream fs_scram(image_prefix + ".scram", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_state(image_prefix + ".state", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    std::fstream fs_subcode(image_prefix + ".subcode", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    fs_scram.exceptions(std::ios::badbit | std::ios::failbit);
    fs_state.exceptions(std::ios::badbit | std::ios::failbit);
    fs_subcode.exceptions(std::ios::badbit | std::ios::failbit);
    std::vector<uint8_t> main_buffer(BATCH_SIZE * CD_DATA_SIZE);
    std::vector<uint8_t> c2_buffer(BATCH_SIZE * CD_C2_SIZE);
    std::vector<State> state_buffer(BATCH_SIZE * CD_DATA_SIZE_SAMPLES);
    for(size_t segment_index = 0; segment_index < segments.size(); ++segment_index)
    {
        const auto &[source_index, lba] = segments[segment_index];
        int32_t source_end = segment_index + 1 < segments.size() ? segments[segment_index + 1].first : sectors_count;
        for(int32_t batch_index = source_index; batch_index < source_end; batch_index += BATCH_SIZE)
        {
            int32_t count = std::min(BATCH_SIZE, source_end - batch_index);
            read_entry(fs_main, main_buffer.data(), CD_DATA_SIZE, batch_index, count, 0, 0);
            read_entry(fs_c2, c2_buffer.data(), CD_C2_SIZE, batch_index, count, 0, 0xFF);
            read_entry(fs_sub, sub_buffer.data(), CD_SUBCODE_SIZE, batch_index, count, 0, 0);
            for(int32_t i = 0; i < count; ++i)
            {
                std::span<const uint8_t> sector_c2(c2_buffer.data() + i * CD_C2_SIZE, CD_C2_SIZE);
                auto state = c2_to_state(sector_c2.data(), State::SUCCESS);
                std::copy(state.begin(), state.end(), state_buffer.begin() + i * CD_DATA_SIZE_SAMPLES);
            }
            int32_t output_index = lba - LBA_START + (batch_index - source_index);
            write_entry(fs_scram, main_buffer.data(), CD_DATA_SIZE, output_index, count, 0, 0);
            write_entry(fs_state, (const uint8_t *)state_buffer.data(), CD_DATA_SIZE_SAMPLES, output_index, count, 0, (uint8_t)State::ERROR_SKIP);
            write_entry(fs_subcode, sub_buffer.data(), CD_SUBCODE_SIZE, output_index, count, 0, 0);
            LOGC_RF("converting: {} / {}", batch_index + count, sectors_count);
        }
    }
    LOGC_RF("");
    LOGC("");
    fs_scram.close();
    fs_state.close();
    fs_subcode.close();
    fs_sub.close();
    fs_main.close();
    fs_c2.close();
    LOG("converted {} sectors", sectors_count);
    return 0;
}
}
