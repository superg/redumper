#include <filesystem>
#include <format>
#include <stdexcept>
#include <iostream>
#include "common.hh"
#include "logger.hh"
#include "options.hh"
#include "redumper.hh"
#include "signal.hh"



using namespace gpsxre;



int main(int argc, char *argv[])
{
	int exit_code(0);

	Signal::GetInstance();

	try
	{
		Options options(argc, const_cast<const char **>(argv));

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
