#include <filesystem>
#include <format>
#include <stdexcept>
#include <iostream>
#include "common.hh"
#include "logger.hh"
#include "options.hh"
#include "redumper.hh"



using namespace gpsxre;



int main(int argc, char *argv[])
{
	int exit_code(0);

	try
	{
		Options options(argc, const_cast<const char **>(argv));

		if(!options.image_name.empty())
			Logger::Get().Reset((std::filesystem::canonical(options.image_path) / options.image_name).string() + ".log");

		LOG("{} (print usage: {})\n", redumper_version(), Options::HelpKeys());

		LOG("command: {}\n", options.command);

		if(options.help)
			options.PrintUsage();
		else
		{
			redumper(options);
		}
	}
	catch(const std::exception &e)
	{
		LOG("error: {}", e.what());
		exit_code = 1;
	}
	catch(...)
	{
		LOG("error: unhandled exception");
		exit_code = 2;
	}

	return exit_code;
}
