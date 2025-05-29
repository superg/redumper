module;
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "throw_line.hh"

export module debug;

import cd.cd;
import cd.common;
import cd.subcode;
import cd.toc;
import common;
import drive;
import drive.mediatek;
import dvd.dump;
import options;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;



namespace gpsxre
{

export int redumper_subchannel(Context &ctx, Options &options)
{
    int exit_code = 0;

    std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path sub_path(image_prefix + ".subcode");

    uint32_t sectors_count = check_file(sub_path, CD_SUBCODE_SIZE);
    std::fstream sub_fs(sub_path, std::fstream::in | std::fstream::binary);
    if(!sub_fs.is_open())
        throw_line("unable to open file ({})", sub_path.filename().string());

    ChannelQ q_empty;
    memset(&q_empty, 0, sizeof(q_empty));

    bool empty = false;
    std::vector<uint8_t> sub_buffer(CD_SUBCODE_SIZE);
    for(uint32_t lba_index = 0; lba_index < sectors_count; ++lba_index)
    {
        read_entry(sub_fs, sub_buffer.data(), CD_SUBCODE_SIZE, lba_index, 1, 0, 0);

        ChannelQ Q;
        subcode_extract_channel((uint8_t *)&Q, sub_buffer.data(), Subchannel::Q);

        uint8_t P[12];
        subcode_extract_channel(P, sub_buffer.data(), Subchannel::P);
        uint32_t p_bits = 0;
        for(uint32_t i = 0; i < sizeof(P); ++i)
            p_bits += std::popcount(P[i]);

        // Q is available
        if(memcmp(&Q, &q_empty, sizeof(q_empty)))
        {
            int32_t lbaq = BCDMSF_to_LBA(Q.mode1.a_msf);

            LOG("[LBA: {:6}, LBAQ: {:6}] {} P: {}/96", LBA_START + (int32_t)lba_index, lbaq, Q.Decode(), p_bits);
            empty = false;
        }
        else if(!empty)
        {
            LOG("...");
            empty = true;
        }
    }

    return exit_code;
}


#pragma pack(push, 1)
struct SBIEntry
{
    MSF msf;
    uint8_t one;

