module;
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <vector>
#include "throw_line.hh"

export module check;

import cd.cd;
import cd.cdrom;
import cd.common;
import cd.offset_manager;
import cd.protection;
import cd.scrambler;
import cd.subcode;
import cd.toc;
import common;
import crc.crc16_gsm;
import options;
import range;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

namespace
{
// Confidence level thresholds
constexpr uint32_t CONFIDENCE_HIGH_MIN_SAMPLES = 5;
constexpr uint32_t CONFIDENCE_MEDIUM_MIN_SAMPLES = 2;

// Display limits
constexpr size_t MAX_ERROR_LBAS_DISPLAY = 5;
}

enum class ConfidenceLevel
{
    NONE,
    LOW,
    MEDIUM,
    HIGH
};

const char *confidence_to_string(ConfidenceLevel level)
{
    switch(level)
    {
    case ConfidenceLevel::HIGH:
        return "high";
    case ConfidenceLevel::MEDIUM:
        return "medium";
    case ConfidenceLevel::LOW:
        return "low";
    case ConfidenceLevel::NONE:
        return "none";
    }
    return "unknown";
}


struct ConfidenceResult
{
    std::string value;
    uint32_t valid_samples;     // Count of the most common value (consensus)
    uint32_t total_valid_reads; // Sum of all successful reads
    uint32_t unique_values;
    ConfidenceLevel confidence;
};


ConfidenceResult calculate_confidence(const std::map<std::string, uint32_t> &samples)
{
    ConfidenceResult result{ "", 0, 0, 0, ConfidenceLevel::NONE };

    if(samples.empty())
        return result;

    auto best = std::max_element(samples.begin(), samples.end(), [](const auto &a, const auto &b) { return a.second < b.second; });

    result.value = best->first;
    result.valid_samples = best->second;
    result.unique_values = samples.size();

    // Calculate total valid reads (sum of all counts)
    result.total_valid_reads = 0;
    for(const auto &[value, count] : samples)
        result.total_valid_reads += count;

    if(best->second >= CONFIDENCE_HIGH_MIN_SAMPLES && samples.size() == 1)
    {
        // 5+ consistent samples, all agree
        result.confidence = ConfidenceLevel::HIGH;
    }
    else if(best->second >= CONFIDENCE_HIGH_MIN_SAMPLES || (best->second >= CONFIDENCE_MEDIUM_MIN_SAMPLES && samples.size() == 1))
    {
        // 5+ samples with some variance, or 2-4 consistent samples
        result.confidence = ConfidenceLevel::MEDIUM;
    }
    else
    {
        // Only 1 valid sample or multiple conflicting values
        result.confidence = ConfidenceLevel::LOW;
    }

    return result;
}


std::string format_lba_list(const std::vector<int32_t> &lbas, size_t max_count = MAX_ERROR_LBAS_DISPLAY)
{
    std::string result;
    for(size_t i = 0; i < std::min(max_count, lbas.size()); ++i)
    {
        if(i > 0)
            result += ", ";
        result += std::to_string(lbas[i]);
    }
    if(lbas.size() > max_count)
        result += ", ...";
    return result;
}


std::string format_indices(const std::vector<int32_t> &indices)
{
    std::string result;
    for(size_t i = 0; i < indices.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += std::format("{:02}:{:d}", (int)i, indices[i]);
    }
    return result;
}


std::string control_to_string(uint8_t control)
{
    std::string result;

    // Bit 2: 0=audio, 1=data
    if(control & 0x04)
        result += "data";
    else
        result += "audio";

    // Bit 1: 0=copy prohibited, 1=copy permitted
    if(control & 0x02)
        result += ", copy permitted";
    else
        result += ", copy prohibited";

    // Bit 3: 0=no pre-emphasis, 1=pre-emphasis
    if(control & 0x08)
        result += ", pre-emphasis";

    // Bit 0: 0=two channel, 1=four channel
    if(control & 0x01)
        result += ", four channel";

    return result;
}

