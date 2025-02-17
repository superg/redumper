module;
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "throw_line.hh"

export module cd.split;

import analyzers.analyzer;
import analyzers.silence_analyzer;
import analyzers.sync_analyzer;
import cd.cd;
import cd.cdrom;
import cd.ecc;
import cd.edc;
import cd.offset_manager;
import cd.scrambler;
import cd.subcode;
import cd.toc;
import dump;
import filesystem.iso9660;
import hash.sha1;
import options;
import readers.image_bin_form1_reader;
import rom_entry;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

const uint32_t OFFSET_DEVIATION_MAX = CD_PREGAP_SIZE * CD_DATA_SIZE_SAMPLES;
const uint32_t OFFSET_SHIFT_MAX_SECTORS = 4;
const uint32_t OFFSET_SHIFT_SYNC_TOLERANCE = 2;


int32_t byte_offset_by_magic(int32_t lba_start, int32_t lba_end, std::fstream &state_fs, std::fstream &scm_fs, const std::vector<uint8_t> &magic)
{
    int32_t write_offset = std::numeric_limits<int32_t>::max();

    if(lba_start > lba_end)
    {
        return write_offset;
    }
    const uint32_t sectors_to_check = lba_end - lba_start;

    std::vector<uint8_t> data(sectors_to_check * CD_DATA_SIZE);
    std::vector<State> state(sectors_to_check * CD_DATA_SIZE_SAMPLES);

    read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba_start - LBA_START, sectors_to_check, 0, 0);
    read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba_start - LBA_START, sectors_to_check, 0, (uint8_t)State::ERROR_SKIP);

    bool data_correct = true;
    for(auto const &s : state)
        if(s == State::ERROR_SKIP || s == State::ERROR_C2)
        {
            data_correct = false;
            continue;
        }

    if(data_correct)
    {
        auto it = std::search(data.begin(), data.end(), magic.begin(), magic.end());
        if(it != data.end())
            write_offset = (uint32_t)(it - data.begin());
    }

    return write_offset;
}


int32_t iso9660_trim_if_needed(Context &ctx, const TOC::Session::Track &t, std::fstream &scm_fs, bool scrap, std::shared_ptr<const OffsetManager> offset_manager, const Options &options)
{
    int32_t lba_end = t.lba_end;

    if((ctx.protection_trim && *ctx.protection_trim || options.iso9660_trim) && t.control & (uint8_t)ChannelQ::Control::DATA && !t.indices.empty())
    {
        uint32_t file_offset = (t.indices.front() - LBA_START) * CD_DATA_SIZE + offset_manager->getOffset(t.indices.front()) * CD_SAMPLE_SIZE;
        auto form1_reader = std::make_unique<Image_BIN_Form1Reader>(scm_fs, file_offset, t.lba_end - t.indices.front(), !scrap);

        iso9660::PrimaryVolumeDescriptor pvd;
        if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
            lba_end = t.indices.front() + pvd.volume_space_size.lsb;
    }

    return lba_end;
}


bool optional_track(uint32_t track_number)
{
    return track_number == 0x00 || track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER);
}


void fill_track_modes(Context &ctx, TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, std::shared_ptr<const OffsetManager> offset_manager,
    const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, bool scrap, const Options &options)
{
    Scrambler scrambler;
    std::vector<uint8_t> sector(CD_DATA_SIZE);
    std::vector<State> state(CD_DATA_SIZE_SAMPLES);

    // discs with offset shift usually have some corruption in a couple of transitional sectors preventing normal descramble detection,
    // as everything is scrambled in this case, force descrambling
    bool force_descramble = offset_manager->isVariable();

    for(auto &s : toc.sessions)
    {
        for(auto &t : s.tracks)
        {
            // skip empty tracks
            if(t.lba_end == t.lba_start)
                continue;

            // skip audio tracks
            if(!(t.control & (uint8_t)ChannelQ::Control::DATA))
                continue;

            const uint8_t data_mode_invalid = 3;

            auto lba_end = iso9660_trim_if_needed(ctx, t, scm_fs, scrap, offset_manager, options);

            for(int32_t lba = t.lba_start; lba < lba_end; ++lba)
            {
                // skip erroneous sectors
                read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);
                if(std::any_of(state.begin(), state.end(), [](State s) { return s == State::ERROR_SKIP || s == State::ERROR_C2; }))
                    continue;

                read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offset_manager->getOffset(lba) * CD_SAMPLE_SIZE, 0);

                bool success = true;
                if(force_descramble)
                    scrambler.process(sector.data(), sector.data(), 0, sector.size());
                else
                    success = scrambler.descramble(sector.data(), &lba);

                if(success)
                {
                    auto mode = ((Sector *)sector.data())->header.mode;
                    if(mode < data_mode_invalid)
                    {
                        t.data_mode = mode;

                        // some systems have mixed mode gaps (SS), prioritize index1 mode
                        if(t.indices.empty() || lba >= t.indices.front())
                            break;
                    }
                }
            }
        }
    }
}


bool check_tracks(Context &ctx, const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, std::shared_ptr<const OffsetManager> offset_manager,
    const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, bool scrap, const Options &options)
{
    bool no_errors = true;

    std::vector<State> state(CD_DATA_SIZE_SAMPLES);

    for(auto const &se : toc.sessions)
    {
        for(auto const &t : se.tracks)
        {
            // skip empty tracks
            if(t.lba_end == t.lba_start)
                continue;

            bool data_track = t.control & (uint8_t)ChannelQ::Control::DATA;

            uint32_t skip_samples = 0;
            uint32_t c2_samples = 0;
            uint32_t skip_sectors = 0;
            uint32_t c2_sectors = 0;

            auto lba_end = iso9660_trim_if_needed(ctx, t, scm_fs, scrap, offset_manager, options);

            for(int32_t lba = t.lba_start; lba < lba_end; ++lba)
            {
                if(inside_range(lba, skip_ranges) != nullptr)
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

                if(skip_count)
                {
                    skip_samples += skip_count;
                    ++skip_sectors;
                }

                if(c2_count)
                {
                    c2_samples += c2_count;
                    ++c2_sectors;
                }
            }

            if(skip_sectors && !optional_track(t.track_number) || c2_sectors)
            {
                LOG("errors detected, track: {}, sectors: {{SKIP: {}, C2: {}}}, samples: {{SKIP: {}, C2: {}}}", toc.getTrackString(t.track_number), skip_sectors, c2_sectors, skip_samples, c2_samples);
                no_errors = false;
            }
        }
    }

    return no_errors;
}


