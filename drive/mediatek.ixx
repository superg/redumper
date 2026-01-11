module;
#include <cstdint>
#include <map>
#include <span>
#include <vector>
#include "throw_line.hh"

export module drive.mediatek;

import cd.cd;
import cd.subcode;
import drive;
import scsi.cmd;
import scsi.sptd;
import utils.logger;



namespace gpsxre
{

struct MediatekConfig
{
    uint32_t size_mb;
    uint32_t entries_count;
};


// MEDIATEK cache map:
// 0x0000 main
// 0x0930 raw P-W
// 0x0990 Q
// 0x09A0 unknown
// 0x09A4 C2
// 0x0ACA unknown
// 0x0B00 end
constexpr uint32_t MEDIATEK_CACHE_ENTRY_SIZE = 0xB00;

static const std::map<Type, MediatekConfig> MEDIATEK_CACHE_CONFIG = {
    { Type::MTK2,  { 2, 586 }  },
    { Type::MTK2B, { 2, 1000 } },
    { Type::MTK3,  { 3, 1070 } },
    { Type::MTK8A, { 8, 2806 } },
    { Type::MTK8B, { 8, 1079 } },
    { Type::MTK8C, { 8, 1268 } }
};


export MediatekConfig mediatek_get_config(Type type)
{
    MediatekConfig mediatek_config = { 0, 0 };

    auto it = MEDIATEK_CACHE_CONFIG.find(type);
    if(it != MEDIATEK_CACHE_CONFIG.end())
        mediatek_config = it->second;

    return mediatek_config;
}


export bool drive_is_mediatek(const DriveConfig &drive_config)
{
    return MEDIATEK_CACHE_CONFIG.find(drive_config.type) != MEDIATEK_CACHE_CONFIG.end();
}


export SPTD::Status mediatek_cache_read(SPTD &sptd, std::vector<uint8_t> &cache, uint32_t cache_size)
{
    constexpr uint32_t read_size = 1024 * 64; // 64Kb

    cache.resize(cache_size);

    SPTD::Status status = {};
    for(uint32_t offset = 0, n = (uint32_t)cache.size(); offset < n; offset += read_size)
    {
        status = cmd_mediatek_read_cache(sptd, cache.data() + offset, offset, std::min(read_size, n - offset));
        if(status.status_code)
        {
            cache.clear();
            break;
        }
    }

    return status;
}


export std::vector<uint8_t> mediatek_cache_extract(const std::vector<uint8_t> &cache, int32_t lba_start, uint32_t entries_count, Type drive_type)
{
    uint32_t cache_entries_count = mediatek_get_config(drive_type).entries_count;

    int32_t index_start = cache_entries_count;
    std::pair<int32_t, int32_t> index_range = { cache_entries_count, cache_entries_count };
    std::pair<int32_t, int32_t> lba_range;

    // try to find the exact match
    for(uint32_t i = 0; i < cache_entries_count; ++i)
    {
        auto entry = (uint8_t *)&cache[MEDIATEK_CACHE_ENTRY_SIZE * i];
        uint8_t *sub_data = entry + 0x0930;

        ChannelQ Q;
        subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

        if(!Q.isValid())
            continue;

        if(Q.adr != 1 || !Q.mode1.tno)
            continue;

        int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
        if(lba == lba_start)
        {
            index_start = i;
            break;
        }
        else if(lba < lba_start)
        {
            if(index_range.first == cache_entries_count || lba > lba_range.first)
            {
                index_range.first = i;
                lba_range.first = lba;
            }
        }
        else if(lba > lba_start)
        {
            if(index_range.second == cache_entries_count || lba < lba_range.second)
            {
                index_range.second = i;
                lba_range.second = lba;
            }
        }
    }

    // calculate index_start based on valid range boundaries
    if(index_start == cache_entries_count && index_range.first != cache_entries_count && index_range.second != cache_entries_count)
    {
        if(index_range.first > index_range.second)
            index_range.second += cache_entries_count;

        if(lba_range.second - lba_range.first == index_range.second - index_range.first)
            index_start = (index_range.first + lba_start - lba_range.first) % cache_entries_count;
    }

    std::vector<uint8_t> data;

    if(!entries_count || entries_count > cache_entries_count)
        entries_count = cache_entries_count;

    if(index_start != cache_entries_count)
    {
        data.reserve(entries_count * CD_RAW_DATA_SIZE);

        bool last_valid = true;
        for(uint32_t i = 0; i < entries_count; ++i)
        {
            uint32_t index = (index_start + i) % cache_entries_count;
            auto entry = (uint8_t *)&cache[MEDIATEK_CACHE_ENTRY_SIZE * index];
            uint8_t *main_data = entry + 0x0000;
            uint8_t *c2_data = entry + 0x09A4;
            uint8_t *sub_data = entry + 0x0930;

            ChannelQ Q;
            subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

            if(Q.isValid())
            {
                if(Q.adr == 1 && Q.mode1.tno && lba_start + i != BCDMSF_to_LBA(Q.mode1.a_msf))
                    break;

                last_valid = true;
            }
            else
                last_valid = false;

            data.insert(data.end(), main_data, main_data + CD_DATA_SIZE);
            data.insert(data.end(), c2_data, c2_data + CD_C2_SIZE);
            data.insert(data.end(), sub_data, sub_data + CD_SUBCODE_SIZE);
        }

        // pop back last cache entry as it's likely incomplete if Q is invalid
        // confirmed by analyzing cache dump where Q was partially overwritten with newer data
        if(!last_valid)
        {
            constexpr uint32_t trim_size = 1 * CD_RAW_DATA_SIZE;
            uint32_t new_size = data.size() < trim_size ? 0 : data.size() - trim_size;
            data.resize(new_size);
        }
    }

    return data;
}


export void mediatek_cache_print_subq(const std::vector<uint8_t> &cache, Type drive_type)
{
    uint32_t cache_entries_count = mediatek_get_config(drive_type).entries_count;

    for(uint32_t i = 0; i < cache_entries_count; ++i)
    {
        auto entry = (uint8_t *)&cache[MEDIATEK_CACHE_ENTRY_SIZE * i];
        uint8_t *sub_data = entry + 0x0930;

        ChannelQ Q;
        subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

        int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
        LOG("{:4} {:6}: {}", i, lba, Q.Decode());
    }
}


export uint32_t mediatek_find_cache_size(const std::vector<uint8_t> &cache, uint32_t block_size, uint32_t match_percentage)
{
    for(uint32_t i = block_size; i < cache.size() - block_size; i += block_size)
    {
        uint32_t size = std::min(i, (uint32_t)(cache.size() - i));

        std::span<const uint8_t> half1(&cache[0], size);
        std::span<const uint8_t> half2(&cache[i], size);

        uint32_t matches = 0;
        for(uint32_t j = 0; j < size; ++j)
        {
            if(half1[j] == half2[j])
                ++matches;
        }

        if(matches * 100 / size >= match_percentage)
            return i;
    }

    return cache.size();
}

}