void print_cdtext(const TOC::CDText &cdtext, const std::string &indent)
{
    if(!cdtext.title.empty())
        LOG("{}TITLE: {}", indent, cdtext.title);
    if(!cdtext.performer.empty())
        LOG("{}PERFORMER: {}", indent, cdtext.performer);
    if(!cdtext.songwriter.empty())
        LOG("{}SONGWRITER: {}", indent, cdtext.songwriter);
    if(!cdtext.composer.empty())
        LOG("{}COMPOSER: {}", indent, cdtext.composer);
    if(!cdtext.arranger.empty())
        LOG("{}ARRANGER: {}", indent, cdtext.arranger);
    if(!cdtext.message.empty())
        LOG("{}MESSAGE: {}", indent, cdtext.message);
    if(!cdtext.closed_info.empty())
        LOG("{}CLOSED INFO: {}", indent, cdtext.closed_info);
    if(!cdtext.mcn_isrc.empty())
        LOG("{}MCN/ISRC: {}", indent, cdtext.mcn_isrc);
}

struct QErrorAnalysis
{
    uint32_t position_errors = 0;
    uint32_t mcn_errors = 0;
    uint32_t isrc_errors = 0;
    std::vector<int32_t> isrc_error_lbas;
    std::vector<int32_t> mcn_error_lbas;
    std::string reconstructed_isrc;
    std::string reconstructed_mcn;
    ConfidenceLevel isrc_confidence = ConfidenceLevel::NONE;
    ConfidenceLevel mcn_confidence = ConfidenceLevel::NONE;
    uint32_t isrc_valid_samples = 0;     // Consensus count (most common value)
    uint32_t mcn_valid_samples = 0;      // Consensus count (most common value)
    uint32_t isrc_total_valid_reads = 0; // Sum of all successful reads
    uint32_t mcn_total_valid_reads = 0;  // Sum of all successful reads
    uint32_t isrc_unique_values = 0;
    uint32_t mcn_unique_values = 0;
};


QErrorAnalysis analyze_q_errors(const TOC::Session::Track &track, std::fstream &subcode_fs, int32_t lba_start, int32_t lba_end)
{
    QErrorAnalysis analysis;

    // Validate input
    if(lba_start >= lba_end)
        return analysis; // Return empty analysis

    std::vector<uint8_t> subcode(CD_SUBCODE_SIZE);
    std::vector<ChannelQ> q_data;
    q_data.reserve(lba_end - lba_start);

    // Load all Q subchannel data for this track
    for(int32_t lba = lba_start; lba < lba_end; ++lba)
    {
        read_entry(subcode_fs, subcode.data(), CD_SUBCODE_SIZE, lba - LBA_START, 1, 0, 0);
        auto q = subcode_extract_q(subcode.data());
        q_data.push_back(q);
    }

    // Collect all valid ISRC and MCN samples
    std::map<std::string, uint32_t> isrc_samples;
    std::map<std::string, uint32_t> mcn_samples;

    for(size_t i = 0; i < q_data.size(); ++i)
    {
        if(q_data[i].isValid())
        {
            if(q_data[i].adr == 2)
            {
                // Extract MCN and count occurrences
                std::string mcn = std::format("{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", q_data[i].mode23.mcn[0], q_data[i].mode23.mcn[1], q_data[i].mode23.mcn[2], q_data[i].mode23.mcn[3],
                    q_data[i].mode23.mcn[4], q_data[i].mode23.mcn[5], q_data[i].mode23.mcn[6]);
                mcn_samples[mcn]++;
            }
            else if(q_data[i].adr == 3)
            {
                // Extract ISRC and count occurrences
                std::string isrc = std::format("{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", q_data[i].mode23.isrc[0], q_data[i].mode23.isrc[1], q_data[i].mode23.isrc[2],
                    q_data[i].mode23.isrc[3], q_data[i].mode23.isrc[4], q_data[i].mode23.isrc[5], q_data[i].mode23.isrc[6], q_data[i].mode23.isrc[7]);
                isrc_samples[isrc]++;
            }
        }
    }

    // Analyze each Q sector
    for(size_t i = 0; i < q_data.size(); ++i)
    {
        int32_t lba = lba_start + i;

        if(!q_data[i].isValid())
        {
            // Try to determine what type of Q this should be
            // Most Q sectors are ADR mode 1 (position)
            if(i > 0 && q_data[i - 1].isValid() && q_data[i - 1].adr == 1)
            {
                analysis.position_errors++;
            }
            else if(i + 1 < q_data.size() && q_data[i + 1].isValid() && q_data[i + 1].adr == 1)
            {
                analysis.position_errors++;
            }
            else if(i > 0 && q_data[i - 1].isValid() && q_data[i - 1].adr == 2)
            {
                analysis.mcn_errors++;
                analysis.mcn_error_lbas.push_back(lba);
            }
            else if(i > 0 && q_data[i - 1].isValid() && q_data[i - 1].adr == 3)
            {
                analysis.isrc_errors++;
                analysis.isrc_error_lbas.push_back(lba);
            }
            else
            {
                // Default to position error
                analysis.position_errors++;
            }
        }
    }

    // Determine ISRC confidence and select best value
    auto isrc_result = calculate_confidence(isrc_samples);
    analysis.reconstructed_isrc = isrc_result.value;
    analysis.isrc_valid_samples = isrc_result.valid_samples;
    analysis.isrc_total_valid_reads = isrc_result.total_valid_reads;
    analysis.isrc_unique_values = isrc_result.unique_values;
    analysis.isrc_confidence = isrc_result.confidence;

    // Determine MCN confidence and select best value
    auto mcn_result = calculate_confidence(mcn_samples);
    analysis.reconstructed_mcn = mcn_result.value;
    analysis.mcn_valid_samples = mcn_result.valid_samples;
    analysis.mcn_total_valid_reads = mcn_result.total_valid_reads;
    analysis.mcn_unique_values = mcn_result.unique_values;
    analysis.mcn_confidence = mcn_result.confidence;

    return analysis;
}


