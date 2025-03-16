module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "throw_line.hh"

export module redumper;

import cd.cd;
import cd.dump;
import cd.dump_extra;
import cd.dump_new;
import cd.fix_msf;
import cd.protection;
import cd.scrambler;
import cd.split;
import cd.subcode;
import cd.toc;
import debug;
import drive;
import drive.flash.mt1339;
import dump;
import dvd.dump;
import dvd.key;
import dvd.split;
import hash;
import info;
import options;
import rings;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import skeleton;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.signal;
import utils.strings;
import version;



namespace gpsxre
{

int redumper_cd(Context &ctx, Options &options);

int redumper_dump(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(profile_is_cd(ctx.current_profile))
        ctx.refine = redumper_refine_cd_new(ctx, options, DumpMode::DUMP);
    else
        ctx.refine = redumper_dump_dvd(ctx, options, DumpMode::DUMP);

    return exit_code;
}


int redumper_refine(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(!ctx.refine || *ctx.refine && options.retries)
    {
        if(profile_is_cd(ctx.current_profile))
            redumper_refine_cd_new(ctx, options, DumpMode::REFINE);
        else
            redumper_dump_dvd(ctx, options, DumpMode::REFINE);
    }

    return exit_code;
}


int redumper_verify(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(profile_is_cd(ctx.current_profile))
        LOG("warning: CD verify is unsupported");
    else
        redumper_dump_dvd(ctx, options, DumpMode::VERIFY);

    return exit_code;
}


int redumper_eject(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto status = cmd_start_stop_unit(*ctx.sptd, 1, 0);
    if(status.status_code)
        LOG("warning: failed to eject, SCSI ({})", SPTD::StatusMessage(status));

    return exit_code;
}


int redumper_split(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    if(std::filesystem::exists(image_prefix + ".iso"))
        redumper_split_dvd(ctx, options);
    else
        redumper_split_cd(ctx, options);

    return exit_code;
}


struct Command
{
    using Handler = int (*)(Context &, Options &);

    bool drive_required;
    bool drive_ready;
    bool drive_autoselect;
    bool image_name_required;
    bool image_name_generate;
    Handler handler;
};


const std::map<std::string, Command> COMMAND_HANDLERS{
    // NAME              DRIVE READY AUTO IMAGE GENERATE HANDLER
    { "cd",            { true, true, true, true, true, redumper_cd }               },
    { "rings",         { true, true, true, false, false, redumper_rings }          },
    { "dump",          { true, true, true, true, true, redumper_dump }             },
    { "dump::extra",   { true, true, true, true, false, redumper_dump_extra }      },
    { "refine",        { true, true, true, true, false, redumper_refine }          },
    { "verify",        { true, true, true, true, false, redumper_verify }          },
    { "dvdkey",        { true, true, true, false, false, redumper_dvdkey }         },
    { "eject",         { true, false, false, false, false, redumper_eject }        },
    { "dvdisokey",     { false, false, false, true, false, redumper_dvdisokey }    },
    { "protection",    { false, false, false, true, false, redumper_protection }   },
    { "split",         { false, false, false, true, false, redumper_split }        },
    { "hash",          { false, false, false, true, false, redumper_hash }         },
    { "info",          { false, false, false, true, false, redumper_info }         },
    { "skeleton",      { false, false, false, true, false, redumper_skeleton }     },
    { "flash::mt1339", { true, false, false, false, false, redumper_flash_mt1339 } },
    { "subchannel",    { false, false, false, true, false, redumper_subchannel }   },
    { "debug",         { false, false, false, false, false, redumper_debug }       },
    { "fixmsf",        { false, false, false, true, false, redumper_fix_msf }      },
    { "debug::flip",   { false, false, false, true, false, redumper_flip }         },
};


const std::map<GET_CONFIGURATION_FeatureCode_ProfileList, std::string> PROFILE_STRING = {
    { GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM,         "CD-ROM"       },
    { GET_CONFIGURATION_FeatureCode_ProfileList::CD_R,           "CD-R"         },
    { GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW,          "CD-RW"        },

    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM,        "DVD-ROM"      },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R,          "DVD-R"        },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM,        "DVD-RAM"      },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO,      "DVD-RW RO"    },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW,         "DVD-RW"       },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL,       "DVD-R DL"     },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL_LJR,   "DVD-R DL LJR" },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW,    "DVD+RW"       },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R,     "DVD+R"        },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW_DL, "DVD+RW DL"    },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R_DL,  "DVD+R DL"     },

    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM,         "BD-ROM"       },
    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_R,           "BD-R"         },
    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM,       "BD-R RRM"     },
    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_RW,          "BD-RW"        },

    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_ROM,      "HD DVD-ROM"   },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R,        "HD DVD-R"     },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RAM,      "HD DVD-RAM"   },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW,       "HD DVD-RW"    },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R_DL,     "HD DVD-R DL"  },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW_DL,    "HD DVD-RW DL" }
};


