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
import cd.scrambler;
import cd.split;
import cd.subcode;
import cd.toc;
import commands;
import drive;
import dump;
import dvd.dump;
import dvd.key;
import info;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.signal;
import utils.strings;
import version;



namespace gpsxre
{

const std::set<std::string> CD_BATCH_COMMANDS { "cd", "sacd", "dvd", "bd" };


const std::map<std::string, std::pair<bool, bool (*)(Context &, Options &)>> COMMAND_HANDLERS
{
	//COMMAND         DRIVE    HANDLER
	{ "dump"      , { true ,   redumper_dump       }},
	{ "refine"    , { true ,   redumper_refine     }},
	{ "verify"    , { true ,   redumper_verify     }},
	{ "dvdkey"    , { true ,   redumper_dvdkey     }},
	{ "dvdisokey" , { false,   redumper_dvdisokey  }},
	{ "protection", { false,   redumper_protection }},
	{ "split"     , { false,   redumper_split      }},
	{ "info"      , { false,   redumper_info       }},

	{ "subchannel", { false,   redumper_subchannel }},
	{ "debug"     , { false,   redumper_debug      }}
};


std::string first_ready_drive()
{
	std::string drive;

	auto drives = SPTD::listDrives();
	for(const auto &d : drives)
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


DiscType query_disc_type(std::string drive)
{
	auto disc_type = DiscType::NONE;

	SPTD sptd(drive);

	// test unit ready
	SPTD::Status status = cmd_drive_ready(sptd);
	if(status.status_code)
		throw_line("drive not ready, SCSI ({})", SPTD::StatusMessage(status));

	GET_CONFIGURATION_FeatureCode_ProfileList current_profile = GET_CONFIGURATION_FeatureCode_ProfileList::RESERVED;
	status = cmd_get_configuration_current_profile(sptd, current_profile);
	if(status.status_code)
		throw_line("failed to query disc type, SCSI ({})", SPTD::StatusMessage(status));

	switch(current_profile)
	{
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
	case GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW:
		disc_type = DiscType::DVD;
		break;

	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_R:
	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM:
	case GET_CONFIGURATION_FeatureCode_ProfileList::BD_RE:
		disc_type = DiscType::BLURAY;
		break;

	default:
		throw_line("unsupported disc type (profile: {})", (uint16_t)current_profile);
	}

	return disc_type;
}


std::string generate_image_name(std::string drive)
{
	auto pos = drive.find_last_of('/');
	std::string d(drive, pos == std::string::npos ? 0 : pos + 1);
	erase_all_inplace(d, ':');

	return std::format("dump_{}_{}", system_date_time("%y%m%d_%H%M%S"), d);
}


std::list<std::string> get_cd_batch_commands(DiscType disc_type)
{
	std::list<std::string> commands;

	const std::list<std::string> CD_BATCH{"dump", "protection", "refine", "split", "info"};
	const std::list<std::string> DVD_BATCH{"dump", "refine", "dvdkey", "info"};
	const std::list<std::string> BD_BATCH{"dump", "refine", "info"};

	if(disc_type == DiscType::CD)
		commands = CD_BATCH;
	else if(disc_type == DiscType::DVD)
		commands = DVD_BATCH;
	else if(disc_type == DiscType::BLURAY)
		commands = BD_BATCH;
	else
		throw_line("unsupported disc type");

	return commands;
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

		if(c == "dump")
			generate_name = true;

		if(it->second.first)
			drive_required = true;
	}

	ctx.disc_type = DiscType::NONE;
	if(drive_required)
	{
		// autoselect drive
		if(options.drive.empty())
			options.drive = first_ready_drive();
		if(options.drive.empty())
			throw_line("no ready drives detected on the system");

		ctx.disc_type = query_disc_type(options.drive);
		
		ctx.sptd = std::make_unique<SPTD>(options.drive);

		// set drive speed
		float speed_modifier = 153.6;
		if(ctx.disc_type == DiscType::DVD)
			speed_modifier = 1385.0;
		else if(ctx.disc_type == DiscType::BLURAY)
			speed_modifier = 4500.0;

		uint16_t speed = options.speed ? speed_modifier * *options.speed : 0xFFFF;

		auto status = cmd_set_cd_speed(*ctx.sptd, speed);
		if(status.status_code)
			LOG("drive set speed failed, SCSI ({})", SPTD::StatusMessage(status));

		// query/override drive configuration
		ctx.drive_config = drive_get_config(cmd_drive_query(*ctx.sptd));
		drive_override_config(ctx.drive_config, options.drive_type.get(), options.drive_read_offset.get(),
							  options.drive_c2_shift.get(), options.drive_pregap_start.get(), options.drive_read_method.get(), options.drive_sector_order.get());
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
			auto cd_batch_commands = get_cd_batch_commands(ctx.disc_type);
			options.commands.insert(options.commands.end(), cd_batch_commands.begin(), cd_batch_commands.end());
		}
	}

	// autogenerate image name
	if(generate_name && options.image_name.empty())
		options.image_name = generate_image_name(options.drive);

	// initialize log file early not to miss any messages
	if(!options.image_name.empty())
		Logger::get().setFile((std::filesystem::path(options.image_path) / options.image_name).string() + ".log");

	return ctx;
}


export int redumper(Options &options)
{
	int exit_code = 0;

	Signal::get();

	auto ctx = initialize(options);

	LOG("{}", redumper_version());
	LOG("");
	LOG("command line: {}", options.command_line);

	if(ctx.sptd)
	{
		LOG("");
		LOG("drive path: {}", options.drive);
		LOG("drive: {}", drive_info_string(ctx.drive_config));
		LOG("drive configuration: {}", drive_config_string(ctx.drive_config));
	}

	if(!options.image_name.empty())
	{
		LOG("");
		LOG("image path: {}", options.image_path.empty() ? "." : options.image_path);
		LOG("image name: {}", options.image_name);
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
		bool complete = it->second.second(ctx, options);
		auto time_stop = std::chrono::high_resolution_clock::now();
		time_check = std::chrono::duration_cast<std::chrono::seconds>(time_stop - time_start);
	}
	LOG("");
	LOG("*** END{}", time_check == std::chrono::seconds::zero() ? "" : std::format(" (time check: {}s)", time_check.count()));

	return exit_code;
}

}