Errors count_refinable_errors(std::fstream &state_fs, std::fstream &subcode_fs, int32_t lba_start, int32_t lba_end, std::shared_ptr<const OffsetManager> offset_manager)
{
    Errors errors = { 0, 0, 0 };

    std::vector<State> sector_state(CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> sector_subcode(CD_SUBCODE_SIZE);

    // Count SCSI and C2 errors in state file
    for(int32_t lba = lba_start; lba < lba_end; ++lba)
    {
        read_entry(state_fs, (uint8_t *)sector_state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);

        errors.scsi += std::count(sector_state.begin(), sector_state.end(), State::ERROR_SKIP);
        errors.c2 += std::count(sector_state.begin(), sector_state.end(), State::ERROR_C2);
    }

    // Count Q errors in subcode file
    for(int32_t lba = lba_start; lba < lba_end; ++lba)
    {
        read_entry(subcode_fs, sector_subcode.data(), CD_SUBCODE_SIZE, lba - LBA_START, 1, 0, 0);
        if(!subcode_extract_q(sector_subcode.data()).isValid())
            ++errors.q;
    }

    return errors;
}


static bool sector_is_protected(int32_t lba, std::shared_ptr<const OffsetManager> offset_manager, const std::vector<Range<int32_t>> &protection)
{
    int32_t sample = lba_to_sample(lba, offset_manager->getOffset(lba));
    int32_t sample_end = sample + (int32_t)CD_DATA_SIZE_SAMPLES - 1;

    // Check if sector range [sample, sample_end] intersects with any protection range
    for(const auto &range : protection)
    {
        // Two ranges [a, b) and [c, d) intersect if: a < d AND c < b
        // Since ranges are half-open [start, end), check: range.start < sample_end+1 AND sample < range.end
        if(range.start <= sample_end && sample < range.end)
            return true;
    }

    return false;
}


static uint32_t find_non_zero_range(std::fstream &scm_fs, std::fstream &state_fs, int32_t lba_start, int32_t lba_end, std::shared_ptr<const OffsetManager> offset_manager, bool data_track,
    bool reverse)
{
    int32_t step = 1;
    if(reverse)
    {
        std::swap(lba_start, lba_end);
        --lba_start;
        --lba_end;
        step = -1;
    }

    Scrambler scrambler;

    int32_t lba = lba_start;
    for(; lba != lba_end; lba += step)
    {
        std::vector<uint8_t> sector(CD_DATA_SIZE);
        read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offset_manager->getOffset(lba) * CD_SAMPLE_SIZE, 0);

        std::vector<State> state(CD_DATA_SIZE_SAMPLES);
        read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);

        // skip all incomplete / erroneous sectors
        bool skip = false;
        for(auto const &s : state)
            if(s == State::ERROR_SKIP || s == State::ERROR_C2)
            {
                skip = true;
                break;
            }
        if(skip)
            continue;

        auto data = (uint32_t *)sector.data();
        uint64_t data_size = sector.size();
        if(data_track)
        {
            scrambler.descramble(sector.data(), &lba);

            auto s = (Sector *)sector.data();
            if(s->header.mode == 0)
            {
                // Mode 0 has 2336 bytes of user data immediately after sync (12 bytes) + header (4 bytes)
                data = (uint32_t *)(sector.data() + 16);
                data_size = MODE0_DATA_SIZE;
            }
            else if(s->header.mode == 1)
            {
                data = (uint32_t *)s->mode1.user_data;
                data_size = FORM1_DATA_SIZE;
            }
            else if(s->header.mode == 2)
            {
                if(s->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
                {
                    data = (uint32_t *)s->mode2.xa.form2.user_data;
                    data_size = FORM2_DATA_SIZE;
                }
                else
                {
                    data = (uint32_t *)s->mode2.xa.form1.user_data;
                    data_size = FORM1_DATA_SIZE;
                }
            }
        }
        data_size /= sizeof(uint32_t);

        if(!is_zeroed(data, data_size))
            break;
    }

    return reverse ? lba - lba_end : lba_end - lba;
}


