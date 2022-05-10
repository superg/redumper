#pragma once



#include <list>
#include <memory>
#include <ostream>
#include <string>



namespace gpsxre
{

struct Options
{
    std::string command;

    std::list<std::string> positional;
    
    bool help;
    bool verbose;

    std::string image_path;
    std::string image_name;
    bool overwrite;
    bool force_split;
    bool unsupported;

    std::string drive;
    std::unique_ptr<int> speed;
    int retries;
    bool refine_subchannel;
    std::unique_ptr<int> stop_lba;
    bool force_toc;
    bool force_qtoc;
    std::string skip;
    bool zero_error_sectors;
    int skip_size;
    int ring_size;
    bool iso9660_trim;
    bool skip_leadin;
    bool cdi_ready;

    Options(int argc, const char *argv[]);

    static std::string HelpKeys();
    std::ostream &PrintUsage(std::ostream &os);
};

}
