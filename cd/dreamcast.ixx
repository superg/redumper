module;
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include "throw_line.hh"

export module cd.dreamcast;

import cd.cd;
import cd.cdrom;
import cd.subcode;
import cd.toc;
import utils.strings;



namespace gpsxre::dreamcast
{

export constexpr std::string_view SYSTEM_AREA_MAGIC = "SEGA SEGAKATANA";
export constexpr int32_t IP_BIN_LBA = 45000;
constexpr std::string_view TOC1_MAGIC = "TOC1";
constexpr uint8_t CONTROL_ADR_TERMINATOR = 0xFF;


struct TOC1
{
    char magic[4];
    struct AddressEntry
    {
        uint32_t lba         :24;
        uint32_t control_adr :8;
    };
    AddressEntry entries[97];
    struct TrackEntry
    {
        uint8_t reserved[2];
        uint8_t track_number;
        uint8_t control_adr;
    };
    TrackEntry first_track;
    TrackEntry last_track;
    AddressEntry leadout;

    uint8_t reserved[108];
};


struct IP_BIN
{
    uint8_t header[256];
    TOC1 toc;
    uint8_t license_code[1280];
};


export bool detect(std::span<const uint8_t> system_area)
{
    return std::equal(SYSTEM_AREA_MAGIC.begin(), SYSTEM_AREA_MAGIC.end(), system_area.begin());
}


void session_add_track(TOC::Session &session, uint32_t track_number, int32_t lba, uint8_t control_adr)
{
    TOC::Session::Track t;

    t.track_number = track_number;

    // each index1 entry is shifted right by the pre-gap size
    lba -= CD_PREGAP_SIZE;

    t.indices.push_back(lba);
    t.control = control_adr >> 4;
    t.lba_start = lba;
    t.lba_end = lba;
    t.data_mode = 0;
    t.cdi = false;

    session.tracks.push_back(t);
}


export void update_toc(TOC &toc, std::span<const uint8_t> sector)
{
    auto &toc1 = ((IP_BIN const &)sector_user_data((Sector &)sector[0])[0]).toc;

    TOC::Session session = { .session_number = 2 };

    if(to_string_view(toc1.magic) != TOC1_MAGIC)
        throw_line("dreamcast: unexpected TOC1 magic");

    for(uint32_t i = 0; i < std::size(toc1.entries); ++i)
    {
        auto &e = toc1.entries[i];

        if(e.control_adr == CONTROL_ADR_TERMINATOR)
            break;

        session_add_track(session, toc1.first_track.track_number + i, e.lba, e.control_adr);
    }

    // leadout track
    session_add_track(session, bcd_decode(CD_LEADOUT_TRACK_NUMBER), toc1.leadout.lba, toc1.leadout.control_adr);

    toc.sessions.push_back(session);
}

}