bool check_tracks(const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, std::fstream &subcode_fs, std::shared_ptr<const OffsetManager> offset_manager,
    const std::vector<Range<int32_t>> &protection)
{
    bool no_errors = true;

    std::vector<State> state(CD_DATA_SIZE_SAMPLES);
    std::vector<uint8_t> subcode(CD_SUBCODE_SIZE);

    LOG("");
    LOG("Track Summary:");

    uint32_t total_skip_samples = 0;
    uint32_t total_c2_samples = 0;
    uint32_t total_skip_sectors = 0;
    uint32_t total_c2_sectors = 0;
    uint32_t total_q_errors = 0;

    for(auto const &se : toc.sessions)
    {
        // Show session header for multi-session discs
        if(toc.sessions.size() > 1)
        {
            LOG("");
            LOG("session {}:", se.session_number);
        }

        for(auto const &t : se.tracks)
        {
            // skip empty tracks
            if(t.lba_end == t.lba_start)
                continue;

            uint32_t skip_samples = 0;
            uint32_t c2_samples = 0;
            uint32_t skip_sectors = 0;
            uint32_t c2_sectors = 0;
            uint32_t q_errors = 0;

            uint32_t pregap_skip_samples = 0;
            uint32_t pregap_c2_samples = 0;
            uint32_t pregap_skip_sectors = 0;
            uint32_t pregap_c2_sectors = 0;
            uint32_t pregap_q_errors = 0;

            uint32_t music_skip_samples = 0;
            uint32_t music_c2_samples = 0;
            uint32_t music_skip_sectors = 0;
            uint32_t music_c2_sectors = 0;
            uint32_t music_q_errors = 0;

            // Determine boundary between pre-gap (index 00) and music (index 01+)
            int32_t music_start = t.indices.empty() ? t.lba_start : t.indices.front();
            bool has_pregap = !t.indices.empty() && t.lba_start < music_start;

            for(int32_t lba = t.lba_start; lba < t.lba_end; ++lba)
            {
                if(sector_is_protected(lba, offset_manager, protection))
                    continue;

                uint32_t skip_count = 0;
                uint32_t c2_count = 0;

                read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);
                for(auto const &s : state)
                {
                    if(s == State::ERROR_SKIP)
                        ++skip_count;
                    else if(s == State::ERROR_C2)
                        ++c2_count;
                }

                // Check Q subchannel
                read_entry(subcode_fs, subcode.data(), CD_SUBCODE_SIZE, lba - LBA_START, 1, 0, 0);
                auto q = subcode_extract_q(subcode.data());
                bool q_invalid = !q.isValid();

                // Categorize errors by pre-gap vs music
                bool is_pregap = has_pregap && lba < music_start;

                if(skip_count)
                {
                    skip_samples += skip_count;
                    ++skip_sectors;
                    if(is_pregap)
                    {
                        pregap_skip_samples += skip_count;
                        ++pregap_skip_sectors;
                    }
                    else
                    {
                        music_skip_samples += skip_count;
                        ++music_skip_sectors;
                    }
                }

                if(c2_count)
                {
                    c2_samples += c2_count;
                    ++c2_sectors;
                    if(is_pregap)
                    {
                        pregap_c2_samples += c2_count;
                        ++pregap_c2_sectors;
                    }
                    else
                    {
                        music_c2_samples += c2_count;
                        ++music_c2_sectors;
                    }
                }

                if(q_invalid)
                {
                    ++q_errors;
                    if(is_pregap)
                        ++pregap_q_errors;
                    else
                        ++music_q_errors;
                }
            }

            LOG("  track {}:", toc.getTrackString(t.track_number));

            // Track metadata
            LOG("    type: {}", control_to_string(t.control));

            if(!t.isrc.empty())
                LOG("    ISRC (TOC): {}", t.isrc);

            if(!t.cd_text.empty())
            {
                LOG("    CD-TEXT:");
                for(const auto &cdtext : t.cd_text)
                    print_cdtext(cdtext, "      ");
            }

            if(!t.indices.empty())
            {
                LOG("    indices: {}", format_indices(t.indices));
            }

            // Show breakdown if track has pre-gap
            if(has_pregap)
            {
                LOG("    index 00 (pre-gap): SKIP: {}, C2: {}, Q: {}", pregap_skip_samples, pregap_c2_samples, pregap_q_errors);
                LOG("    index 01+ (music): SKIP: {}, C2: {}, Q: {}", music_skip_samples, music_c2_samples, music_q_errors);
            }
            else
            {
                LOG("    SKIP: {}", skip_samples);
                LOG("    C2: {}", c2_samples);
                LOG("    Q: {}", q_errors);
            }

            // Detailed Q error analysis if there are Q errors
            if(q_errors > 0)
            {
                auto q_analysis = analyze_q_errors(t, subcode_fs, t.lba_start, t.lba_end);

                LOG("    Q error analysis:");
                if(q_analysis.position_errors > 0)
                    LOG("      recoverable position errors: {} of {} total", q_analysis.position_errors, q_errors);

                if(!q_analysis.isrc_error_lbas.empty())
                {
                    LOG("      ISRC read failures: {} at LBAs: {}", q_analysis.isrc_errors, format_lba_list(q_analysis.isrc_error_lbas));
                }

                if(!q_analysis.mcn_error_lbas.empty())
                {
                    LOG("      MCN read failures: {} at LBAs: {}", q_analysis.mcn_errors, format_lba_list(q_analysis.mcn_error_lbas));
                }

                if(!q_analysis.reconstructed_isrc.empty())
                {
                    // Decode the raw hex ISRC to human-readable format
                    uint8_t isrc_bytes[8];
                    for(size_t i = 0; i < 8; ++i)
                    {
                        char hex_byte[3] = { q_analysis.reconstructed_isrc[i * 2], q_analysis.reconstructed_isrc[i * 2 + 1], '\0' };
                        isrc_bytes[i] = std::stoi(hex_byte, nullptr, 16);
                    }
                    std::string decoded_isrc = decode_isrc(isrc_bytes);

                    LOG("      reconstructed ISRC: {}", decoded_isrc);
                    LOG("        raw: {}", q_analysis.reconstructed_isrc);
                    LOG("        samples:");
                    LOG("          consensus: {}", q_analysis.isrc_valid_samples);
                    LOG("          unique values: {}", q_analysis.isrc_unique_values);
                    LOG("          attempts: {}", q_analysis.isrc_total_valid_reads + q_analysis.isrc_errors);
                    LOG("        confidence: {}", confidence_to_string(q_analysis.isrc_confidence));
                }

                if(!q_analysis.reconstructed_mcn.empty())
                {
                    // Decode the raw hex MCN to human-readable format
                    uint8_t mcn_bytes[7];
                    for(size_t i = 0; i < 7; ++i)
                    {
                        char hex_byte[3] = { q_analysis.reconstructed_mcn[i * 2], q_analysis.reconstructed_mcn[i * 2 + 1], '\0' };
                        mcn_bytes[i] = std::stoi(hex_byte, nullptr, 16);
                    }
                    std::string decoded_mcn = decode_mcn(mcn_bytes);

                    LOG("      reconstructed MCN: {}", decoded_mcn);
                    LOG("        raw: {}", q_analysis.reconstructed_mcn);
                    LOG("        samples:");
                    LOG("          consensus: {}", q_analysis.mcn_valid_samples);
                    LOG("          unique values: {}", q_analysis.mcn_unique_values);
                    LOG("          attempts: {}", q_analysis.mcn_total_valid_reads + q_analysis.mcn_errors);
                    LOG("        confidence: {}", confidence_to_string(q_analysis.mcn_confidence));
                }
            }

            total_skip_samples += skip_samples;
            total_c2_samples += c2_samples;
            total_skip_sectors += skip_sectors;
            total_c2_sectors += c2_sectors;
            total_q_errors += q_errors;

            if(skip_sectors || c2_sectors)
                no_errors = false;
        }
    }

    LOG("");

    // Check if session lead-in/lead-out is isolated by one good sector
    for(uint32_t i = 0; i < toc.sessions.size(); ++i)
    {
        auto &t_s = toc.sessions[i].tracks.front();
        auto &t_e = toc.sessions[i].tracks.back();

        std::vector<State> state_boundary(CD_DATA_SIZE_SAMPLES);

        // Check lead-in boundary (only if sector is within recorded range)
        if(t_s.lba_start - 1 >= LBA_START)
        {
            read_entry(state_fs, (uint8_t *)state_boundary.data(), CD_DATA_SIZE_SAMPLES, t_s.lba_start - 1 - LBA_START, 1, -offset_manager->getOffset(t_s.lba_start - 1), (uint8_t)State::ERROR_SKIP);
            for(auto const &s : state_boundary)
                if(s == State::ERROR_SKIP)
                {
                    LOG("warning: lead-in starts with unavailable sector (session: {})", toc.sessions[i].session_number);
                    break;
                }
        }

        // Check lead-out boundary (only if sector is within recorded range)
        if(t_e.lba_end >= LBA_START)
        {
            read_entry(state_fs, (uint8_t *)state_boundary.data(), CD_DATA_SIZE_SAMPLES, t_e.lba_end - LBA_START, 1, -offset_manager->getOffset(t_e.lba_end), (uint8_t)State::ERROR_SKIP);
            for(auto const &s : state_boundary)
                if(s == State::ERROR_SKIP)
                {
                    LOG("warning: lead-out ends with unavailable sector (session: {})", toc.sessions[i].session_number);
                    break;
                }
        }
    }

    // Check session lead-out for non-zero data
    for(auto &s : toc.sessions)
    {
        auto &t = s.tracks.back();

        auto nonzero_count = find_non_zero_range(scm_fs, state_fs, t.lba_start, t.lba_end, offset_manager, t.control & (uint8_t)ChannelQ::Control::DATA, true);
        if(nonzero_count)
            LOG("warning: lead-out contains non-zero data (session: {}, sectors: {}/{})", s.session_number, nonzero_count, t.lba_end - t.lba_start);
    }

    // Check pre-gap completeness
    for(uint32_t i = 0; i < toc.sessions.size(); ++i)
    {
        auto &t = toc.sessions[i].tracks.front();

        int32_t pregap_end = i ? t.indices.front() : 0;
        int32_t pregap_start = pregap_end - CD_PREGAP_SIZE;

        uint32_t unavailable = 0;
        for(int32_t lba = pregap_start; lba != pregap_end; ++lba)
        {
            std::vector<State> state_pregap(CD_DATA_SIZE_SAMPLES);
            read_entry(state_fs, (uint8_t *)state_pregap.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);

            for(auto const &s : state_pregap)
                if(s == State::ERROR_SKIP)
                {
                    ++unavailable;
                    break;
                }
        }

        if(unavailable)
            LOG("warning: incomplete pre-gap (session: {}, unavailable: {}/{})", toc.sessions[i].session_number, unavailable, pregap_end - pregap_start);
    }

    LOG("");
    LOG("Overall Status:");
    LOG("  Total errors: SKIP: {} sector errors ({} sample errors), C2: {} sector errors ({} sample errors), Q: {} errors", total_skip_sectors, total_skip_samples, total_c2_sectors, total_c2_samples,
        total_q_errors);

    if(no_errors)
    {
        if(total_q_errors)
            LOG("  Status: Q errors detected - subchannel refine recommended");
        else
            LOG("  Status: CLEAN - ready to split");
    }
    else
    {
        LOG("  Status: ERRORS DETECTED - refine recommended");
    }

    LOG("");
    LOG("media errors: ");
    LOG("  SCSI: {}", total_skip_samples);
    LOG("  C2: {}", total_c2_samples);
    LOG("  Q: {}", total_q_errors);

    // Count refinable errors across all sessions
    LOG("");

    Errors refinable_errors = { 0, 0, 0 };
    for(auto const &se : toc.sessions)
    {
        // Refine only processes actual track data, not pre-gap or lead-out
        // Use first track's first index (index 1) as start, last non-lead-out track as end
        for(auto const &t : se.tracks)
        {
            // Skip lead-out track (AA/172)
            if(t.track_number >= 0xAA)
                continue;

            // Use track indices if available, otherwise use lba_start
            int32_t track_start = t.indices.empty() ? t.lba_start : t.indices.front();
            int32_t track_end = t.lba_end;

            auto track_errors = count_refinable_errors(state_fs, subcode_fs, track_start, track_end, offset_manager);
            refinable_errors.scsi += track_errors.scsi;
            refinable_errors.c2 += track_errors.c2;
            refinable_errors.q += track_errors.q;
        }
    }

    LOG("refinable errors:");
    LOG("  SCSI: {}", refinable_errors.scsi);
    LOG("  C2: {}", refinable_errors.c2);
    LOG("  Q: {}", refinable_errors.q);

    return no_errors;
}


