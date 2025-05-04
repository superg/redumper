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
import cd.fix_msf;
import cd.protection;
import cd.scrambler;
import cd.split;
import cd.subcode;
import cd.toc;
import common;
import debug;
import drive;
import drive.flash.mt1339;
import drive.test;
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

int redumper_dump(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type == DiscType::CD)
        ctx.refine = redumper_dump_cd(ctx, options, DumpMode::DUMP);
    else
        ctx.refine = redumper_dump_dvd(ctx, options, DumpMode::DUMP);

    return exit_code;
}


int redumper_refine(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(!ctx.refine || *ctx.refine && options.retries)
    {
        if(ctx.disc_type == DiscType::CD)
            redumper_dump_cd(ctx, options, DumpMode::REFINE);
        else
            redumper_dump_dvd(ctx, options, DumpMode::REFINE);
    }

    return exit_code;
}


int redumper_verify(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type == DiscType::CD)
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


const std::map<std::string, Command> COMMANDS{
    // NAME              DRIVE READY AUTO IMAGE GENERATE HANDLER
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
    { "drive::test",   { true, true, true, false, false, redumper_drive_test }     },
};


const std::map<DiscType, std::string> DISC_TYPE_STRING = {
    { DiscType::CD,       "CD"       },
    { DiscType::DVD,      "DVD"      },
    { DiscType::BLURAY,   "BLURAY"   },
    { DiscType::BLURAY_R, "BLURAY-R" },
    { DiscType::HDDVD,    "HD-DVD"   }
};


const std::map<GET_CONFIGURATION_FeatureCode_ProfileList, std::string> PROFILE_STRING = {
    { GET_CONFIGURATION_FeatureCode_ProfileList::RESERVED,           "reserved"           },
    { GET_CONFIGURATION_FeatureCode_ProfileList::NON_REMOVABLE_DISK, "non removable disk" },
    { GET_CONFIGURATION_FeatureCode_ProfileList::REMOVABLE_DISK,     "removable disk"     },
    { GET_CONFIGURATION_FeatureCode_ProfileList::MO_ERASABLE,        "MO erasable"        },
    { GET_CONFIGURATION_FeatureCode_ProfileList::MO_WRITE_ONCE,      "MO write once"      },
    { GET_CONFIGURATION_FeatureCode_ProfileList::AS_MO,              "AS MO"              },

    { GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM,             "CD-ROM"             },
    { GET_CONFIGURATION_FeatureCode_ProfileList::CD_R,               "CD-R"               },
    { GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW,              "CD-RW"              },

    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM,            "DVD-ROM"            },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R,              "DVD-R"              },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM,            "DVD-RAM"            },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO,          "DVD-RW RO"          },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW,             "DVD-RW"             },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL,           "DVD-R DL"           },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL_LJR,       "DVD-R DL LJR"       },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW,        "DVD+RW"             },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R,         "DVD+R"              },

    { GET_CONFIGURATION_FeatureCode_ProfileList::DDCD_ROM,           "DDCD-ROM"           },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DDCD_R,             "DDCD-R"             },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DDCD_RW,            "DDCD-RW"            },

    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW_DL,     "DVD+RW DL"          },
    { GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R_DL,      "DVD+R DL"           },

    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM,             "BD-ROM"             },
    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_R,               "BD-R"               },
    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM,           "BD-R RRM"           },
    { GET_CONFIGURATION_FeatureCode_ProfileList::BD_RW,              "BD-RW"              },

    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_ROM,          "HD DVD-ROM"         },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R,            "HD DVD-R"           },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RAM,          "HD DVD-RAM"         },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW,           "HD DVD-RW"          },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R_DL,         "HD DVD-R DL"        },
    { GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW_DL,        "HD DVD-RW DL"       },

    { GET_CONFIGURATION_FeatureCode_ProfileList::NON_STANDARD,       "NON STANDARD"       }
};


