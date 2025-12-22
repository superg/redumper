module;
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <span>
#include <string_view>

export module cd.dreamcast;

import cd.toc;



namespace gpsxre::dreamcast
{

export constexpr int32_t HD_INDEX1_LBA = 45000;
export constexpr std::string_view SYSTEM_AREA_MAGIC = "SEGA SEGAKATANA";

export bool detect(std::span<const uint8_t> system_area)
{
    return std::equal(SYSTEM_AREA_MAGIC.begin(), SYSTEM_AREA_MAGIC.end(), system_area.begin());
}


export void reconstruct_toc(TOC &toc, const TOC &qtoc)
{
    int32_t d = qtoc.sessions.front().tracks.front().indices.front() - toc.sessions.front().tracks.front().indices.front();

    toc = qtoc;

    // first data track is d sectors smaller, shift the following tracks accordingly
    for(auto &s : toc.sessions)
        for(auto &t : s.tracks)
        {
            t.lba_end -= d;
            for(auto &i : t.indices)
                i -= d;
            if(t.track_number == 1)
            {
                continue;
            }

            t.lba_start -= d;
        }

    // last track is data?
    if(toc.sessions.back().tracks.size() > 1)
        toc.sessions.back().tracks[toc.sessions.back().tracks.size() - 2].control |= (uint8_t)ChannelQ::Control::DATA;
}

}
