module;
#include "throw_line.hh"

export module commands;

import cd.dump;
import cd.split;
import debug;
import dump;
import dvd.dump;
import dvd.key;
import info;
import options;
import utils.logger;



namespace gpsxre
{

export bool redumper_dump(Context &ctx, Options &options)
{
	ctx.dump = std::make_unique<Context::Dump>();

	if(ctx.disc_type == DiscType::CD)
		ctx.dump->refine = redumper_dump_cd(ctx, options, false);
	else
		ctx.dump->refine = dump_dvd(ctx, options, DumpMode::DUMP);

	return true;
}


export bool redumper_refine(Context &ctx, Options &options)
{
	bool complete = false;

	if(options.retries && (!ctx.dump || ctx.dump->refine))
	{
		if(ctx.disc_type == DiscType::CD)
			redumper_dump_cd(ctx, options, true);
		else
			dump_dvd(ctx, options, DumpMode::REFINE);

		complete = true;
	}

	return complete;
}


export bool redumper_verify(Context &ctx, Options &options)
{
	bool complete = false;

	if(ctx.disc_type == DiscType::CD)
	{
		LOG("warning: CD verify is unsupported");
	}
	else if(ctx.disc_type == DiscType::DVD)
	{
		dump_dvd(ctx, options, DumpMode::VERIFY);
		complete = true;
	}

	return complete;
}


export bool redumper_dvdkey(Context &ctx, Options &options)
{
	bool complete = false;

	if(ctx.disc_type == DiscType::DVD)
	{
		dvd_key(ctx, options);
		complete = true;
	}

	return complete;
}


export bool redumper_dvdisokey(Context &ctx, Options &options)
{
	dvd_isokey(ctx, options);

	return true;
}


export bool redumper_protection(Context &ctx, Options &options)
{
	redumper_protection_cd(options);

	return true;
}


export bool redumper_split(Context &ctx, Options &options)
{
	redumper_split_cd(options);

	return true;
}


export bool redumper_info(Context &ctx, Options &options)
{
	redumper_info(options);

	return true;
}


export bool redumper_subchannel(Context &ctx, Options &options)
{
	debug_subchannel(options);

	return true;
}


export bool redumper_debug(Context &ctx, Options &options)
{
	debug(ctx, options);

	return true;
}

}
