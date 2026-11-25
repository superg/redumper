module;
#include "throw_line.hh"



export module cd.spiral.dreamcast;

import common;
import drive;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.logger;



namespace gpsxre
{

export int redumper_spiral_dreamcast(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(!is_spiraldrive_firmware(ctx.drive_config))
        throw_line("drive is not running SpiralDrive firmware");

    bool enable = !options.spiraldrive_dreamcast_disable;

    LOG("{} GD-ROM HD session", enable ? "enabling" : "disabling");
    if(auto status = cmd_spiraldrive(*ctx.sptd, nullptr, 0, SpiralDrive_Operation::DREAMCAST, 0, enable); status.status_code)
        throw_line("failed to {} GD-ROM HD session, SCSI ({})", enable ? "enable" : "disable", SPTD::StatusMessage(status));

    return exit_code;
}

}
