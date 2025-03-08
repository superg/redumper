module;
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

const std::set<std::string> CD_BATCH_COMMANDS{ "cd", "sacd", "dvd", "bd", "new" };


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


void redumper_dump(Context &ctx, Options &options)
{
    if(profile_is_cd(ctx.current_profile))
        ctx.refine = redumper_dump_cd(ctx, options, false);
    else
        ctx.refine = redumper_dump_dvd(ctx, options, DumpMode::DUMP);
}


void redumper_dump_new(Context &ctx, Options &options)
{
    if(profile_is_cd(ctx.current_profile))
        ctx.refine = redumper_refine_cd_new(ctx, options, DumpMode::DUMP);
    // ctx.refine = redumper_dump_cd_new(ctx, options);
    else
        ctx.refine = redumper_dump_dvd(ctx, options, DumpMode::DUMP);
}


void redumper_refine(Context &ctx, Options &options)
{
    if(!ctx.refine || *ctx.refine && options.retries)
    {
        if(profile_is_cd(ctx.current_profile))
            redumper_dump_cd(ctx, options, true);
        else
            redumper_dump_dvd(ctx, options, DumpMode::REFINE);
    }
}


void redumper_refine_new(Context &ctx, Options &options)
{
    if(!ctx.refine || *ctx.refine && options.retries)
    {
        if(profile_is_cd(ctx.current_profile))
            redumper_refine_cd_new(ctx, options, DumpMode::REFINE);
        else
            redumper_dump_dvd(ctx, options, DumpMode::REFINE);
    }
}


void redumper_verify(Context &ctx, Options &options)
{
    if(profile_is_cd(ctx.current_profile))
        LOG("warning: CD verify is unsupported");
    else
        redumper_dump_dvd(ctx, options, DumpMode::VERIFY);
}


void redumper_dvdkey(Context &ctx, Options &options)
{
    if(profile_is_dvd(ctx.current_profile))
        dvd_key(ctx, options);
}


void redumper_eject(Context &ctx, Options &options)
{
    if(ctx.sptd)
    {
        auto status = cmd_start_stop_unit(*ctx.sptd, 1, 0);
        if(status.status_code)
            LOG("warning: failed to eject, SCSI ({})", SPTD::StatusMessage(status));
    }
}


void redumper_split(Context &ctx, Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    if(std::filesystem::exists(image_prefix + ".iso"))
        redumper_split_dvd(ctx, options);
    else
        redumper_split_cd(ctx, options);
}


const std::map<std::string, std::pair<bool, void (*)(Context &, Options &)>> COMMAND_HANDLERS{
    // COMMAND           DRIVE    HANDLER
    { "rings",         { true, redumper_rings }         },
    { "dump",          { true, redumper_dump }          },
    { "dumpnew",       { true, redumper_dump_new }      },
    { "refine",        { true, redumper_refine }        },
    { "refinenew",     { true, redumper_refine_new }    },
    { "verify",        { true, redumper_verify }        },
    { "dvdkey",        { true, redumper_dvdkey }        },
    { "eject",         { true, redumper_eject }         },
    { "dvdisokey",     { false, redumper_dvdisokey }    },
    { "protection",    { false, redumper_protection }   },
    { "split",         { false, redumper_split }        },
    { "hash",          { false, redumper_hash }         },
    { "info",          { false, redumper_info }         },
    { "skeleton",      { false, redumper_skeleton }     },
    { "flash::mt1339", { false, redumper_flash_mt1339 } },

    { "subchannel",    { false, redumper_subchannel }   },
    { "debug",         { false, redumper_debug }        },
    { "fixmsf",        { false, redumper_fix_msf }      },
    { "debug::flip",   { false, redumper_flip }         },
};


