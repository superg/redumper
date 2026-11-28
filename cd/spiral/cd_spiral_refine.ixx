module;
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "throw_line.hh"



export module cd.spiral_refine;

import cd.cd;
import cd.common;
import cd.dump;
import cd.subcode;
import common;
import crc.crc16_gsm;
import options;
import scsi.mmc;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;



namespace gpsxre
{

using SpiralTOCEntries = std::map<std::pair<uint8_t, uint8_t>, ChannelQ>;
using SpiralCDTextKey = std::pair<uint8_t, uint8_t>;

constexpr int32_t SPIRAL_BATCH_SIZE = 256;


struct SpiralCDText
{
    std::map<SpiralCDTextKey, CD_TEXT_Descriptor> packs;
    std::map<SpiralCDTextKey, SpiralCDTextKey> next;
    std::optional<SpiralCDTextKey> previous;
};


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
    // stock firmware publishes ADR 1 entries without the lead-in running time of the Q frame they were read from
    if(q.adr == 1)
        std::fill(std::begin(descriptor.msf), std::end(descriptor.msf), 0);
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
            tracks[track_number] = { 0, q.control, q.adr, track_number, 0, endian_swap((uint32_t)BCDMSF_to_LBA(q.mode1.a_msf)) };
        }

        const auto &leadout = entries.at({ 1, 0xA2 });
        tracks[CD_LEADOUT_TRACK_NUMBER] = { 0, leadout.control, leadout.adr, CD_LEADOUT_TRACK_NUMBER, 0, endian_swap((uint32_t)BCDMSF_to_LBA(leadout.mode1.a_msf)) };

        // stock firmware order: A0 to A2, tracks, then ADR 5 with B0 and C0 first
        std::vector<std::pair<uint8_t, uint8_t>> keys;
        for(const auto &[key, q] : entries)
            keys.push_back(key);
        auto rank = [](const std::pair<uint8_t, uint8_t> &key)
        {
            auto [adr, point] = key;
            if(adr == 1)
                return std::tuple(point >= 0xA0 ? 0 : 1, point);
            return std::tuple(point == 0xB0 ? 2 : (point == 0xC0 ? 3 : 4), point);
        };
        std::sort(keys.begin(), keys.end(), [&rank](const auto &a, const auto &b) { return rank(a) < rank(b); });
        for(const auto &key : keys)
        {
            auto descriptor = encode_spiral_full_toc_entry((uint8_t)(session_index + 1), entries.at(key));
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


// stock firmware response: track items last, numeric fields in binary, TNO and ZERO cleared
void encode_spiral_pma(const SpiralTOCEntries &pma, std::vector<uint8_t> &pma_buffer)
{
    pma_buffer.assign(sizeof(CMD_ParameterListHeader), 0);
    for(bool tracks : { false, true })
        for(const auto &[key, q] : pma)
        {
            if((q.adr == 1) != tracks)
                continue;

            FULL_TOC_Descriptor descriptor{
                .session_number = 0,
                .control = q.control,
                .adr = q.adr,
                .tno = 0,
                .point = bcd_decode(q.mode1.point_index),
                .msf = { q.mode1.msf.m,   q.mode1.msf.s,   q.mode1.msf.f   },
                .zero = 0,
                .p_msf = { q.mode1.a_msf.m, q.mode1.a_msf.s, q.mode1.a_msf.f }
            };
            std::span<uint8_t> bytes((uint8_t *)&descriptor, sizeof(descriptor));
            // ADR 2 keeps the disc identification digits in BCD and only the disc type from the second field
            if(q.adr == 2)
                bytes[8] = bytes[10] = 0;
            else
            {
                if(q.mode1.msf.m != 0xFF)
                    for(uint8_t offset : { 4, 5, 6 })
                        bytes[offset] = bcd_decode(bytes[offset]);
                for(uint8_t offset : { 8, 9, 10 })
                    bytes[offset] = bcd_decode(bytes[offset]);
            }
            pma_buffer.insert(pma_buffer.end(), bytes.begin(), bytes.end());
        }

    CMD_ParameterListHeader pma_header{
        endian_swap((uint16_t)(pma_buffer.size() - sizeof(uint16_t))), { 0, 0 }
    };
    std::memcpy(pma_buffer.data(), &pma_header, sizeof(pma_header));
}


// lead-in R-W carries 6-bit symbols without interleaving, every 24 symbols form one 18-byte pack
void collect_spiral_cd_text_packs(std::span<const uint8_t> sector_sub, SpiralCDText &cd_text)
{
    constexpr uint32_t SYMBOL_BITS = 6;
    constexpr uint32_t PACK_SYMBOLS = sizeof(CD_TEXT_Descriptor) * CHAR_BIT / SYMBOL_BITS;
    for(uint32_t pack_offset = 0; pack_offset < CD_SUBCODE_SIZE; pack_offset += PACK_SYMBOLS)
    {
        CD_TEXT_Descriptor pack;
        std::span<uint8_t> bytes((uint8_t *)&pack, sizeof(pack));
        for(uint32_t i = 0; i < PACK_SYMBOLS; i += 4)
        {
            uint32_t bits = 0;
            for(uint32_t j = 0; j < 4; ++j)
                bits = bits << SYMBOL_BITS | (sector_sub[pack_offset + i + j] & 0x3F);
            for(uint32_t j = 0; j < 3; ++j)
                bytes[i / 4 * 3 + j] = (uint8_t)(bits >> (2 - j) * CHAR_BIT);
        }

        if((pack.pack_type & 0xF0) == 0x80 && CRC16_GSM().update(bytes.data(), bytes.size() - sizeof(pack.crc)).final() == endian_swap(pack.crc))
        {
            SpiralCDTextKey key((uint8_t)pack.block_number, pack.sequence_number);
            cd_text.packs.emplace(key, pack);
            if(cd_text.previous)
                cd_text.next.emplace(*cd_text.previous, key);
            cd_text.previous = key;
        }
        else
            cd_text.previous.reset();
    }
}


// packs repeat cyclically in the lead-in, recorded order is restored by following adjacency from the lowest key
void encode_spiral_cd_text(const SpiralCDText &cd_text, std::vector<uint8_t> &cd_text_buffer)
{
    cd_text_buffer.assign(sizeof(CMD_ParameterListHeader), 0);
    auto append_pack = [&cd_text_buffer](const CD_TEXT_Descriptor &pack)
    {
        std::span<const uint8_t> bytes((const uint8_t *)&pack, sizeof(pack));
        cd_text_buffer.insert(cd_text_buffer.end(), bytes.begin(), bytes.end());
    };

    std::set<SpiralCDTextKey> visited;
    std::optional<SpiralCDTextKey> key = cd_text.packs.begin()->first;
    while(key && !visited.contains(*key))
    {
        visited.insert(*key);
        append_pack(cd_text.packs.at(*key));
        auto next = cd_text.next.find(*key);
        key = next == cd_text.next.end() ? std::nullopt : std::optional(next->second);
    }

    // packs never read adjacent to the restored chain
    for(const auto &[pack_key, pack] : cd_text.packs)
        if(!visited.contains(pack_key))
            append_pack(pack);

    CMD_ParameterListHeader cd_text_header{
        endian_swap((uint16_t)(cd_text_buffer.size() - sizeof(uint16_t))), { 0, 0 }
    };
    std::memcpy(cd_text_buffer.data(), &cd_text_header, sizeof(cd_text_header));
}


// entries are 96-byte raw subcode or 12-byte Q packets
export auto analyze_spiral_subcode(std::fstream &fs, uint32_t entry_size, int32_t sectors_count, std::span<uint8_t> buffer)
{
    auto batch_size = (int32_t)(buffer.size() / entry_size);
    std::vector<SpiralTOCEntries> sessions(1);
    std::optional<int32_t> zero_index;
    std::optional<std::pair<int32_t, int32_t>> zero_index_candidate;
    std::vector<std::pair<int32_t, int32_t>> segments;
    std::optional<ChannelQ> repeated_q;
    uint8_t repeat_count = 0;
    bool program_area_seen = false;
    std::optional<int32_t> lead_in_index;
    std::vector<SpiralCDText> cd_texts(1);
    SpiralTOCEntries pma;
    std::optional<ChannelQ> previous_q;
    LOG("analyzing subcode (sectors: {})", sectors_count);
    for(int32_t batch_index = 0; batch_index < sectors_count; batch_index += batch_size)
    {
        int32_t count = std::min(batch_size, sectors_count - batch_index);
        read_entry(fs, buffer.data(), entry_size, batch_index, count, 0, 0);
        for(int32_t i = 0; i < count; ++i)
        {
            int32_t sector_index = batch_index + i;
            std::span<const uint8_t> entry(buffer.data() + i * entry_size, entry_size);
            if(entry_size == CD_SUBCODE_SIZE && !program_area_seen)
                collect_spiral_cd_text_packs(entry, cd_texts.back());

            ChannelQ q = entry_size == CD_SUBCODE_SIZE ? subcode_extract_q(entry.data()) : *(const ChannelQ *)entry.data();
            if(!q.isValid())
            {
                repeated_q.reset();
                repeat_count = 0;
                previous_q.reset();
                continue;
            }

            // a PMA item repeats ahead of the first program area with only ZERO counting frames, which lead-in entries never do
            if(!zero_index && previous_q && !q.mode1.tno && q.adr >= 1 && q.adr <= 6)
            {
                ChannelQ comparison_q(*previous_q);
                ++comparison_q.mode1.zero;
                if(!std::memcmp(comparison_q.raw, q.raw, sizeof(q.raw)))
                    pma[{ (uint8_t)q.adr, q.mode1.point_index }] = q;
            }
            previous_q = q;

            // a junk frame with a valid CRC, as recorded in the PCA, must not place the program area, so two addresses have to agree
            if(q.adr == 1 && q.mode1.tno)
            {
                int32_t current_zero_index = sector_index - BCDMSF_to_LBA(q.mode1.a_msf);
                bool confirmed = zero_index_candidate && zero_index_candidate->first == current_zero_index;
                int32_t first_index = confirmed ? zero_index_candidate->second : sector_index;
                zero_index_candidate = std::pair(current_zero_index, sector_index);
                if(confirmed)
                {
                    program_area_seen = true;
                    if(!zero_index)
                    {
                        if(current_zero_index < 0)
                            throw_line("LBA 0 is outside the capture (sector index: {})", current_zero_index);
                        zero_index = current_zero_index;
                        segments.emplace_back(0, -*zero_index);
                    }
                    else if(int32_t previous_zero_index = segments.back().first - segments.back().second; current_zero_index != previous_zero_index)
                    {
                        // lead-in has no positional Q and continues into its program area, so a forward jump belongs ahead of it
                        if(lead_in_index && current_zero_index < previous_zero_index)
                            first_index = *lead_in_index;
                        segments.emplace_back(first_index, first_index - current_zero_index);
                    }
                    lead_in_index.reset();
                }
            }

            if((q.adr == 1 || q.adr == 5) && !q.mode1.tno)
            {
                if(program_area_seen)
                {
                    if(sessions.size() >= 99)
                        throw_line("too many captured sessions");
                    sessions.emplace_back();
                    cd_texts.emplace_back();
                    program_area_seen = false;
                    lead_in_index = sector_index;
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

    return std::tuple(std::move(sessions), std::move(segments), std::move(cd_texts), std::move(pma));
}


// later segments win at overlapping LBAs, so a sector they cover is never taken from an earlier segment
bool segment_superseded(const std::vector<std::pair<int32_t, int32_t>> &segments, size_t segment_index, int32_t sectors_count, int32_t lba)
{
    for(size_t i = segment_index + 1; i < segments.size(); ++i)
    {
        int32_t count = (i + 1 < segments.size() ? segments[i + 1].first : sectors_count) - segments[i].first;
        if(lba >= segments[i].second && lba < segments[i].second + count)
            return true;
    }

    return false;
}


export int redumper_spiral_refine(Context &, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    auto capture_prefix = options.spiraldrive_capture.empty() ? image_prefix : options.spiraldrive_capture;

    // the image is created by the first capture and refined by every following one
    uint32_t image_files_count = 0;
    for(const char *extension : { ".scram", ".state", ".subcode" })
        if(std::filesystem::exists(image_prefix + extension))
            ++image_files_count;
    if(image_files_count && image_files_count != 3)
        throw_line("image files are incomplete (image name: {})", options.image_name);
    bool create = !image_files_count;

    std::fstream fs_main(capture_prefix + ".main.raw", std::ios::in | std::ios::binary);
    std::fstream fs_c2(capture_prefix + ".c2.raw", std::ios::in | std::ios::binary);
    std::fstream fs_sub(capture_prefix + ".sub.raw", std::ios::in | std::ios::binary);
    if(!fs_main.is_open() || !fs_c2.is_open() || !fs_sub.is_open())
        throw_line("unable to open capture files (capture: {})", capture_prefix);
    fs_main.exceptions(std::ios::badbit | std::ios::failbit);
    fs_c2.exceptions(std::ios::badbit | std::ios::failbit);
    fs_sub.exceptions(std::ios::badbit | std::ios::failbit);

    std::vector<uint8_t> main_buffer(SPIRAL_BATCH_SIZE * CD_DATA_SIZE);
    std::vector<uint8_t> c2_buffer(SPIRAL_BATCH_SIZE * CD_C2_SIZE);
    std::vector<uint8_t> sub_buffer(SPIRAL_BATCH_SIZE * CD_SUBCODE_SIZE);

    auto sectors_count = (int32_t)scale_up(std::filesystem::file_size(capture_prefix + ".sub.raw"), CD_SUBCODE_SIZE);
    auto [sessions, segments, cd_texts, pma] = analyze_spiral_subcode(fs_sub, CD_SUBCODE_SIZE, sectors_count, sub_buffer);

    // the image needs a capture that reaches the program area, a wobble capture may end before it
    if(int32_t zero_index = -segments.front().second; zero_index >= sectors_count)
        throw_line("LBA 0 is outside the capture (sector index: {})", zero_index);

    LOG("segments:");
    int32_t previous_end = segments.front().second;
    for(size_t segment_index = 0; segment_index < segments.size(); ++segment_index)
    {
        const auto &[source_index, lba] = segments[segment_index];
        int32_t source_end = segment_index + 1 < segments.size() ? segments[segment_index + 1].first : sectors_count;
        int32_t count = source_end - source_index;
        int32_t lba_end = lba + count;
        LOG("  LBA: [{:6} .. {:6}), count: {:6}, jump: {:+}", lba, lba_end, count, lba - previous_end);
        previous_end = lba_end;
    }
    LOG("");

    // disc structure files belong to the capture that creates the image
    if(create)
    {
        if(!options.image_path.empty())
            std::filesystem::create_directories(options.image_path);

        std::vector<uint8_t> toc_buffer;
        std::vector<uint8_t> full_toc_buffer;
        encode_spiral_toc(sessions, toc_buffer, full_toc_buffer);
        write_vector(image_prefix + ".toc", toc_buffer);
        write_vector(image_prefix + ".fulltoc", full_toc_buffer);

        if(!pma.empty())
        {
            std::vector<uint8_t> pma_buffer;
            encode_spiral_pma(pma, pma_buffer);
            write_vector(image_prefix + ".pma", pma_buffer);
        }

        // shortcut: only the first session CD-TEXT is stored, extend once a disc with later session CD-TEXT is found
        if(!cd_texts.front().packs.empty())
        {
            std::vector<uint8_t> cd_text_buffer;
            encode_spiral_cd_text(cd_texts.front(), cd_text_buffer);
            write_vector(image_prefix + ".cdtext", cd_text_buffer);
        }
        for(size_t session_index = 1; session_index < cd_texts.size(); ++session_index)
            if(!cd_texts[session_index].packs.empty())
                LOG("warning: CD-TEXT not stored (session: {}, packs: {})", session_index + 1, cd_texts[session_index].packs.size());
    }

    auto mode = std::ios::in | std::ios::out | std::ios::binary;
    if(create)
        mode |= std::ios::trunc;
    std::fstream fs_scram(image_prefix + ".scram", mode);
    std::fstream fs_state(image_prefix + ".state", mode);
    std::fstream fs_subcode(image_prefix + ".subcode", mode);
    fs_scram.exceptions(std::ios::badbit | std::ios::failbit);
    fs_state.exceptions(std::ios::badbit | std::ios::failbit);
    fs_subcode.exceptions(std::ios::badbit | std::ios::failbit);

    std::vector<uint8_t> image_main_buffer(SPIRAL_BATCH_SIZE * CD_DATA_SIZE);
    std::vector<State> image_state_buffer(SPIRAL_BATCH_SIZE * CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> image_sub_buffer(SPIRAL_BATCH_SIZE * CD_SUBCODE_SIZE);
    std::vector<uint8_t> sector_protection(CD_DATA_SIZE_SAMPLES);
    int32_t data_updated = 0;
    int32_t subcode_updated = 0;
    for(size_t segment_index = 0; segment_index < segments.size(); ++segment_index)
    {
        const auto &[source_index, lba] = segments[segment_index];
        int32_t source_end = segment_index + 1 < segments.size() ? segments[segment_index + 1].first : sectors_count;
        for(int32_t batch_index = source_index; batch_index < source_end; batch_index += SPIRAL_BATCH_SIZE)
        {
            int32_t count = std::min(SPIRAL_BATCH_SIZE, source_end - batch_index);
            read_entry(fs_main, main_buffer.data(), CD_DATA_SIZE, batch_index, count, 0, 0);
            read_entry(fs_c2, c2_buffer.data(), CD_C2_SIZE, batch_index, count, 0, 0xFF);
            read_entry(fs_sub, sub_buffer.data(), CD_SUBCODE_SIZE, batch_index, count, 0, 0);

            int32_t batch_lba = lba + (batch_index - source_index);
            int32_t output_index = batch_lba - LBA_START;
            read_entry(fs_scram, image_main_buffer.data(), CD_DATA_SIZE, output_index, count, 0, 0);
            read_entry(fs_state, (uint8_t *)image_state_buffer.data(), CD_DATA_SIZE_SAMPLES, output_index, count, 0, (uint8_t)State::ERROR_SKIP);
            read_entry(fs_subcode, image_sub_buffer.data(), CD_SUBCODE_SIZE, output_index, count, 0, 0);

            // a sample is taken only where the capture state is better, data is never compared
            bool batch_data_updated = false;
            bool batch_subcode_updated = false;
            for(int32_t i = 0; i < count; ++i)
            {
                if(segment_superseded(segments, segment_index, sectors_count, batch_lba + i))
                    continue;

                auto sector_state = c2_to_state(c2_buffer.data() + i * CD_C2_SIZE, State::SUCCESS);
                std::span<State> image_state(image_state_buffer.data() + i * CD_DATA_SIZE_SAMPLES, CD_DATA_SIZE_SAMPLES);
                std::span<uint8_t> image_main(image_main_buffer.data() + i * CD_DATA_SIZE, CD_DATA_SIZE);
                std::span<const uint8_t> sector_main(main_buffer.data() + i * CD_DATA_SIZE, CD_DATA_SIZE);
                if(sector_data_state_update(image_state, image_main, sector_state, sector_main, sector_protection))
                {
                    batch_data_updated = true;
                    ++data_updated;
                }

                std::span<uint8_t> image_sub(image_sub_buffer.data() + i * CD_SUBCODE_SIZE, CD_SUBCODE_SIZE);
                std::span<const uint8_t> sector_sub(sub_buffer.data() + i * CD_SUBCODE_SIZE, CD_SUBCODE_SIZE);
                // an unset image Q always takes the capture sector, which is not an update when both are the same
                if(!std::equal(image_sub.begin(), image_sub.end(), sector_sub.begin()) && sector_subcode_update(image_sub, sector_sub))
                {
                    batch_subcode_updated = true;
                    ++subcode_updated;
                }
            }

            if(batch_data_updated)
            {
                write_entry(fs_scram, image_main_buffer.data(), CD_DATA_SIZE, output_index, count, 0, 0);
                write_entry(fs_state, (const uint8_t *)image_state_buffer.data(), CD_DATA_SIZE_SAMPLES, output_index, count, 0, (uint8_t)State::ERROR_SKIP);
            }
            if(batch_subcode_updated)
                write_entry(fs_subcode, image_sub_buffer.data(), CD_SUBCODE_SIZE, output_index, count, 0, 0);
            LOGC_RF("refining: {} / {}", batch_index + count, sectors_count);
        }
    }
    LOGC_RF("");
    LOGC("");

    // explicit close throws on a failed final flush, the destructor would silently ignore it
    fs_scram.close();
    fs_state.close();
    fs_subcode.close();

    LOG("refined {} sectors (data updated: {}, subcode updated: {})", sectors_count, data_updated, subcode_updated);

    return 0;
}
}
