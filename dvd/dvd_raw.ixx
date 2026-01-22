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


export SPTD::Status read_dvd_raw(Context &ctx, uint8_t *buffer, uint32_t block_size, int32_t address, uint32_t transfer_length, bool raw_addressing, bool force_unit_access)
{
    // TODO: gate behind OmniDrive check
    return cmd_read_omnidrive(*ctx.sptd, buffer, block_size, address, transfer_length, OmniDrive_DiscType::DVD, raw_addressing, force_unit_access, false, OmniDrive_Subchannels::NONE, false);

    // TODO: read raw DVD from cache (cmd_mediatek_read_cache)
}

}