std::vector<std::string> write_tracks(Context &ctx, const TOC &toc, std::fstream &scm_fs, std::fstream &state_fs, std::shared_ptr<const OffsetManager> offset_manager,
    const std::vector<std::pair<int32_t, int32_t>> &skip_ranges, bool scrap, const Options &options)
{
    std::vector<std::string> xml_lines;

    Scrambler scrambler;
    std::vector<uint8_t> sector(CD_DATA_SIZE);
    std::vector<State> state(CD_DATA_SIZE_SAMPLES);

    // discs with offset shift usually have some corruption in a couple of transitional sectors preventing normal descramble detection,
    // as everything is scrambled in this case, force descrambling
    bool force_descramble = offset_manager->isVariable();

    for(auto &s : toc.sessions)
    {
        for(auto &t : s.tracks)
        {
            // skip empty tracks
            if(t.lba_end == t.lba_start)
                continue;

            bool data_track = t.control & (uint8_t)ChannelQ::Control::DATA;

            std::string track_string = toc.getTrackString(t.track_number);
            bool lilo = t.track_number == 0x00 || t.track_number == bcd_decode(CD_LEADOUT_TRACK_NUMBER);

            // add session number to lead-in/lead-out track string to make filename unique
            if(lilo && toc.sessions.size() > 1)
                track_string = std::format("{}.{}", track_string, s.session_number);

            std::string track_name = std::format("{}{}.bin", options.image_name, toc.getTracksCount() > 1 || lilo ? std::format(" (Track {})", track_string) : "");

            if(std::filesystem::exists(std::filesystem::path(options.image_path) / track_name) && !options.overwrite)
                throw_line("file already exists ({})", track_name);

            std::fstream fs_bin(std::filesystem::path(options.image_path) / track_name, std::fstream::out | std::fstream::binary);
            if(!fs_bin.is_open())
                throw_line("unable to create file ({})", track_name);

            ROMEntry rom_entry(track_name);

            std::vector<std::pair<int32_t, int32_t>> descramble_errors;

            auto lba_end = iso9660_trim_if_needed(ctx, t, scm_fs, scrap, offset_manager, options);

            for(int32_t lba = t.lba_start; lba < lba_end; ++lba)
            {
                bool generate_sector = false;
                if(!options.leave_unchanged)
                {
                    read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);
                    generate_sector = std::any_of(state.begin(), state.end(), [](State s) { return s == State::ERROR_SKIP || s == State::ERROR_C2; });
                }

                // generate sector and fill it with fill byte (default: 0x55)
                if(generate_sector)
                {
                    // data
                    if(data_track)
                    {
                        Sector &s = *(Sector *)sector.data();
                        memcpy(s.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
                        s.header.address = LBA_to_BCDMSF(lba);
                        s.header.mode = t.data_mode;
                        memset(s.mode2.user_data, optional_track(t.track_number) ? 0x00 : options.skip_fill, sizeof(s.mode2.user_data));
                    }
                    // audio
                    else
                        memset(sector.data(), optional_track(t.track_number) ? 0x00 : options.skip_fill, sector.size());
                }
                else
                {
                    read_entry(scm_fs, sector.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offset_manager->getOffset(lba) * CD_SAMPLE_SIZE, 0);

                    // data: needs unscramble
                    if(data_track)
                    {
                        bool success = true;
                        if(force_descramble)
                            scrambler.process(sector.data(), sector.data(), 0, sector.size());
                        else
                            success = scrambler.descramble(sector.data(), &lba);

                        if(!success)
                        {
                            if(descramble_errors.empty() || descramble_errors.back().second + 1 != lba)
                                descramble_errors.emplace_back(lba, lba);
                            else
                                descramble_errors.back().second = lba;
                        }
                    }
                }

                rom_entry.update(sector.data(), sector.size());

                fs_bin.write((char *)sector.data(), sector.size());
                if(fs_bin.fail())
                    throw_line("write failed ({})", track_name);
            }

            for(auto const &d : descramble_errors)
            {
                if(d.first == d.second)
                    LOG("warning: descramble failed (LBA: {})", d.first);
                else
                    LOG("warning: descramble failed (LBA: [{} .. {}])", d.first, d.second);

                // DEBUG
                //                LOG("debug: scram offset: {:08X}", debug_get_scram_offset(d.first, write_offset));
            }

            xml_lines.emplace_back(rom_entry.xmlLine());
        }
    }

    return xml_lines;
}


bool toc_mismatch(const TOC &toc, const TOC &qtoc)
{
    bool mismatch = false;

    std::set<std::string> tracks;

    std::map<std::string, const TOC::Session::Track *> toc_tracks;
    for(auto const &s : toc.sessions)
        for(auto const &t : s.tracks)
        {
            toc_tracks[toc.getTrackString(t.track_number)] = &t;
            tracks.insert(toc.getTrackString(t.track_number));
        }

    std::map<std::string, const TOC::Session::Track *> qtoc_tracks;
    for(auto const &s : qtoc.sessions)
        for(auto const &t : s.tracks)
        {
            qtoc_tracks[toc.getTrackString(t.track_number)] = &t;
            tracks.insert(toc.getTrackString(t.track_number));
        }

    for(auto const &t : tracks)
    {
        auto tt = toc_tracks.find(t);
        auto qt = qtoc_tracks.find(t);

        if(tt != toc_tracks.end() && qt != qtoc_tracks.end())
        {
            if(tt->second->control != qt->second->control)
            {
                mismatch = true;
                LOG("warning: TOC / QTOC mismatch, control (track: {}, control: {:04b} <=> {:04b})", t, tt->second->control, qt->second->control);
            }

            if(tt->second->lba_start != qt->second->lba_start)
            {
                mismatch = true;
                LOG("warning: TOC / QTOC mismatch, track index 00 (track: {}, LBA: {} <=> {})", t, tt->second->lba_start, qt->second->lba_start);
            }

            auto tt_size = tt->second->indices.size();
            auto qt_size = qt->second->indices.size();
            if(tt_size == qt_size)
            {
                if(tt_size && qt_size && tt->second->indices.front() != qt->second->indices.front())
                {
                    mismatch = true;
                    LOG("warning: TOC / QTOC mismatch, track index 01 (track: {}, LBA: {} <=> {})", t, tt->second->indices.front(), qt->second->indices.front());
                }
            }
            else
            {
                mismatch = true;
                LOG("warning: TOC / QTOC mismatch, track index size (track: {})", t);
            }

            if(tt->second->lba_end != qt->second->lba_end)
            {
                mismatch = true;
                LOG("warning: TOC / QTOC mismatch, track length (track: {}, LBA: {} <=> {})", t, tt->second->lba_end, qt->second->lba_end);
            }
        }
        else
        {
            if(tt == toc_tracks.end())
            {
                mismatch = true;
                LOG("warning: TOC / QTOC mismatch, nonexistent track in TOC (track: {})", t);
            }

            if(qt == qtoc_tracks.end())
            {
                mismatch = true;
                LOG("warning: TOC / QTOC mismatch, nonexistent track in QTOC (track: {})", t);
            }
        }
    }

    return mismatch;
}


