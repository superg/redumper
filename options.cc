#include <fmt/format.h>
#include "common.hh"
#include "logger.hh"
#include "options.hh"



namespace gpsxre
{

Options::Options(int argc, const char *argv[])
    : help(false)
    , verbose(false)
    , overwrite(false)
    , force_split(false)
    , leave_unchanged(false)
    , retries(0)
    , refine_subchannel(false)
    , force_qtoc(false)
    , skip_fill(0x55)
    , skip_size(1 << 12)
    , ring_size(1024)
    , iso9660_trim(false)
    , plextor_skip_leadin(false)
    , asus_skip_leadout(false)
    , correct_offset_shift(false)
    , cdi_ready_normalize(false)
    , perfect_audio_offset(false)
    , audio_silence_threshold(48)
{
    for(int i = 0; i < argc; ++i)
    {
        std::string argument = argv[i];

        bool quoted = false;
        if(argument.find(' ') != std::string::npos)
            quoted = true;

        command += fmt::vformat("{}{}{}{}", fmt::make_format_args(quoted ? "\"" : "", argument, quoted ? "\"" : "", i + 1 == argc ? "" : " "));
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
                else if(key == "--drive")
                    s_value = &drive;
                else if(key == "--drive-type")
                {
                    drive_type = std::make_unique<std::string>();
                    s_value = drive_type.get();
                }
                else if(key == "--drive-read-offset")
                {
                    drive_read_offset = std::make_unique<int>();
                    i_value = drive_read_offset.get();
                }
                else if(key == "--drive-c2-shift")
                {
                    drive_c2_shift = std::make_unique<int>();
                    i_value = drive_c2_shift.get();
                }
                else if(key == "--drive-pregap-start")
                {
                    drive_pregap_start = std::make_unique<int>();
                    i_value = drive_pregap_start.get();
                }
                else if(key == "--drive-read-method")
                {
                    drive_read_method = std::make_unique<std::string>();
                    s_value = drive_read_method.get();
                }
                else if(key == "--drive-sector-order")
                {
                    drive_sector_order = std::make_unique<std::string>();
                    s_value = drive_sector_order.get();
                }
                else if(key == "--speed")
                {
                    speed = std::make_unique<int>();
                    i_value = speed.get();
                }
                else if(key == "--retries")
                    i_value = &retries;
                else if(key == "--refine-subchannel")
                    refine_subchannel = true;
                else if(key == "--lba-start")
                {
                    lba_start = std::make_unique<int>();
                    i_value = lba_start.get();
                }
                else if(key == "--lba-end")
                {
                    lba_end = std::make_unique<int>();
                    i_value = lba_end.get();
                }
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
                else if(key == "--plextor-skip-leadin")
                    plextor_skip_leadin = true;
                else if(key == "--asus-skip-leadout")
                    asus_skip_leadout = true;
                else if(key == "--correct-offset-shift")
                    correct_offset_shift = true;
                else if(key == "--cdi-ready-normalize")
                    cdi_ready_normalize = true;
                else if(key == "--force-offset")
                {
                    force_offset = std::make_unique<int>();
                    i_value = force_offset.get();
                }
                else if(key == "--perfect-audio-offset")
                    perfect_audio_offset = true;
                else if(key == "--audio-silence-threshold")
                    i_value = &audio_silence_threshold;
                // unknown option
                else
                {
                    throw_line(fmt::format("unknown option ({})", key));
                }
            }
            else
                throw_line(fmt::format("option value expected ({})", argv[i - 1]));
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


void Options::PrintUsage()
{
    LOG("usage: redumper [mode] [options]");
    LOG("");

    LOG("modes: ");
    LOG("\tcd\taggregate \"Do It All\" mode {{dump => protection => refine => split => info}} (default)");
    LOG("\tdump\tdumps CD");
    LOG("\tprotection\tscans dump for protection");
    LOG("\trefine\trefines the dump from a CD by rereading erroneous sectors");
    LOG("\tsplit\tperforms track splits and generates a CUE-sheet");
    LOG("\tinfo\tredump.org specific text file with dump information");
//    LOG("\trings\tscans CD for protection rings, outputs ring ranges for CD dumping");
    LOG("");

    LOG("options: ");
    LOG("\t{}\tprint usage", HelpKeys());
    LOG("\t--verbose\tverbose output");
    LOG("\t--image-path=VALUE\tdump files base directory");
    LOG("\t--image-name=VALUE\tdump files prefix, autogenerated in dump mode, if not provided");
    LOG("\t--overwrite\toverwrites previously generated dump files");
    LOG("\t--force-split\t\tforce track split with errors");
    LOG("\t--leave-unchanged\t\tdon't replace erroneous sectors with generated ones");
    LOG("\t--drive=VALUE\t\tdrive to use, first available drive with disc, if not provided");
    LOG("\t--drive-type=VALUE\t\toverride drive type, possible values: GENERIC, PLEXTOR, LG_ASUS");
    LOG("\t--drive-read-offset=VALUE\t\toverride drive read offset");
    LOG("\t--drive-c2-shift=VALUE\t\toverride drive C2 shift");
    LOG("\t--drive-pregap-start=VALUE\t\toverride drive pre-gap start LBA");
    LOG("\t--drive-read-method=VALUE\t\toverride drive read method, possible values: BE, D8, BE_CDDA");
    LOG("\t--drive-sector-order=VALUE\t\toverride drive sector order, possible values: DATA_C2_SUB, DATA_SUB_C2");
    LOG("\t--speed=VALUE\t\tdrive read speed, optimal drive speed will be used if not provided");
    LOG("\t--retries=VALUE\tnumber of sector retries in case of SCSI/C2 error (default: {})", retries);
    LOG("\t--refine-subchannel\tIn addition to SCSI/C2, refine subchannel");
    LOG("\t--lba-start=VALUE\tLBA to start dumping from");
    LOG("\t--lba-end=VALUE\tLBA to stop dumping at (everything before the value), useful for discs with fake TOC");
    LOG("\t--force-qtoc\tForce QTOC based track split");
    LOG("\t--skip=VALUE\tLBA ranges of sectors to skip");
    LOG("\t--skip-fill=VALUE\tfill byte value for skipped sectors (default: 0x{:02X})", skip_fill);
    LOG("\t--skip-size=VALUE\trings mode, number of sectors to skip on SCSI error (default: {})", skip_size);
    LOG("\t--ring-size=VALUE\trings mode, maximum ring size to stop subdivision (rings, default: {})", ring_size);
    LOG("\t--iso9660-trim\ttrim each ISO9660 data track to PVD volume size, useful for discs with fake TOC");
    LOG("\t--plextor-skip-leadin\tskip dumping lead-in using negative range");
    LOG("\t--asus-skip-leadout\tskip extracting lead-out from drive cache");
    LOG("\t--correct-offset-shift\tcorrect disc write offset shift");
    LOG("\t--cdi-ready-normalize\tseparate CDi-Ready track 1 index 0 to track 0");
    LOG("\t--force-offset=VALUE\toverride offset autodetection and use supplied value");
    LOG("\t--perfect-audio-offset\tintelligent silence based audio offset detection");
    LOG("\t--audio-silence-threshold=VALUE\tmaximum absolute sample value to treat it as silence (default: {})", audio_silence_threshold);
}

}
