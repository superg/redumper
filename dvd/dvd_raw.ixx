module;
#include <cstdint>
#include <cstring>
#include <vector>
#include "throw_line.hh"

export module dvd.raw;

import common;
import dvd;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;



namespace gpsxre
{


struct MediatekCacheFrame
{
    RecordingFrame recording_frame;
    uint8_t unknown[18];
};


export SPTD::Status read_dvd_raw(Context &ctx, uint8_t *buffer, uint32_t block_size, int32_t address, uint32_t transfer_length, bool force_unit_access)
{
    bool raw_addressing = false;
    if(address < 0)
    {
        raw_addressing = true;
        address += -DVD_LBA_START;
    }

    // TODO: gate behind OmniDrive check
    return cmd_read_omnidrive(*ctx.sptd, buffer, block_size, address, transfer_length, OmniDrive_DiscType::DVD, raw_addressing, force_unit_access, false, OmniDrive_Subchannels::NONE, false);

    // TODO: read raw DVD from cache (cmd_mediatek_read_cache)
}


export SPTD::Status read_bd_raw(Context &ctx, uint8_t *buffer, uint32_t block_size, int32_t address, uint32_t transfer_length, bool force_unit_access)
{
    bool raw_addressing = false;
    if(address < 0)
    {
        raw_addressing = true;
        address += -BD_LBA_START;
    }

    // TODO: gate behind OmniDrive check
    return cmd_read_omnidrive(*ctx.sptd, buffer, block_size, address, transfer_length, OmniDrive_DiscType::BD, raw_addressing, force_unit_access, false, OmniDrive_Subchannels::NONE, false);

    // TODO: read raw BD from cache (cmd_mediatek_read_cache)
}

}
