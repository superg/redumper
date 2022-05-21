#include <format>
#include <stdexcept>
#include <iostream>
#include "common.hh"
#include "options.hh"
#include "redumper.hh"



int main(int argc, char *argv[])
{
	int exit_code(0);

	try
	{
		std::cout << std::format("{} (print usage: {})", gpsxre::redumper_version(), gpsxre::Options::HelpKeys()) << std::endl << std::endl;

		gpsxre::Options options(argc, const_cast<const char **>(argv));

		std::cout << std::format("command: {}", options.command) << std::endl << std::endl;

		if(options.help)
			options.PrintUsage(std::cout);
		else
		{
			gpsxre::redumper(options);
		}
	}
	catch(const std::exception &e)
	{
		std::cout << "error: " << e.what() << std::endl;
		exit_code = 1;
	}
	catch(...)
	{
		std::cout << "error: unhandled exception" << std::endl;
		exit_code = 2;
	}

	return exit_code;
}