int redumper_execute_command(std::string command, Command::Handler handler, Context &ctx, Options &options, std::chrono::seconds &time_check)
{
    LOG("");
    LOG("*** {}{}", str_uppercase(command), time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));
    LOG("");

    auto time_start = std::chrono::high_resolution_clock::now();
    int exit_code = handler(ctx, options);
    auto time_stop = std::chrono::high_resolution_clock::now();
    time_check = std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start);

    return exit_code;
}


int redumper_cd(Context &ctx, Options &options)
{
    int exit_code = 0;

    std::list<std::string> commands{ "dump", "dump::extra", "protection", "refine", "dvdkey", "split", "hash", "info" };

    if(options.auto_eject)
    {
        if(auto it = std::find(commands.begin(), commands.end(), "split"); it != commands.end())
            commands.insert(it, "eject");
    }

    auto it = options.cd_continue ? std::find(commands.begin(), commands.end(), *options.cd_continue) : commands.begin();
    if(it == commands.end())
        throw_line("cd continue command is unavailable (command: {})", *options.cd_continue);

    std::chrono::seconds time_check = std::chrono::seconds::zero();
    for(; it != commands.end(); ++it)
    {
        auto handler_it = COMMAND_HANDLERS.find(*it);
        if(handler_it == COMMAND_HANDLERS.end())
            throw_line("unknown command (command: {})", *it);

        LOG("");
        LOG("*** {}{}", str_uppercase(*it), time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));
        LOG("");

        auto time_start = std::chrono::high_resolution_clock::now();
        exit_code = handler_it->second.handler(ctx, options);
        auto time_stop = std::chrono::high_resolution_clock::now();
        time_check = std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start);

        if(exit_code)
            break;
    }

    return exit_code;
}


std::string first_ready_drive()
{
    std::string drive;

    for(const auto &d : SPTD::listDrives())
    {
        try
        {
            SPTD sptd(d);

            auto status = cmd_drive_ready(sptd);
            if(!status.status_code)
            {
                drive = d;
                break;
            }
        }
        // drive busy
        catch(const std::exception &)
        {
            ;
        }
    }

    return drive;
}


std::string generate_image_name(std::string drive)
{
    auto pos = drive.find_last_of('/');
    std::string d(drive, pos == std::string::npos ? 0 : pos + 1);
    erase_all_inplace(d, ':');

    return std::format("dump_{}_{}", system_date_time("%y%m%d_%H%M%S"), d);
}