std::vector<std::pair<int32_t, int32_t>> audio_get_toc_index0_ranges(const TOC &toc)
{
    std::vector<std::pair<int32_t, int32_t>> index0_ranges;

    for(auto &s : toc.sessions)
    {
        for(auto &t : s.tracks)
        {
            int32_t index0_end = t.indices.empty() ? t.lba_end : t.indices.front();
            if(index0_end > t.lba_start)
                index0_ranges.emplace_back(t.lba_start * (int32_t)CD_DATA_SIZE_SAMPLES, index0_end * (int32_t)CD_DATA_SIZE_SAMPLES);
        }
    }

    return index0_ranges;
}


void analyze_scram_samples(std::fstream &scm_fs, std::fstream &state_fs, uint32_t samples_count, uint32_t batch_size, const std::list<std::shared_ptr<Analyzer>> &analyzers)
{
    std::vector<uint32_t> samples(batch_size);
    std::vector<State> state(batch_size);

    batch_process_range<uint32_t>(std::pair(0, samples_count), batch_size,
        [&scm_fs, &state_fs, &samples, &state, &analyzers](int32_t offset, int32_t size) -> bool
        {
            read_entry(scm_fs, (uint8_t *)samples.data(), CD_SAMPLE_SIZE, offset, size, 0, 0);
            read_entry(state_fs, (uint8_t *)state.data(), 1, offset, size, 0, (uint8_t)State::ERROR_SKIP);

            for(auto const &a : analyzers)
                a->process(samples.data(), state.data(), size, offset);

            return false;
        });
}


uint16_t disc_offset_by_silence(std::vector<std::pair<int32_t, int32_t>> &offset_ranges, const std::vector<std::pair<int32_t, int32_t>> &index0_ranges,
    const std::vector<std::vector<std::pair<int32_t, int32_t>>> &silence_ranges)
{
    for(uint16_t t = 0; t < silence_ranges.size(); ++t)
    {
        auto &silence_range = silence_ranges[t];

        for(int32_t sample_offset = -OFFSET_DEVIATION_MAX; sample_offset <= (int32_t)OFFSET_DEVIATION_MAX; ++sample_offset)
        {
            bool match = true;

            uint32_t cache_i = 0;
            for(auto const &r : index0_ranges)
            {
                bool found = false;

                std::pair<int32_t, int32_t> ir(r.first + sample_offset, r.second + sample_offset);

                for(uint32_t i = cache_i; i < silence_range.size(); ++i)
                {
                    bool ahead = ir.first >= silence_range[i].first;
                    if(ahead)
                        cache_i = i;

                    if(ahead && ir.second <= silence_range[i].second)
                    {
                        found = true;
                        break;
                    }

                    if(ir.second < silence_range[i].first)
                        break;
                }

                if(!found)
                {
                    match = false;
                    break;
                }
            }

            if(match)
            {
                if(offset_ranges.empty())
                {
                    offset_ranges.emplace_back(sample_offset, sample_offset);
                }
                else
                {
                    if(offset_ranges.back().second + 1 == sample_offset)
                        offset_ranges.back().second = sample_offset;
                    else
                        offset_ranges.emplace_back(sample_offset, sample_offset);
                }
            }
        }

        if(!offset_ranges.empty())
            return t;
    }

    return silence_ranges.size();
}


int32_t disc_offset_by_overlap(const TOC &toc, std::fstream &scm_fs, int32_t write_offset_data)
{
    int32_t write_offset = std::numeric_limits<int32_t>::max();

    for(auto &s : toc.sessions)
    {
        for(uint32_t t = 1; t < s.tracks.size(); ++t)
        {
            auto &t1 = s.tracks[t - 1];
            auto &t2 = s.tracks[t];

            if(t1.control & (uint8_t)ChannelQ::Control::DATA && !(t2.control & (uint8_t)ChannelQ::Control::DATA))
            {
                static constexpr uint32_t OVERLAP_COUNT = 10;

                uint32_t sectors_to_check = std::min(std::min((uint32_t)(t1.lba_end - t1.lba_start), (uint32_t)(t2.lba_end - t2.lba_start)), OVERLAP_COUNT);

                std::vector<uint32_t> t1_samples(sectors_to_check * CD_DATA_SIZE_SAMPLES);
                read_entry(scm_fs, (uint8_t *)t1_samples.data(), CD_DATA_SIZE, (t1.lba_end - sectors_to_check) - LBA_START, sectors_to_check, -write_offset_data * CD_SAMPLE_SIZE, 0);

                std::vector<uint32_t> t2_samples(sectors_to_check * CD_DATA_SIZE_SAMPLES);
                read_entry(scm_fs, (uint8_t *)t2_samples.data(), CD_DATA_SIZE, t2.lba_start - LBA_START, sectors_to_check, 0 * CD_SAMPLE_SIZE, 0);

                Scrambler scrambler;
                for(uint32_t i = 0; i < sectors_to_check; ++i)
                {
                    uint8_t *s = (uint8_t *)t1_samples.data() + i * CD_DATA_SIZE;
                    scrambler.process(s, s, 0, CD_DATA_SIZE);
                }

                for(auto it = t1_samples.begin(); it != t1_samples.end(); ++it)
                {
                    if(std::equal(it, t1_samples.end(), t2_samples.begin()))
                    {
                        write_offset = t1_samples.end() - it;
                        break;
                    }
                }

                break;
            }
        }

        if(write_offset != std::numeric_limits<int32_t>::max())
            break;
    }

    return write_offset;
}