    struct Q
    {
        uint8_t adr     :4;
        uint8_t control :4;
        uint8_t tno;
        uint8_t point_index;
        MSF msf;
        uint8_t zero;
        MSF a_msf;
    } q;
};
#pragma pack(pop)


export int redumper_debug(Context &ctx, Options &options)
{
    int exit_code = 0;

    std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path cache_path(image_prefix + ".asus");
    std::filesystem::path toc_path(image_prefix + ".toc");
    std::filesystem::path fulltoc_path(image_prefix + ".fulltoc");
    std::filesystem::path cdtext_path(image_prefix + ".cdtext");
    std::filesystem::path cue_path(image_prefix + ".cue");
    std::filesystem::path physical_path(image_prefix + ".physical");
    std::filesystem::path sub_path(image_prefix + ".subcode");

    if(0)
    {
        std::vector<uint8_t> fulltoc_buffer = read_vector(fulltoc_path);

        TOC toc_full(fulltoc_buffer, true);

        print_toc(toc_full);
        LOG("");
    }

    // CDB decode
    if(0)
    {
        // uint8_t cdb_raw[] = { 0xbe, 0x04, 0x00, 0x00, 0x0b, 0xd0, 0x00, 0x00, 0x18, 0x14, 0x00, 0x00 };
        uint8_t cdb_raw[] = { 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf8, 0x04, 0x00 };
        auto cdb = (CDB12_ReadCD *)cdb_raw;

        LOG("");
    }

    // TOC bad split
    if(0)
    {
        std::vector<uint8_t> toc_buffer = read_vector(toc_path);
        TOC toc(toc_buffer, false);

        uint32_t subcode_sectors_count = check_file(sub_path, CD_SUBCODE_SIZE);

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

        toc.updateQ(subq.data(), subp.data(), subcode_sectors_count, LBA_START, options.legacy_subs);
        toc.printCUE(std::cout, options.image_name, 0);
        LOG("");
    }

    // BluRay structure print
    if(0)
    {
        std::vector<uint8_t> structure = read_vector(physical_path);

        LOG("disc structure:");
        // print_di_units_structure(&structure[sizeof(CMD_ParameterListHeader)], false);

        LOG("");
    }

    // DVD sectors count
    if(0)
    {
        /*
                READ_DVD_STRUCTURE_LayerDescriptor layer_descriptor;
                layer_descriptor.data_start_sector = endian_swap<uint32_t>(0x30000);
                layer_descriptor.data_end_sector = endian_swap<uint32_t>(0xfff648e8);
                layer_descriptor.layer0_end_sector = endian_swap<uint32_t>(1569279);
                layer_descriptor.track_path = 1;

                int32_t test = 0xfff648e8;

                int32_t test2 = sign_extend<24>(endian_swap(layer_descriptor.data_end_sector));

                uint32_t sectors_count = get_layer_length(layer_descriptor);
                LOG("DVD sectors count: {}", sectors_count);

                LOG("");
        */
    }

    // popcnt test
    if(0)
    {
        /*
                for(uint32_t i = 0; i < 0xffffffff; ++i)
                {
                    uint32_t test = __popcnt(i);
                    uint32_t test2 = bits_count(i);

                    if(test != test2)
                        LOG("{} <=> {}", test, test2);
                }
        */
    }

    // CD-TEXT debug
    if(0)
    {
        std::vector<uint8_t> toc_buffer = read_vector(toc_path);
        TOC toc(toc_buffer, false);

        std::vector<uint8_t> cdtext_buffer = read_vector(cdtext_path);
        toc.updateCDTEXT(cdtext_buffer);

        std::fstream fs(cue_path, std::fstream::out);
        if(!fs.is_open())
            throw_line("unable to create file ({})", cue_path.string());
        toc.printCUE(fs, options.image_name, 0);

        LOG("");
    }

    // LG/ASUS cache read
    if(0)
    {
        SPTD sptd(options.drive);
        std::vector<uint8_t> cache;
        SPTD::Status status = asus_cache_read(sptd, cache, 1024 * 1024 * asus_get_config(Type::LG_ASU3).size_mb);

        LOG("");
    }

    // LG/ASUS cache dump extract
    if(0)
    {
        std::vector<uint8_t> cache = read_vector(cache_path);

        auto drive_type = Type::LG_ASU8C;
        asus_cache_print_subq(cache, drive_type);

        // auto asd = asus_cache_unroll(cache);
        // auto asd = asus_cache_extract(cache, 128224, 0);
        auto asus_leadout_buffer = asus_cache_extract(cache, 292353, 100, drive_type);
        uint32_t entries_count = (uint32_t)asus_leadout_buffer.size() / CD_RAW_DATA_SIZE;

        LOG("entries count: {}", entries_count);

        std::ofstream ofs_data(image_prefix + ".asus.data", std::ofstream::binary);
        std::ofstream ofs_c2(image_prefix + ".asus.c2", std::ofstream::binary);
        std::ofstream ofs_sub(image_prefix + ".asus.sub", std::ofstream::binary);
        for(uint32_t i = 0; i < entries_count; ++i)
        {
            uint8_t *entry = &asus_leadout_buffer[CD_RAW_DATA_SIZE * i];

            ofs_data.write((char *)entry, CD_DATA_SIZE);
            ofs_c2.write((char *)entry + CD_DATA_SIZE, CD_C2_SIZE);
            ofs_sub.write((char *)entry + CD_DATA_SIZE + CD_C2_SIZE, CD_SUBCODE_SIZE);
        }

        LOG("");
    }

    // convert old state file to new state file
    if(0)
    {
        std::fstream fs_state(state_path, std::fstream::out | std::fstream::in | std::fstream::binary);
        uint64_t states_count = std::filesystem::file_size(state_path) / sizeof(State);
        std::vector<State> states((std::vector<State>::size_type)states_count);
        fs_state.read((char *)states.data(), states.size() * sizeof(State));
        for(auto &s : states)
        {
            uint8_t value = (uint8_t)s;
            if(value == 0)
                s = (State)4;
            else if(value == 1)
                s = (State)3;
            else if(value == 3)
                s = (State)1;
            else if(value == 4)
                s = (State)0;
        }

        fs_state.seekp(0);
        fs_state.write((char *)states.data(), states.size() * sizeof(State));

        LOG("");
    }

    // SBI
    if(0)
    {
        std::vector<std::pair<std::vector<std::string>, std::set<int32_t>>> dictionary;

        for(auto &entry : std::filesystem::directory_iterator(options.image_path))
        {
            LOG("{}", entry.path().string());

            std::ifstream ifs(entry.path(), std::ifstream::binary);
            if(ifs.is_open())
            {
                char magic[4];

                uint32_t entries_count = (std::filesystem::file_size(entry.path()) - sizeof(magic)) / sizeof(SBIEntry);
                std::vector<SBIEntry> entries(entries_count);

                ifs.read(magic, sizeof(magic));
                for(auto &e : entries)
                    ifs.read((char *)&e, sizeof(e));

                std::set<int32_t> values;
                for(auto const &e : entries)
                    values.insert(BCDMSF_to_LBA(e.msf));

                bool add = true;
                for(auto &d : dictionary)
                    if(d.second == values)
                    {
                        d.first.push_back(entry.path().filename().string());
                        add = false;
                        break;
                    }
                if(add)
                {
                    std::vector<std::string> names;
                    names.push_back(entry.path().filename().string());
                    dictionary.emplace_back(names, values);
                }

                LOG("");
            }
        }

        LOG("");
    }

    LOG("");

    return exit_code;
}

export int redumper_flip(Context &ctx, Options &options)
{
    int exit_code = 0;

    std::string image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path state_path(image_prefix + ".state");
    std::filesystem::path scram_path(image_prefix + ".scram");
    std::filesystem::path flip_path(image_prefix + ".flip");

    if(std::filesystem::exists(flip_path) && !options.overwrite)
        throw_line("file already exists ({})", flip_path.filename().string());

    std::fstream scram_fs(scram_path, std::fstream::in | std::fstream::binary);
    if(!scram_fs.is_open())
        throw_line("unable to open file ({})", scram_path.filename().string());

    std::fstream state_fs(state_path, std::fstream::in | std::fstream::binary);
    if(!state_fs.is_open())
        throw_line("unable to open file ({})", state_path.filename().string());

    std::fstream flip_fs(flip_path, std::fstream::out | std::fstream::binary);
    if(!flip_fs.is_open())
        throw_line("unable to open file ({})", flip_path.filename().string());

    uint32_t samples_count = std::filesystem::file_size(state_path);
    for(uint32_t i = 0; i < samples_count; ++i)
    {
        State state;
        state_fs.read((char *)&state, sizeof(state));
        if(state_fs.fail())
            throw_line("state read failed");

        uint32_t sample;
        scram_fs.read((char *)&sample, sizeof(sample));
        if(scram_fs.fail())
            throw_line("sample read failed");

        if(state != State::SUCCESS)
            sample = ~sample;

        flip_fs.write((char *)&sample, sizeof(sample));
        if(flip_fs.fail())
            throw_line("flip write failed");
    }

    return exit_code;
}

}
