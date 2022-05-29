#include <format>
#include "common.hh"
#include "options.hh"



namespace gpsxre
{

Options::Options(int argc, const char *argv[])
    : help(false)
    , verbose(false)
    , overwrite(false)
    , force_split(false)
    , leave_unchanged(false)
    , unsupported(false)
    , retries(0)
    , refine_subchannel(false)
    , force_toc(false)
    , force_qtoc(false)
    , skip_fill(0x55)
    , skip_size(1 << 12)
    , ring_size(1024)
    , iso9660_trim(false)
    , skip_leadin(false)
    , cdi_correct_offset(false)
    , disable_toc_zero(false)
{
    for(int i = 0; i < argc; ++i)
    {
        std::string argument = argv[i];

        bool quoted = false;
        if(argument.find(' ') != std::string::npos)
            quoted = true;

        command += std::format(quoted ? "\"{}\"{}" : "{}{}", argument, i + 1 == argc ? "" : " ");
    }

    std::string *s_value = nullptr;
    int *i_value = nullptr;
    for(int i = 1; i < argc; ++i)
    {
        std::string o(argv[i]);

        // option
        if(o[0] == '-')
        {
            std::string key;
            auto value_pos = o.find("=");
            if(value_pos == std::string::npos)
            {
                key = o;
                o.clear();
            }
            else
            {
                key = std::string(o, 0, value_pos);
                o = std::string(o, value_pos + 1);
            }

            if(s_value == nullptr && i_value == nullptr)
            {
                if(key == "--help" || key == "-h")
                    help = true;
                else if(key == "--verbose")
                    verbose = true;
                else if(key == "--image-path")
                    s_value = &image_path;
                else if(key == "--image-name")
                    s_value = &image_name;
                else if(key == "--overwrite")
                    overwrite = true;
                else if(key == "--force-split")
                    force_split = true;
                else if(key == "--leave-unchanged")
                    leave_unchanged = true;
                else if(key == "--unsupported")
                    unsupported = true;
                else if(key == "--drive")
                    s_value = &drive;
                else if(key == "--speed")
                {
                    speed = std::make_unique<int>();
                    i_value = speed.get();
                }
                else if(key == "--retries")
                    i_value = &retries;
                else if(key == "--refine-subchannel")
                    refine_subchannel = true;
                else if(key == "--stop-lba")
                {
                    stop_lba = std::make_unique<int>();
                    i_value = stop_lba.get();
                }
                else if(key == "--force-toc")
                    force_toc = true;
                else if(key == "--force-qtoc")
                    force_qtoc = true;
                else if(key == "--skip")
                    s_value = &skip;
                else if(key == "--skip-fill")
                    i_value = &skip_fill;
                else if(key == "--skip-size")
                    i_value = &skip_size;
                else if(key == "--ring-size")
                    i_value = &ring_size;
                else if(key == "--iso9660-trim")
                    iso9660_trim = true;
                else if(key == "--skip-leadin")
                    skip_leadin = true;
                else if(key == "--cdi-correct-offset")
                    cdi_correct_offset = true;
                else if(key == "--disable-toc-zero")
                    disable_toc_zero = true;
                // unknown option
                else
                {
                    throw_line(std::format("unknown option ({})", key));
                }
            }
            else
                throw_line(std::format("option value expected ({})", argv[i - 1]));
        }
        
        if(!o.empty())
        {
            if(s_value != nullptr)
            {
                *s_value = o;
                s_value = nullptr;
            }
            else if(i_value != nullptr)
            {
                *i_value = std::stoi(o, nullptr, 0);
                i_value = nullptr;
            }
            else
                positional.emplace_back(o);
        }
    }
}


std::string Options::HelpKeys()
{
    return "--help,-h";
}


std::ostream &Options::PrintUsage(std::ostream &os)
{
    os << "usage: redumper [mode] [options]" << std::endl;
    os << std::endl;

    os << "modes: " << std::endl;
    os << "\tcd\taggregate \"Do It All\" mode {dump => protection => refine => split => info} (default)" << std::endl;
    os << "\tdump\tdumps CD" << std::endl;
    os << "\tprotection\tscans dump for protection" << std::endl;
    os << "\trefine\trefines the dump from a CD by rereading erroneous sectors" << std::endl;
    os << "\tsplit\tperforms track splits and generates a CUE-sheet" << std::endl;
    os << "\tinfo\tredump.org specific text file with dump information" << std::endl;
//    os << "\trings\tscans CD for protection rings, outputs ring ranges for CD dumping" << std::endl;
    os << std::endl;

    os << "options: " << std::endl;
    os << "\t" << HelpKeys() << "\tprint usage" << std::endl;
    os << "\t--verbose\tverbose output" << std::endl;
    os << "\t--image-path=VALUE\tdump files base directory" << std::endl;
    os << "\t--image-name=VALUE\tdump files prefix, autogenerated in dump mode, if not provided" << std::endl;
    os << "\t--overwrite\toverwrites previously generated dump files" << std::endl;
    os << "\t--force-split\t\tforce track split with errors" << std::endl;
    os << "\t--leave-unchanged\t\tdon't replace erroneous sectors with generated ones" << std::endl;
    os << "\t--unsupported\t\tenabled unsupported drives" << std::endl;
    os << "\t--drive=VALUE\t\tdrive to use, first available drive with disc, if not provided" << std::endl;
    os << "\t--speed=VALUE\t\tdrive read speed, optimal drive speed will be used if not provided" << std::endl;
    os << std::format("\t--retries=VALUE\tnumber of sector retries in case of SCSI/C2 error (default: {})", retries) << std::endl;
    os << "\t--refine-subchannel\tIn addition to SCSI/C2, refine subchannel" << std::endl;
    os << "\t--stop-lba=VALUE\tLBA to stop dumping at (everything before the value), useful for discs with fake TOC" << std::endl;
    os << "\t--force-toc\tForce TOC based track split" << std::endl;
    os << "\t--force-qtoc\tForce QTOC based track split" << std::endl;
    os << "\t--skip=VALUE\tLBA ranges of sectors to skip" << std::endl;
    os << std::format("\t--skip-fill=VALUE\tfill byte value for skipped sectors (default: 0x{:02X})", skip_fill) << std::endl;
    os << std::format("\t--skip-size=VALUE\trings mode, number of sectors to skip on SCSI error (default: {})", skip_size) << std::endl;
    os << std::format("\t--ring-size=VALUE\trings mode, maximum ring size to stop subdivision (rings, default: {})", ring_size) << std::endl;
    os << "\t--iso9660-trim\ttrim each ISO9660 data track to PVD volume size, useful for discs with fake TOC" << std::endl;
    os << "\t--skip-leadin\tskip extracting lead-in using PLEXTOR negative range" << std::endl;
    os << "\t--cdi-correct-offset\tcorrect mid-track CDI/VCD offset change" << std::endl;
    os << "\t--disable-toc-zero\tdon't alter first TOC track entry" << std::endl;

    return os;
}

}