int32_t find_non_zero_data_offset(std::fstream &scm_fs, std::fstream &state_fs, int32_t sample_offset_start, int32_t sample_offset_end, bool scrap)
{
    bool reverse = sample_offset_end < sample_offset_start;

    Scrambler scrambler;

    std::vector<State> state(CD_DATA_SIZE_SAMPLES);

    int32_t step = (reverse ? -1 : 1) * CD_DATA_SIZE_SAMPLES;
    if(reverse)
    {
        sample_offset_start += step;
        sample_offset_end += step;
    }

    int32_t sample_offset = sample_offset_start;
    for(; sample_offset != sample_offset_end; sample_offset += step)
    {
        read_entry(state_fs, (uint8_t *)state.data(), sizeof(State), sample_offset_r2a(sample_offset), CD_DATA_SIZE_SAMPLES, 0, (uint8_t)State::ERROR_SKIP);

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

        Sector sector;
        auto data = (uint8_t *)&sector;
        uint32_t data_size = sizeof(sector);

        read_entry(scm_fs, data, CD_SAMPLE_SIZE, sample_offset_r2a(sample_offset), CD_DATA_SIZE_SAMPLES, 0, 0);
        if(!scrap)
            scrambler.process(data, data, 0, data_size);

        if(sector.header.mode == 0)
        {
            data = sector.mode2.user_data;
            data_size = MODE0_DATA_SIZE;
        }
        else if(sector.header.mode == 1)
        {
            data = sector.mode1.user_data;
            data_size = FORM1_DATA_SIZE;
        }
        else if(sector.header.mode == 2)
        {
            if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
            {
                data = sector.mode2.xa.form2.user_data;
                data_size = FORM2_DATA_SIZE;
            }
            else
            {
                data = sector.mode2.xa.form1.user_data;
                data_size = FORM1_DATA_SIZE;
            }
        }

        if(!is_zeroed(data, data_size))
            break;
    }

    return sample_offset - (reverse ? step : 0);
}


uint32_t find_non_zero_range(std::fstream &scm_fs, std::fstream &state_fs, int32_t lba_start, int32_t lba_end, std::shared_ptr<const OffsetManager> offset_manager, bool data_track, bool reverse)
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
                data = (uint32_t *)s->mode2.user_data;
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


bool state_errors_in_range(std::fstream &state_fs, std::pair<int32_t, int32_t> nonzero_data_range)
{
    std::vector<State> state(CD_DATA_SIZE_SAMPLES * 1024);

    bool interrupted = batch_process_range<int32_t>(nonzero_data_range, state.size(),
        [&state_fs, &state](int32_t offset, int32_t size) -> bool
        {
            read_entry(state_fs, (uint8_t *)state.data(), sizeof(State), sample_offset_r2a(offset), size, 0, (uint8_t)State::ERROR_SKIP);

            return std::any_of(state.begin(), state.begin() + size, [](State s) { return s == State::ERROR_SKIP || s == State::ERROR_C2; });
        });

    return interrupted;
}


std::string calculate_universal_hash(std::fstream &scm_fs, std::pair<int32_t, int32_t> nonzero_data_range)
{
    SHA1 bh_sha1;

    std::vector<uint32_t> samples(10 * 1024 * 1024); // 10Mb chunk
    batch_process_range<int32_t>(nonzero_data_range, samples.size(),
        [&scm_fs, &samples, &bh_sha1](int32_t offset, int32_t size) -> bool
        {
            read_entry(scm_fs, (uint8_t *)samples.data(), CD_SAMPLE_SIZE, offset - LBA_START * CD_DATA_SIZE_SAMPLES, size, 0, 0);
            bh_sha1.update((uint8_t *)samples.data(), size * sizeof(uint32_t));

            return false;
        });

    return bh_sha1.final();
}


int32_t sample_offset_to_write_offset(int32_t scram_sample_offset, int32_t lba)
{
    return scram_sample_offset - lba * CD_DATA_SIZE_SAMPLES;
}


