#include <exception>

import logger;
import options;
import redumper;
import version;



using namespace gpsxre;



int main(int argc, char *argv[])
{
	int exit_code(0);

	try
	{
		Options options(argc, const_cast<const char **>(argv));

		if(options.help)
			options.printUsage();
		else if(options.version)
			LOG(redumper_version());
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
