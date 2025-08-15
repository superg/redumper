#include <exception>
#include <format>
#ifdef _WIN32
#include <windows.h>
#endif

import options;
import redumper;
import utils.logger;
import version;



using namespace gpsxre;



int main(int argc, char *argv[])
{
    int exit_code = 0;

#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    try
    {
        Options options(argc, const_cast<const char **>(argv));

        if(options.help)
            options.printUsage();
        else if(options.version)
            LOG("{}", redumper_version());
        else if(options.list_recommended_drives)
            redumper_print_drives(false);
        else if(options.list_all_drives)
            redumper_print_drives(true);
        else
        {
            exit_code = redumper(options);
        }
    }
    catch(const std::exception &e)
    {
        LOG("error: {}", e.what());
        exit_code = -1;
    }
    catch(...)
    {
        LOG("error: unhandled exception");
        exit_code = -2;
    }

    return exit_code;
}