void disc_offset_normalize_records(std::vector<SyncAnalyzer::Record> &records, std::fstream &scm_fs, std::fstream &state_fs, const Options &options)
{
    // correct lead-in lba
    for(uint32_t i = 0; i < records.size(); ++i)
    {
        if(records[i].sample_offset <= 0 && (int32_t)(records[i].sample_offset + records[i].count * CD_DATA_SIZE_SAMPLES) > 0)
        {
            for(uint32_t j = i; j; --j)
            {
                auto &p = records[j - 1];

                uint32_t count = scale_up(records[j].sample_offset - p.sample_offset, CD_DATA_SIZE_SAMPLES);
                p.lba = records[j].lba - count;
            }

            break;
        }
    }

    // proactively erase false sync frames (if that ever happens)
    for(auto it = records.begin(); it != records.end();)
    {
        if(it->count == 1)
            it = records.erase(it);
        else
            ++it;
    }

    // merge offset groups
    for(bool merge = true; merge;)
    {
        merge = false;
        for(uint32_t i = 0; i + 1 < records.size(); ++i)
        {
            uint32_t offset_diff = records[i + 1].sample_offset - records[i].sample_offset;

            bool m = false;
            if(options.offset_shift_relocate)
            {
                int32_t range_diff = records[i + 1].lba - records[i].lba;
                m = range_diff * CD_DATA_SIZE_SAMPLES == offset_diff;
            }
            else
            {
                m = !(offset_diff % CD_DATA_SIZE_SAMPLES);
            }

            if(m)
            {
                records[i].count += records[i + 1].count;
                records.erase(records.begin() + i + 1);

                merge = true;
                break;
            }
        }
    }

    // shrink gaps between transitional sectors
    std::vector<uint8_t> data(CD_DATA_SIZE);
    for(uint32_t i = 0; i + 1 < records.size(); ++i)
    {
        auto &l = records[i];
        auto &r = records[i + 1];

        // skip aligned sectors
        uint32_t offset_diff = r.sample_offset - l.sample_offset;
        if(!(offset_diff % CD_DATA_SIZE_SAMPLES))
            continue;

        int32_t offset = sample_offset_to_write_offset(r.sample_offset, r.lba);
        uint32_t count = 0;
        for(int32_t lba = r.lba - 1; lba >= l.lba + l.count; --lba)
        {
            read_entry(scm_fs, data.data(), CD_DATA_SIZE, lba - LBA_START, 1, -offset * CD_SAMPLE_SIZE, 0);
            auto sync_diff = diff_bytes_count(data.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
            if(sync_diff <= OFFSET_SHIFT_SYNC_TOLERANCE)
                ++count;
            else
                break;
        }

        if(count)
        {
            r.lba -= count;
            r.sample_offset -= count * CD_DATA_SIZE_SAMPLES;
        }
    }
}


export void redumper_split_cd(Context &ctx, Options &options)
{
    image_check_empty(options);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path scm_path(image_prefix + ".scram");
    std::filesystem::path scp_path(image_prefix + ".scrap");
    std::filesystem::path sub_path(image_prefix + ".subcode");
    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path toc_path(image_prefix + ".toc");
    std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
    std::filesystem::path cdtext_path(image_prefix + ".cdtext");

    bool scrap = !std::filesystem::exists(scm_path) && std::filesystem::exists(scp_path);
    auto scra_path(scrap ? scp_path : scm_path);

    uint32_t subcode_sectors_count = check_file(sub_path, CD_SUBCODE_SIZE);

    std::fstream scm_fs(scra_path, std::fstream::in | std::fstream::binary);
    if(!scm_fs.is_open())
        throw_line("unable to open file ({})", scra_path.filename().string());

    std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
    if(!state_fs.is_open())
        throw_line("unable to open file ({})", state_path.filename().string());

    std::vector<uint8_t> toc_buffer = read_vector(toc_path);
    std::vector<uint8_t> full_toc_buffer;
    if(std::filesystem::exists(fulltoc_path))
        full_toc_buffer = read_vector(fulltoc_path);

    auto toc = toc_choose(toc_buffer, full_toc_buffer);

    // preload subchannel P/Q
    std::vector<uint8_t> subp;
    std::vector<ChannelQ> subq;
    if(std::filesystem::exists(sub_path))
    {
        std::vector<ChannelP> subp_raw;
        subcode_load_subpq(subp_raw, subq, sub_path);

        LOG_F("correcting P... ");
        subp = subcode_correct_subp(subp_raw.data(), subcode_sectors_count);
        LOG("done");

        LOG_F("correcting Q... ");
        if(!subcode_correct_subq(subq.data(), subcode_sectors_count))
            subq.clear();
        LOG("done");
        LOG("");
    }

    if(subq.empty())
    {
        LOG("warning: subchannel data is not available, generating TOC index 0 entries");
        toc.generateIndex0();
    }
    else
        toc.updateQ(subq.data(), subp.data(), subcode_sectors_count, LBA_START, options.legacy_subs);

    LOG("final TOC:");
    print_toc(toc);
    LOG("");

    if(!subq.empty())
    {
        TOC qtoc(subq.data(), subcode_sectors_count, LBA_START);

        // compare TOC and QTOC
        if(toc_mismatch(toc, qtoc))
        {
            LOG("");
            LOG("final QTOC:");
            print_toc(qtoc);
            LOG("");
        }

        if(options.force_qtoc)
        {
            toc = qtoc;
            LOG("warning: split is performed by QTOC");
            LOG("");
        }

        toc.updateMCN(subq.data(), subcode_sectors_count);
    }

    // CD-TEXT
    if(std::filesystem::exists(cdtext_path))
    {
        std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);

        toc.updateCDTEXT(cdtext_buffer);
    }

    std::list<std::shared_ptr<Analyzer>> analyzers;

    auto index0_ranges = audio_get_toc_index0_ranges(toc);

    uint32_t samples_min = std::numeric_limits<uint32_t>::max();
    std::for_each(index0_ranges.begin(), index0_ranges.end(), [&samples_min](const std::pair<int32_t, int32_t> &r) { samples_min = std::min(samples_min, (uint32_t)(r.second - r.first)); });
    auto silence_analyzer = std::make_shared<SilenceAnalyzer>(options.audio_silence_threshold, samples_min);
    analyzers.emplace_back(silence_analyzer);

    auto sync_analyzer = std::make_shared<SyncAnalyzer>(scrap);
    analyzers.emplace_back(sync_analyzer);

    LOG_F("analyzing... ");
    auto analysis_time_start = std::chrono::high_resolution_clock::now();
    analyze_scram_samples(scm_fs, state_fs, std::filesystem::file_size(scra_path), CD_DATA_SIZE_SAMPLES, analyzers);
    auto analysis_time_stop = std::chrono::high_resolution_clock::now();
    LOG("done (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(analysis_time_stop - analysis_time_start).count());
    LOG("");

    auto silence_ranges = silence_analyzer->ranges();

    std::pair<int32_t, int32_t> nonzero_toc_range(toc.sessions.front().tracks.front().lba_start * CD_DATA_SIZE_SAMPLES, toc.sessions.back().tracks.back().lba_start * CD_DATA_SIZE_SAMPLES);
    auto nonzero_data_range = std::pair(silence_ranges.front().front().second, silence_ranges.front().back().first);

    std::vector<std::pair<int32_t, int32_t>> offsets;

    // data track
    if(offsets.empty())
    {
        auto sync_records = sync_analyzer->getRecords();

        bool data_track = false;
        for(auto const &o : sync_records)
            if(o.count >= CD_PREGAP_SIZE)
            {
                data_track = true;
                break;
            }

        if(data_track)
        {
            LOG("data disc detected, offset configuration: ");
            for(auto const &o : sync_records)
            {
                MSF msf = LBA_to_BCDMSF(o.lba);
                LOG("  LBA: {:6} -> {:6}, MSF: {:02X}:{:02X}:{:02X}, count: {:6}, offset: {:+}", o.sample_offset / (int32_t)CD_DATA_SIZE_SAMPLES, o.lba, msf.m, msf.s, msf.f, o.count,
                    sample_offset_to_write_offset(o.sample_offset, o.lba));
            }
            LOG("");

            disc_offset_normalize_records(sync_records, scm_fs, state_fs, options);

            for(auto const &r : sync_records)
                offsets.emplace_back(r.lba, sample_offset_to_write_offset(r.sample_offset, r.lba));

            // align non-zero data range to data sector boundary and truncate leading / trailing empty sectors
            {
                auto const &f = sync_records.front();
                if(f.sample_offset - nonzero_data_range.first < CD_DATA_SIZE_SAMPLES)
                {
                    int32_t sample_offset_end = f.sample_offset + f.count * CD_DATA_SIZE_SAMPLES;
                    nonzero_data_range.first = find_non_zero_data_offset(scm_fs, state_fs, f.sample_offset, sample_offset_end, scrap);
                }

                auto const &b = sync_records.back();
                int32_t sample_offset_start = b.sample_offset + b.count * CD_DATA_SIZE_SAMPLES;
                if(nonzero_data_range.second - sample_offset_start < CD_DATA_SIZE_SAMPLES)
                    nonzero_data_range.second = find_non_zero_data_offset(scm_fs, state_fs, sample_offset_start, b.sample_offset, scrap);
            }
        }
    }

    LOG("non-zero  TOC sample range: [{:+9} .. {:+9}]", nonzero_toc_range.first, nonzero_toc_range.second);
    LOG("non-zero data sample range: [{:+9} .. {:+9}]", nonzero_data_range.first, nonzero_data_range.second);

    if(!scrap)
    {
        if(state_errors_in_range(state_fs, nonzero_data_range))
            LOG("warning: non-zero data range is not continuous, skipping Universal Hash calculation");
        else
            LOG("Universal Hash (SHA-1): {}", calculate_universal_hash(scm_fs, nonzero_data_range));
    }
    LOG("");

    if(scrap)
    {
        if(offsets.empty())
            throw_line("no data sectors detected in scrap mode");

        if(offsets.size() == 1)
        {
            int32_t write_offset_data = offsets.front().second;

            int32_t write_offset_audio = std::numeric_limits<int32_t>::max();
            if(options.force_offset)
                write_offset_audio = *options.force_offset;
            else
            {
                // try to detect positive offset based on scrambled data track overlap into audio
                write_offset_audio = disc_offset_by_overlap(toc, scm_fs, write_offset_data);
                if(write_offset_audio == std::numeric_limits<int32_t>::max())
                    write_offset_audio = 0;
                else
                    LOG("overlap offset detected");
            }

            // interleave data and audio offsets
            offsets.clear();
            for(auto const &s : toc.sessions)
                for(auto const &t : s.tracks)
                {
                    auto o = t.control & (uint8_t)ChannelQ::Control::DATA ? write_offset_data : write_offset_audio;
                    if(offsets.empty() || o != offsets.back().second)
                        offsets.emplace_back(t.lba_start, o);
                }
        }
        else
            LOG("warning: offset shift detected in scrap mode");
    }
    else
    {
        if(options.force_offset)
        {
            offsets.clear();
            offsets.emplace_back(0, *options.force_offset);
        }
    }

    // Atari Jaguar CD
    if(offsets.empty() && toc.sessions.size() == 2 && !(toc.sessions.back().tracks.front().control & (uint8_t)ChannelQ::Control::DATA))
    {
        auto &t = toc.sessions.back().tracks.front();

        if(!t.indices.empty())
        {
            constexpr std::string_view atari_magic("TAIRTAIR");
            int32_t byte_offset = byte_offset_by_magic(t.indices.front() - 1, t.indices.front() + 1, state_fs, scm_fs, std::vector<uint8_t>(atari_magic.begin(), atari_magic.end()));
            if(byte_offset != std::numeric_limits<int32_t>::max())
            {
                byte_offset -= sizeof(uint16_t);
                offsets.emplace_back(0, byte_offset / CD_SAMPLE_SIZE - CD_DATA_SIZE_SAMPLES);
                LOG("Atari Jaguar disc detected");
            }
        }
    }

    // VideoNow
    if(offsets.empty() && toc.sessions.size() == 1)
    {
        auto &t = toc.sessions.front().tracks.front();

        int32_t lba_start = sample_to_lba(nonzero_data_range.first);

        static const std::vector<uint8_t> videonow_magic = { 0xE1, 0xE1, 0xE1, 0x01, 0xE1, 0xE1, 0xE1, 0x00 };
        int32_t byte_offset = byte_offset_by_magic(lba_start, t.lba_end, state_fs, scm_fs, videonow_magic);
        if(byte_offset != std::numeric_limits<int32_t>::max())
        {
            offsets.emplace_back(0, byte_offset / CD_SAMPLE_SIZE + CD_DATA_SIZE_SAMPLES * lba_start);
            LOG("VideoNow disc detected");
        }
    }

    // VideoNow Color
    if(offsets.empty() && toc.sessions.size() == 1)
    {
        auto &t = toc.sessions.front().tracks.front();

        int32_t lba_start = sample_to_lba(nonzero_data_range.first);

        static const std::vector<uint8_t> videonow_magic_color = { 0x81, 0xE3, 0xE3, 0xC7, 0xC7, 0x81, 0x81, 0xE3 };
        int32_t byte_offset = byte_offset_by_magic(lba_start, t.lba_end, state_fs, scm_fs, videonow_magic_color);
        if(byte_offset != std::numeric_limits<int32_t>::max())
        {
            offsets.emplace_back(0, byte_offset / CD_SAMPLE_SIZE + CD_DATA_SIZE_SAMPLES * lba_start);
            LOG("VideoNow Color (Color / Jr. / XP / Color FX) disc detected");
        }
    }

    // perfect audio offset
    if(offsets.empty())
    {
        std::vector<std::pair<int32_t, int32_t>> offset_ranges;
        uint16_t silence_level = disc_offset_by_silence(offset_ranges, index0_ranges, silence_ranges);
        if(silence_level < silence_ranges.size())
        {
            std::string offset_string;
            for(uint32_t i = 0; i < offset_ranges.size(); ++i)
            {
                auto const &r = offset_ranges[i];

                if(r.first == r.second)
                    offset_string += std::format("{:+}{}", r.first, i + 1 == offset_ranges.size() ? "" : ", ");
                else
                    offset_string += std::format("[{:+} .. {:+}]{}", r.first, r.second, i + 1 == offset_ranges.size() ? "" : ", ");
            }
            LOG("Perfect Audio Offset (silence level: {}): {}", silence_level, offset_string);

            // only one perfect offset exists
            if(offset_ranges.size() == 1 && offset_ranges.front().first == offset_ranges.front().second && !silence_level)
            {
                offsets.emplace_back(0, offset_ranges.front().first);
                LOG("Perfect Audio Offset applied");
            }
        }
    }

    // move data
    if(offsets.empty())
    {
        int32_t toc_sample_size = nonzero_toc_range.second - nonzero_toc_range.first;
        int32_t data_sample_size = nonzero_data_range.second - nonzero_data_range.first;

        // attempt to move data only if sample data range fits into TOC calculated range
        if(data_sample_size <= toc_sample_size)
        {
            int32_t write_offset = std::numeric_limits<int32_t>::max();

            // move data out of lead-out
            if(nonzero_data_range.second > nonzero_toc_range.second)
            {
                write_offset = nonzero_data_range.second - nonzero_toc_range.second;
                LOG("moving data out of lead-out (difference: {:+})", write_offset);
            }
            // move data out of lead-in only if we can get rid of it whole
            else if(nonzero_data_range.first < 0 && data_sample_size <= nonzero_toc_range.second)
            {
                write_offset = nonzero_data_range.first;
                LOG("moving data out of lead-in (difference: {:+})", write_offset);
            }
            // move data out of TOC
            else if(nonzero_data_range.first < nonzero_toc_range.first && data_sample_size <= toc_sample_size)
            {
                write_offset = nonzero_data_range.first - nonzero_toc_range.first;
                LOG("moving data out of TOC (difference: {:+})", write_offset);
            }

            if(write_offset != std::numeric_limits<int32_t>::max())
                offsets.emplace_back(0, write_offset);
        }
    }

    // fallback
    if(offsets.empty())
    {
        offsets.emplace_back(0, 0);
        LOG("fallback offset 0 applied");
    }

    auto offset_manager = std::make_shared<const OffsetManager>(offsets);

    // FIXME: rework non-zero area detection
    if(!options.correct_offset_shift && !scrap && offsets.size() > 1)
    {
        LOG("warning: offset shift detected, to apply correction please use an option");

        offsets.clear();
        offsets.emplace_back(0, offset_manager->getOffset(0));
        offset_manager = std::make_shared<const OffsetManager>(offsets);
    }

    // output disc write offset
    {
        int32_t disc_write_offset = 0;
        if(scrap)
        {
            for(auto const &s : toc.sessions)
                for(auto const &t : s.tracks)
                    if(!(t.control & (uint8_t)ChannelQ::Control::DATA))
                    {
                        disc_write_offset = offset_manager->getOffset(t.lba_start);
                        break;
                    }
        }
        else
            disc_write_offset = offset_manager->getOffset(0);

        LOG("disc write offset: {:+}", disc_write_offset);
        LOG("");
    }

    if(!scrap && offsets.size() > 1)
    {
        LOG("offset shift correction applied: ");
        for(auto const &o : offsets)
            LOG("  LBA: {:6}, offset: {:+}", o.first, o.second);
        LOG("");
    }

    // identify CD-I tracks, needed for CUE-sheet generation
    for(auto &s : toc.sessions)
        for(auto &t : s.tracks)
            if(t.control & (uint8_t)ChannelQ::Control::DATA && !t.indices.empty() && t.track_number != bcd_decode(CD_LEADOUT_TRACK_NUMBER))
            {
                uint32_t file_offset = (t.indices.front() - LBA_START) * CD_DATA_SIZE + offset_manager->getOffset(t.indices.front()) * CD_SAMPLE_SIZE;
                auto form1_reader = std::make_unique<Image_BIN_Form1Reader>(scm_fs, file_offset, t.lba_end - t.indices.front(), !scrap);

                iso9660::PrimaryVolumeDescriptor pvd;
                if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY)
                    && !memcmp(pvd.standard_identifier, iso9660::STANDARD_IDENTIFIER_CDI, sizeof(pvd.standard_identifier)))
                    t.cdi = true;
            }

    // check if pre-gap is complete
    for(uint32_t i = 0; i < toc.sessions.size(); ++i)
    {
        auto &t = toc.sessions[i].tracks.front();

        int32_t pregap_end = i ? t.indices.front() : 0;
        int32_t pregap_start = pregap_end - CD_PREGAP_SIZE;

        uint32_t unavailable = 0;
        for(int32_t lba = pregap_start; lba != pregap_end; ++lba)
        {
            std::vector<State> state(CD_DATA_SIZE_SAMPLES);
            read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, lba - LBA_START, 1, -offset_manager->getOffset(lba), (uint8_t)State::ERROR_SKIP);

            for(auto const &s : state)
                if(s == State::ERROR_SKIP)
                {
                    ++unavailable;
                    break;
                }
        }

        if(unavailable)
            LOG("warning: incomplete pre-gap (session: {}, unavailable: {}/{})", toc.sessions[i].session_number, unavailable, pregap_end - pregap_start);
    }

    // check session pre-gap for non-zero data
    for(uint32_t i = 0; i < toc.sessions.size(); ++i)
    {
        auto &s = toc.sessions[i];
        auto &t = s.tracks.front();

        int32_t leadin_start = i ? toc.sessions[i - 1].tracks.back().lba_end : sample_to_lba(nonzero_data_range.first, offset_manager->getOffset(sample_to_lba(nonzero_data_range.first)));
        int32_t leadin_end = i ? t.indices.front() : 0;

        // do this before new track insertion
        t.lba_start = leadin_end;

        // if it's not empty, construct 00 track with non-zero data
        uint32_t nonzero_count = 0;
        if(leadin_end > leadin_start)
            nonzero_count = find_non_zero_range(scm_fs, state_fs, leadin_start, leadin_end, offset_manager, t.control & (uint8_t)ChannelQ::Control::DATA, false);
        if(nonzero_count)
        {
            auto t_00 = t;

            t_00.track_number = 0;
            t_00.lba_start = leadin_start;
            t_00.lba_end = leadin_end;
            t_00.indices.clear();

            s.tracks.insert(s.tracks.begin(), t_00);

            LOG("warning: lead-in contains non-zero data (session: {}, sectors: {}/{})", s.session_number, nonzero_count, leadin_end - leadin_start);
        }
    }

    // check session lead-out for non-zero data
    for(auto &s : toc.sessions)
    {
        auto &t = s.tracks.back();

        auto nonzero_count = find_non_zero_range(scm_fs, state_fs, t.lba_start, t.lba_end, offset_manager, t.control & (uint8_t)ChannelQ::Control::DATA, true);
        if(nonzero_count)
            LOG("warning: lead-out contains non-zero data (session: {}, sectors: {}/{})", s.session_number, nonzero_count, t.lba_end - t.lba_start);

        t.lba_end = t.lba_start + nonzero_count;
    }

    // check if session lead-in/lead-out is isolated by one good sector
    for(uint32_t i = 0; i < toc.sessions.size(); ++i)
    {
        auto &t_s = toc.sessions[i].tracks.front();
        auto &t_e = toc.sessions[i].tracks.back();

        std::vector<State> state(CD_DATA_SIZE_SAMPLES);

        read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, t_s.lba_start - 1 - LBA_START, 1, -offset_manager->getOffset(t_s.lba_start - 1), (uint8_t)State::ERROR_SKIP);
        for(auto const &s : state)
            if(s == State::ERROR_SKIP)
            {
                LOG("warning: lead-in starts with unavailable sector (session: {})", toc.sessions[i].session_number);
                break;
            }

        read_entry(state_fs, (uint8_t *)state.data(), CD_DATA_SIZE_SAMPLES, t_e.lba_end - LBA_START, 1, -offset_manager->getOffset(t_e.lba_end), (uint8_t)State::ERROR_SKIP);
        for(auto const &s : state)
            if(s == State::ERROR_SKIP)
            {
                LOG("warning: lead-out ends with unavailable sector (session: {})", toc.sessions[i].session_number);
                break;
            }
    }

    std::vector<std::pair<int32_t, int32_t>> skip_ranges = string_to_ranges(options.skip);
    // FIXME: negated offset is confusing, simplify
    std::vector<std::pair<int32_t, int32_t>> protection_ranges = get_protection_sectors(ctx, -offset_manager->getOffset(0));
    skip_ranges.insert(skip_ranges.begin(), protection_ranges.begin(), protection_ranges.end());

    // determine data track modes
    fill_track_modes(ctx, toc, scm_fs, state_fs, offset_manager, skip_ranges, scrap, options);

    // check tracks
    LOG("checking tracks");
    if(!check_tracks(ctx, toc, scm_fs, state_fs, offset_manager, skip_ranges, scrap, options) && !options.force_split)
        throw_line("data errors detected, unable to continue");
    LOG("done");
    LOG("");

    // write tracks
    LOG("writing tracks");
    ctx.dat = write_tracks(ctx, toc, scm_fs, state_fs, offset_manager, skip_ranges, scrap, options);
    LOG("done");
    LOG("");

    // write CUE-sheet
    std::vector<std::string> cue_sheets;
    if(toc.cd_text_lang.size() > 1)
    {
        cue_sheets.resize(toc.cd_text_lang.size());
        for(uint32_t i = 0; i < toc.cd_text_lang.size(); ++i)
        {
            cue_sheets[i] = i ? std::format("{}_{:02X}.cue", options.image_name, toc.cd_text_lang[i]) : std::format("{}.cue", options.image_name);

            if(std::filesystem::exists(std::filesystem::path(options.image_path) / cue_sheets[i]) && !options.overwrite)
                throw_line("file already exists ({})", cue_sheets[i]);

            std::fstream fs(std::filesystem::path(options.image_path) / cue_sheets[i], std::fstream::out);
            if(!fs.is_open())
                throw_line("unable to create file ({})", cue_sheets[i]);
            toc.printCUE(fs, options.image_name, i);
        }
    }
    else
    {
        cue_sheets.push_back(std::format("{}.cue", options.image_name));

        if(std::filesystem::exists(std::filesystem::path(options.image_path) / cue_sheets.front()) && !options.overwrite)
            throw_line("file already exists ({})", cue_sheets.front());

        std::fstream fs(std::filesystem::path(options.image_path) / cue_sheets.front(), std::fstream::out);
        if(!fs.is_open())
            throw_line("unable to create file ({})", cue_sheets.front());
        toc.printCUE(fs, options.image_name, 0);
    }

    if(toc.sessions.size() > 1)
    {
        LOG("multisession: ");
        for(auto const &s : toc.sessions)
            LOG("  session {}: {}-{}", s.session_number, s.tracks.front().indices.empty() ? s.tracks.front().lba_start : s.tracks.front().indices.front(), s.tracks.back().lba_end - 1);
        LOG("");
    }

    for(auto const &c : cue_sheets)
    {
        LOG("CUE [{}]:", c);
        std::filesystem::path cue_path(std::filesystem::path(options.image_path) / c);
        std::fstream ifs(cue_path, std::fstream::in);
        if(!ifs.is_open())
            throw_line("unable to open file ({})", cue_path.filename().string());
        std::string line;
        while(std::getline(ifs, line))
            LOG("{}", line);
        LOG("");
    }

    if(ctx.dump_errors)
    {
        LOG("initial dump media errors: ");
        LOG("  SCSI: {}", ctx.dump_errors->scsi);
        LOG("  C2: {}", ctx.dump_errors->c2);
        LOG("  Q: {}", ctx.dump_errors->q);
    }
}

}