DiscType profile_to_disc_type(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    DiscType disc_type;

    switch(profile)
    {
    default:
    case GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM:
    case GET_CONFIGURATION_FeatureCode_ProfileList::CD_R:
    case GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW:
        disc_type = DiscType::CD;
        break;

    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL_LJR:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW_DL:
    case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R_DL:
        disc_type = DiscType::DVD;
        break;

    case GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM:
        disc_type = DiscType::BLURAY;
        break;

    case GET_CONFIGURATION_FeatureCode_ProfileList::BD_R:
    case GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM:
    case GET_CONFIGURATION_FeatureCode_ProfileList::BD_RW:
        disc_type = DiscType::BLURAY_R;
        break;

    case GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_ROM:
    case GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R:
    case GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RAM:
    case GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW:
    case GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R_DL:
    case GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW_DL:
        disc_type = DiscType::HDDVD;
        break;
    }

    return disc_type;
}


std::list<std::pair<std::string, Command>> redumper_cd_get_commands(Options &options)
{
    std::list<std::pair<std::string, Command>> commands;

    std::list<std::string> cd_commands{ "dump", "dump::extra", "protection", "refine", "dvdkey", "split", "hash", "info" };
    if(options.auto_eject)
        if(auto it = std::find(cd_commands.begin(), cd_commands.end(), "split"); it != cd_commands.end())
            cd_commands.insert(it, "eject");
    if(options.skeleton)
        cd_commands.push_back("skeleton");

    auto it = options.cd_continue ? std::find(cd_commands.begin(), cd_commands.end(), *options.cd_continue) : cd_commands.begin();
    if(it == cd_commands.end())
        throw_line("cd continue command is unavailable (command: {})", *options.cd_continue);

    for(; it != cd_commands.end(); ++it)
    {
        auto handler_it = COMMANDS.find(*it);
        if(handler_it == COMMANDS.end())
            throw_line("unknown command (command: {})", *it);

        commands.emplace_back(handler_it->first, handler_it->second);
    }

    return commands;
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
    std::list<std::pair<std::string, Command>> commands;
    Command aggregate = {};

    if(options.command.empty() || options.command == "disc")
    {
        commands = redumper_cd_get_commands(options);

        for(auto const &c : commands)
        {
            aggregate.drive_required = aggregate.drive_required || c.second.drive_required;
            aggregate.drive_ready = aggregate.drive_ready || c.second.drive_ready;
            aggregate.drive_autoselect = aggregate.drive_autoselect || c.second.drive_autoselect;
            aggregate.image_name_required = aggregate.image_name_required || c.second.image_name_required;
            aggregate.image_name_generate = aggregate.image_name_generate || c.second.image_name_generate;
        }
    }
    else
    {
        auto it = COMMANDS.find(options.command);
        if(it == COMMANDS.end())
            throw_line("unknown command (command: {})", options.command);

        commands.emplace_back(it->first, it->second);
        aggregate = it->second;
    }

    Context ctx;

    if(aggregate.drive_required)
    {
        if(options.drive.empty())
        {
            if(aggregate.drive_autoselect)
            {
                options.drive = first_ready_drive();

                if(options.drive.empty())
                    throw_line("no ready drives detected on the system");
            }
            else
                throw_line("drive is not provided");
        }

        ctx.sptd = std::make_shared<SPTD>(options.drive);

        if(aggregate.drive_ready)
        {
            auto status = cmd_drive_ready(*ctx.sptd);
            if(status.status_code)
                throw_line("drive not ready, SCSI ({})", SPTD::StatusMessage(status));
        }
    }

    if(aggregate.image_name_required && options.image_name.empty())
    {
        if(aggregate.image_name_generate)
            options.image_name = generate_image_name(options.drive);
        else
            throw_line("image name is not provided");
    }

    // init log file early not to miss any messages
    if(!options.image_name.empty())
        Logger::get().setFile((std::filesystem::path(options.image_path) / options.image_name).string() + ".log");

    LOG("{}", redumper_version());

    if(aggregate.drive_required)
    {
        // query/override drive configuration
        ctx.drive_config = drive_get_config(cmd_drive_query(*ctx.sptd));
        drive_override_config(ctx.drive_config, options.drive_type.get(), options.drive_read_offset.get(), options.drive_c2_shift.get(), options.drive_pregap_start.get(),
            options.drive_read_method.get(), options.drive_sector_order.get());
    }

    if(!options.arguments.empty())
    {
        LOG("");
        LOG("arguments: {}", options.arguments);
    }

    ctx.disc_type = DiscType::CD;
    if(aggregate.drive_required)
    {
        LOG("");
        LOG("drive path: {}", options.drive);
        LOG("drive: {}", drive_info_string(ctx.drive_config));
        LOG("drive configuration: {}", drive_config_string(ctx.drive_config));

        if(aggregate.drive_ready)
        {
            GET_CONFIGURATION_FeatureCode_ProfileList current_profile = GET_CONFIGURATION_FeatureCode_ProfileList::RESERVED;
            auto status = cmd_get_configuration_current_profile(*ctx.sptd, current_profile);
            if(status.status_code)
            {
                // some drives don't have this command implemented, fallback to CD
                LOG("warning: failed to query current profile, SCSI ({})", SPTD::StatusMessage(status));
            }

            ctx.disc_type = options.disc_type ? string_to_enum(*options.disc_type, DISC_TYPE_STRING) : profile_to_disc_type(current_profile);

            // set drive speed
            uint16_t speed = 0xFFFF;
            std::string speed_str("<optimal>");
            if(options.speed)
            {
                float speed_modifier = 176.4;
                if(ctx.disc_type == DiscType::DVD)
                    speed_modifier = 1385.0;
                else if(ctx.disc_type == DiscType::BLURAY || ctx.disc_type == DiscType::BLURAY_R)
                    speed_modifier = 4500.0;
                else if(ctx.disc_type == DiscType::HDDVD)
                    speed_modifier = 4500.0;

                speed = speed_modifier * *options.speed;
                speed_str = std::format("{} KB", speed);
            }

            status = cmd_set_cd_speed(*ctx.sptd, speed);
            if(status.status_code)
                speed_str = std::format("<setting failed, SCSI ({})>", SPTD::StatusMessage(status));

            LOG("drive read speed: {}", speed_str);

            auto layout = sector_order_layout(ctx.drive_config.sector_order);
            if(layout.subcode_offset == CD_RAW_DATA_SIZE)
                LOG("warning: drive doesn't support reading of subchannel data");
            if(layout.c2_offset == CD_RAW_DATA_SIZE)
                LOG("warning: drive doesn't support C2 error pointers");

            LOG("");
            LOG("current profile: {}", enum_to_string(current_profile, PROFILE_STRING));
            LOG("disc type: {}", enum_to_string(ctx.disc_type, DISC_TYPE_STRING));
        }
    }

    if(!options.image_name.empty())
    {
        LOG("");
        LOG("image path: {}", str_quoted_if_space(options.image_path.empty() ? "." : options.image_path));
        LOG("image name: {}", str_quoted_if_space(options.image_name));
    }

    int exit_code = 0;

    std::chrono::seconds time_check = std::chrono::seconds::zero();
    for(auto const &c : commands)
    {
        LOG("");
        LOG("*** {}{}", str_uppercase(c.first), time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));
        LOG("");

        auto time_start = std::chrono::high_resolution_clock::now();
        exit_code = c.second.handler(ctx, options);
        auto time_stop = std::chrono::high_resolution_clock::now();
        time_check = std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start);

        if(exit_code)
            break;
    }
    LOG("");
    LOG("*** END{}", time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));

    return exit_code;
}

}