export int redumper(Options &options)
{
    if(options.command.empty())
        options.command = "cd";

    auto it = COMMAND_HANDLERS.find(options.command);
    if(it == COMMAND_HANDLERS.end())
        throw_line("unknown command (command: {})", options.command);

    Context ctx;

    if(it->second.drive_required)
    {
        if(options.drive.empty())
        {
            if(it->second.drive_autoselect)
            {
                options.drive = first_ready_drive();

                if(options.drive.empty())
                    throw_line("no ready drives detected on the system");
            }
            else
                throw_line("drive is not provided");
        }

        ctx.sptd = std::make_shared<SPTD>(options.drive);

        if(it->second.drive_ready)
        {
            auto status = cmd_drive_ready(*ctx.sptd);
            if(status.status_code)
                throw_line("drive not ready, SCSI ({})", SPTD::StatusMessage(status));
        }
    }

    if(it->second.image_name_required && options.image_name.empty())
    {
        if(it->second.image_name_generate)
            options.image_name = generate_image_name(options.drive);
        else
            throw_line("image name is not provided");
    }

    // init log file early not to miss any messages
    if(!options.image_name.empty())
        Logger::get().setFile((std::filesystem::path(options.image_path) / options.image_name).string() + ".log");

    LOG("{}", redumper_version());

    ctx.current_profile = GET_CONFIGURATION_FeatureCode_ProfileList::RESERVED;
    if(it->second.drive_required)
    {
        // query/override drive configuration
        ctx.drive_config = drive_get_config(cmd_drive_query(*ctx.sptd));
        drive_override_config(ctx.drive_config, options.drive_type.get(), options.drive_read_offset.get(), options.drive_c2_shift.get(), options.drive_pregap_start.get(),
            options.drive_read_method.get(), options.drive_sector_order.get());

        if(it->second.drive_ready)
        {
            auto status = cmd_get_configuration_current_profile(*ctx.sptd, ctx.current_profile);
            if(status.status_code)
            {
                // some drives don't have this command implemented, fallback to CD
                LOG("warning: failed to query current profile, SCSI ({})", SPTD::StatusMessage(status));
                ctx.current_profile = GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM;
            }

            if(!profile_is_cd(ctx.current_profile) && !profile_is_dvd(ctx.current_profile) && !profile_is_bluray(ctx.current_profile) && !profile_is_hddvd(ctx.current_profile))
                throw_line("unsupported disc type (current profile: 0x{:02X})", (uint16_t)ctx.current_profile);
        }
    }

    if(!options.arguments.empty())
    {
        LOG("");
        LOG("arguments: {}", options.arguments);
    }

    if(it->second.drive_required)
    {
        LOG("");
        LOG("drive path: {}", options.drive);
        LOG("drive: {}", drive_info_string(ctx.drive_config));
        LOG("drive configuration: {}", drive_config_string(ctx.drive_config));

        if(it->second.drive_ready)
        {
            // set drive speed
            uint16_t speed = 0xFFFF;
            std::string speed_str("<optimal>");
            if(options.speed)
            {
                float speed_modifier = 176.4;
                if(profile_is_dvd(ctx.current_profile))
                    speed_modifier = 1385.0;
                else if(profile_is_bluray(ctx.current_profile))
                    speed_modifier = 4500.0;
                else if(profile_is_hddvd(ctx.current_profile))
                    speed_modifier = 4500.0;

                speed = speed_modifier * *options.speed;
                speed_str = std::format("{} KB", speed);
            }

            auto status = cmd_set_cd_speed(*ctx.sptd, speed);
            if(status.status_code)
                speed_str = std::format("<setting failed, SCSI ({})>", SPTD::StatusMessage(status));

            LOG("drive read speed: {}", speed_str);

            auto layout = sector_order_layout(ctx.drive_config.sector_order);
            if(layout.subcode_offset == CD_RAW_DATA_SIZE)
                LOG("warning: drive doesn't support reading of subchannel data");
            if(layout.c2_offset == CD_RAW_DATA_SIZE)
                LOG("warning: drive doesn't support C2 error pointers");

            LOG("");
            LOG("current profile: {}", enum_to_string(ctx.current_profile, PROFILE_STRING));
        }
    }

    if(!options.image_name.empty())
    {
        LOG("");
        LOG("image path: {}", str_quoted_if_space(options.image_path.empty() ? "." : options.image_path));
        LOG("image name: {}", str_quoted_if_space(options.image_name));
    }

    std::chrono::seconds time_check = std::chrono::seconds::zero();
    int exit_code = redumper_execute_command(options.command, it->second.handler, ctx, options, time_check);
    LOG("");
    LOG("*** END{}", time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));

    return exit_code;
}

}