std::shared_ptr<SPTD> first_ready_drive(std::string &drive)
{
    std::shared_ptr<SPTD> sptd;

    for(const auto &d : SPTD::listDrives())
    {
        try
        {
            auto s = std::make_shared<SPTD>(d);

            auto status = cmd_drive_ready(*s);
            if(!status.status_code)
            {
                sptd = s;
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

    return sptd;
}


std::string generate_image_name(std::string drive)
{
    auto pos = drive.find_last_of('/');
    std::string d(drive, pos == std::string::npos ? 0 : pos + 1);
    erase_all_inplace(d, ':');

    return std::format("dump_{}_{}", system_date_time("%y%m%d_%H%M%S"), d);
}


std::list<std::string> get_cd_batch_commands(Context &ctx, const std::string &command, bool eject)
{
    // clang-format off
    if(profile_is_cd(ctx.current_profile))
        return command == "new" ? eject ? std::list<std::string>{ "dumpnew", "protection", "refinenew", "eject", "split", "hash", "info" }
                                        : std::list<std::string>{ "dumpnew", "protection", "refinenew", "split", "hash", "info" }
                                : eject ? std::list<std::string>{ "dump", "protection", "refine", "eject", "split", "hash", "info" }
                                        : std::list<std::string>{ "dump", "protection", "refine", "split", "hash", "info" };
    else if(profile_is_dvd(ctx.current_profile))
        return eject ? std::list<std::string>{ "dump", "refine", "dvdkey", "eject", "split", "hash", "info" }
                     : std::list<std::string>{ "dump", "refine", "dvdkey", "split", "hash", "info" };
    else if(profile_is_bluray(ctx.current_profile))
        return eject ? std::list<std::string>{ "dump", "refine", "eject", "split", "hash", "info" }
                     : std::list<std::string>{ "dump", "refine", "split", "hash", "info" };
    else if(profile_is_hddvd(ctx.current_profile))
        return eject ? std::list<std::string>{ "dump", "refine", "eject", "split", "hash", "info" }
                     : std::list<std::string>{ "dump", "refine", "split", "hash", "info" };
    else
        return std::list<std::string>{};
    // clang-format on
}


Context initialize(Options &options)
{
    Context ctx;

    if(options.commands.empty())
        options.commands.push_back("cd");

    // validate commands and determine if drive is required
    bool drive_required = false;
    bool generate_name = false;
    for(auto c : options.commands)
    {
        if(CD_BATCH_COMMANDS.find(c) != CD_BATCH_COMMANDS.end())
            c = "dump";

        auto it = COMMAND_HANDLERS.find(c);
        if(it == COMMAND_HANDLERS.end())
            throw_line("unknown command (command: {})", c);

        if(c == "dump" || c == "dumpnew")
            generate_name = true;

        if(it->second.first)
            drive_required = true;
    }

    if(drive_required)
    {
        // autoselect drive
        if(options.drive.empty())
        {
            ctx.sptd = first_ready_drive(options.drive);

            if(options.drive.empty())
                throw_line("no ready drives detected on the system");
        }
        else
        {
            ctx.sptd = std::make_shared<SPTD>(options.drive);

            // test unit ready
            auto status = cmd_drive_ready(*ctx.sptd);
            if(status.status_code)
                throw_line("drive not ready, SCSI ({})", SPTD::StatusMessage(status));
        }
    }

    // autogenerate image name
    if(generate_name && options.image_name.empty())
        options.image_name = generate_image_name(options.drive);

    // init log file early not to miss any messages
    if(!options.image_name.empty())
        Logger::get().setFile((std::filesystem::path(options.image_path) / options.image_name).string() + ".log");

    ctx.current_profile = GET_CONFIGURATION_FeatureCode_ProfileList::RESERVED;
    if(drive_required)
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

        // query/override drive configuration
        ctx.drive_config = drive_get_config(cmd_drive_query(*ctx.sptd));
        drive_override_config(ctx.drive_config, options.drive_type.get(), options.drive_read_offset.get(), options.drive_c2_shift.get(), options.drive_pregap_start.get(),
            options.drive_read_method.get(), options.drive_sector_order.get());
    }

    // substitute cd batch commands
    std::list<std::string> commands;
    commands.swap(options.commands);
    for(auto c : commands)
    {
        if(CD_BATCH_COMMANDS.find(c) == CD_BATCH_COMMANDS.end())
            options.commands.push_back(c);
        else
        {
            auto cd_batch_commands = get_cd_batch_commands(ctx, c, options.auto_eject);
            options.commands.insert(options.commands.end(), cd_batch_commands.begin(), cd_batch_commands.end());
        }
    }

    return ctx;
}


export int redumper(Options &options)
{
    int exit_code = 0;

    auto ctx = initialize(options);

    LOG("{}", redumper_version());

    if(!options.arguments.empty())
    {
        LOG("");
        LOG("arguments: {}", options.arguments);
    }

    if(ctx.sptd)
    {
        LOG("");
        LOG("drive path: {}", options.drive);
        LOG("drive: {}", drive_info_string(ctx.drive_config));
        LOG("drive configuration: {}", drive_config_string(ctx.drive_config));

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

    if(!options.image_name.empty())
    {
        LOG("");
        LOG("image path: {}", str_quoted_if_space(options.image_path.empty() ? "." : options.image_path));
        LOG("image name: {}", str_quoted_if_space(options.image_name));
    }

    std::chrono::seconds time_check = std::chrono::seconds::zero();
    for(auto const &c : options.commands)
    {
        auto it = COMMAND_HANDLERS.find(c);
        if(it == COMMAND_HANDLERS.end())
            throw_line("unknown command (command: {})", c);

        LOG("");
        LOG("*** {}{}", str_uppercase(c), time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));
        LOG("");

        auto time_start = std::chrono::high_resolution_clock::now();
        it->second.second(ctx, options);
        auto time_stop = std::chrono::high_resolution_clock::now();
        time_check = std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start);
    }
    LOG("");
    LOG("*** END{}", time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));

    return exit_code;
}

}