export int redumper_check(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    // Check required files exist
    auto scm_path = image_prefix + ".scram";
    auto state_path = image_prefix + ".state";
    auto subcode_path = image_prefix + ".subcode";
    auto toc_path = image_prefix + ".toc";
    auto fulltoc_path = image_prefix + ".fulltoc";

    if(!std::filesystem::exists(scm_path))
        throw_line("scram file doesn't exist ({})", scm_path);
    if(!std::filesystem::exists(state_path))
        throw_line("state file doesn't exist ({})", state_path);
    if(!std::filesystem::exists(subcode_path))
        throw_line("subcode file doesn't exist ({})", subcode_path);
    if(!std::filesystem::exists(toc_path))
        throw_line("toc file doesn't exist ({})", toc_path);

    // Open files (read-only)
    std::fstream scm_fs(scm_path, std::fstream::in | std::fstream::binary);
    if(!scm_fs.is_open())
        throw_line("unable to open file ({})", scm_path);

    std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
    if(!state_fs.is_open())
        throw_line("unable to open file ({})", state_path);

    std::fstream subcode_fs(subcode_path, std::fstream::in | std::fstream::binary);
    if(!subcode_fs.is_open())
        throw_line("unable to open file ({})", subcode_path);

    // Load TOC
    std::vector<uint8_t> toc_buffer = read_vector(toc_path);
    std::vector<uint8_t> full_toc_buffer;
    if(std::filesystem::exists(fulltoc_path))
        full_toc_buffer = read_vector(fulltoc_path);
    auto toc = toc_choose(toc_buffer, full_toc_buffer);

    // Load CD-TEXT if available
    auto cdtext_path = image_prefix + ".cdtext";
    if(std::filesystem::exists(cdtext_path))
    {
        std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);
        toc.updateCDTEXT(cdtext_buffer);
    }

    // Generate index 0 entries (needed for track boundaries)
    toc.generateIndex0();

    // Determine offset
    int32_t offset = 0;
    if(options.force_offset)
    {
        offset = *options.force_offset;
        LOG("offset: {}", offset);
    }
    else
    {
        LOG("warning: no offset specified, using 0 (use --force-offset if incorrect)");
    }

    auto offset_manager = std::make_shared<OffsetManager>(std::vector<std::pair<int32_t, int32_t>>{
        { std::numeric_limits<int32_t>::min(), offset }
    });

    std::vector<Range<int32_t>> protection;
    protection_ranges_from_lba_ranges(protection, string_to_ranges(options.skip), 0);

    // Display disc-level metadata
    LOG("");
    LOG("disc metadata:");

    if(!toc.mcn.empty())
        LOG("  MCN: {}", toc.mcn);

    if(!toc.cd_text.empty())
    {
        LOG("  CD-TEXT:");
        for(size_t i = 0; i < toc.cd_text.size(); ++i)
        {
            if(toc.cd_text.size() > 1)
                LOG("    language {}:", i < toc.cd_text_lang.size() ? (int)toc.cd_text_lang[i] : (int)i);
            print_cdtext(toc.cd_text[i], toc.cd_text.size() > 1 ? "      " : "    ");
        }
    }

    LOG("  sessions: {}", toc.sessions.size());
    LOG("  total tracks: {}",
        [&]()
        {
            uint32_t count = 0;
            for(auto const &se : toc.sessions)
                count += se.tracks.size();
            return count;
        }());

    // Check tracks
    if(!check_tracks(toc, scm_fs, state_fs, subcode_fs, offset_manager, protection))
        exit_code = 1;

    return exit_code;
}

}
