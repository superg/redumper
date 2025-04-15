module;
#include <format>
#include <memory>
#include <string>
#include "throw_line.hh"

export module options;

import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export struct Options
{
    std::string command;
    std::string arguments;

    bool help;
    bool version;
    bool verbose;
    bool auto_eject;
    bool debug;

    std::string image_path;
    std::string image_name;
    bool overwrite;
    std::unique_ptr<std::string> disc_type;
    bool force_split;
    bool leave_unchanged;

    std::string drive;
    std::unique_ptr<std::string> drive_type;
    std::unique_ptr<int> drive_read_offset;
    std::unique_ptr<int> drive_c2_shift;
    std::unique_ptr<int> drive_pregap_start;
    std::unique_ptr<std::string> drive_read_method;
    std::unique_ptr<std::string> drive_sector_order;
    std::unique_ptr<double> speed;
    int retries;
    bool refine_subchannel;
    std::unique_ptr<std::string> cd_continue;
    std::unique_ptr<int> lba_start;
    std::unique_ptr<int> lba_end;
    bool force_qtoc;
    bool legacy_subs;
    std::string skip;
    int skip_fill;
    bool iso9660_trim;
    bool plextor_skip_leadin;
    int plextor_leadin_retries;
    bool plextor_leadin_force_store;
    bool asus_skip_leadout;
    int asus_leadout_retries;
    bool disable_cdtext;
    bool correct_offset_shift;
    bool offset_shift_relocate;
    std::unique_ptr<int> force_offset;
    int audio_silence_threshold;
    std::unique_ptr<int> dump_write_offset;
    int dump_read_size;
    bool overread_leadout;
    bool force_unscrambled;
    bool force_refine;
    std::string firmware;
    bool drive_test_skip_plextor_leadin;
    bool drive_test_skip_cache_read;
    bool skip_subcode_desync;


    Options(int argc, const char *argv[])
        : help(false)
        , version(false)
        , verbose(false)
        , auto_eject(false)
        , debug(false)
        , overwrite(false)
        , force_split(false)
        , leave_unchanged(false)
        , retries(0)
        , refine_subchannel(false)
        , force_qtoc(false)
        , legacy_subs(false)
        , skip_fill(0x55)
        , iso9660_trim(false)
        , plextor_skip_leadin(false)
        , plextor_leadin_retries(4)
        , plextor_leadin_force_store(false)
        , asus_skip_leadout(false)
        , asus_leadout_retries(32)
        , disable_cdtext(false)
        , correct_offset_shift(false)
        , offset_shift_relocate(false)
        , audio_silence_threshold(32)
        , dump_read_size(32)
        , overread_leadout(false)
        , force_unscrambled(false)
        , force_refine(false)
        , drive_test_skip_plextor_leadin(false)
        , drive_test_skip_cache_read(false)
        , skip_subcode_desync(false)
    {
        for(int i = 1; i < argc; ++i)
            arguments += str_quoted_if_space(argv[i]) + " ";
        if(!arguments.empty())
            arguments.pop_back();

        std::string *s_value = nullptr;
        int *i_value = nullptr;
        double *d_value = nullptr;
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
                    else if(key == "--version")
                        version = true;
                    else if(key == "--verbose")
                        verbose = true;
                    else if(key == "--auto-eject")
                        auto_eject = true;
                    else if(key == "--debug")
                        debug = true;
                    else if(key == "--image-path")
                        s_value = &image_path;
                    else if(key == "--image-name")
                        s_value = &image_name;
                    else if(key == "--overwrite")
                        overwrite = true;
                    else if(key == "--disc-type")
                    {
                        disc_type = std::make_unique<std::string>();
                        s_value = disc_type.get();
                    }
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
                        speed = std::make_unique<double>();
                        d_value = speed.get();
                    }
                    else if(key == "--retries")
                        i_value = &retries;
                    else if(key == "--refine-subchannel")
                        refine_subchannel = true;
                    else if(key == "--continue")
                    {
                        cd_continue = std::make_unique<std::string>();
                        s_value = cd_continue.get();
                    }
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
                    else if(key == "--legacy-subs")
                        legacy_subs = true;
                    else if(key == "--skip")
                        s_value = &skip;
                    else if(key == "--skip-fill")
                        i_value = &skip_fill;
                    else if(key == "--iso9660-trim")
                        iso9660_trim = true;
                    else if(key == "--plextor-skip-leadin")
                        plextor_skip_leadin = true;
                    else if(key == "--plextor-leadin-retries")
                        i_value = &plextor_leadin_retries;
                    else if(key == "--plextor-leadin-force-store")
                        plextor_leadin_force_store = true;
                    else if(key == "--asus-skip-leadout")
                        asus_skip_leadout = true;
                    else if(key == "--asus-leadout-retries")
                        i_value = &asus_leadout_retries;
                    else if(key == "--disable-cdtext")
                        disable_cdtext = true;
                    else if(key == "--correct-offset-shift")
                        correct_offset_shift = true;
                    else if(key == "--offset-shift-relocate")
                        offset_shift_relocate = true;
                    else if(key == "--force-offset")
                    {
                        force_offset = std::make_unique<int>();
                        i_value = force_offset.get();
                    }
                    else if(key == "--audio-silence-threshold")
                        i_value = &audio_silence_threshold;
                    else if(key == "--dump-write-offset")
                    {
                        dump_write_offset = std::make_unique<int>();
                        i_value = dump_write_offset.get();
                    }
                    else if(key == "--dump-read-size")
                        i_value = &dump_read_size;
                    else if(key == "--overread-leadout")
                        overread_leadout = true;
                    else if(key == "--force-unscrambled")
                        force_unscrambled = true;
                    else if(key == "--force-refine")
                        force_refine = true;
                    else if(key == "--firmware")
                        s_value = &firmware;
                    else if(key == "--drive-test-skip-plextor-leadin")
                        drive_test_skip_plextor_leadin = true;
                    else if(key == "--drive-test-skip-cache-read")
                        drive_test_skip_cache_read = true;
                    else if(key == "--skip-desynced-sectors")
                        skip_subcode_desync = true;
                    // unknown option
                    else
                    {
                        throw_line("unknown option ({})", key);
                    }
                }
                else
                    throw_line("option value expected ({})", argv[i - 1]);
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
                    *i_value = str_to_int(o);
                    i_value = nullptr;
                }
                else if(d_value != nullptr)
                {
                    *d_value = str_to_double(o);
                    d_value = nullptr;
                }
                else
                {
                    if(command.empty())
                        command = o;
                    else
                        throw_line("command already provided ({})", command);
                }
            }
        }
    }


    void printUsage()
    {
        LOG("usage: redumper [command] [options]");
        LOG("");

        LOG("COMMANDS:");
        LOG("\tdisc          \taggregate mode that does everything (default)");
        LOG("\tdump          \tdumps disc to primary dump files");
        LOG("\tdump::extra   \tdumps extended disc areas such as lead-in and lead-out using specific drives");
        LOG("\trefine        \trefines dump files by re-reading the disc");
        LOG("\tverify        \tverifies dump files from the disc and marks any mismatch in state for the subsequent refine");
        LOG("\tdvdkey        \textracts DVD CSS keys from the disc or cracks title keys on region mismatch");
        LOG("\teject         \tejects drive tray");
        LOG("\tdvdisokey     \tcracks DVD CSS keys directly from iso dump, no drive required");
        LOG("\tprotection    \tscans dump files for protections");
        LOG("\tsplit         \tgenerates BIN/CUE track split from dump files");
        LOG("\thash          \toutputs XML DAT hash entries (CUE/BIN or ISO)");
        LOG("\tinfo          \toutputs basic image information (CUE/BIN or ISO)");
        LOG("\tskeleton      \tgenerates image file with zeroed content");
        LOG("\tflash::mt1339 \tMT1339 drive firmware flasher");
        LOG("");

        LOG("OPTIONS:");
        LOG("\t(general)");
        LOG("\t--help,-h                       \tprint usage");
        LOG("\t--version                       \tprint version");
        LOG("\t--verbose                       \tverbose output");
        LOG("\t--auto-eject                    \tauto eject after dump");
        LOG("\t--drive=VALUE                   \tdrive to use, first available drive with disc, if not provided");
        LOG("\t--speed=VALUE                   \tdrive read speed, optimal drive speed will be used if not provided");
        LOG("\t--retries=VALUE                 \tnumber of sector retries in case of SCSI/C2 error (default: {})", retries);
        LOG("\t--image-path=VALUE              \tdump files base directory");
        LOG("\t--image-name=VALUE              \tdump files prefix, autogenerated in dump mode if not provided");
        LOG("\t--overwrite                     \toverwrites previously generated dump files");
        LOG("\t--disc-type=VALUE               \toverride detected disc type (current profile), possible values: CD, DVD, BLURAY, BLURAY-R, HD-DVD");
        LOG("");
        LOG("\t(drive configuration)");
        LOG("\t--drive-type=VALUE              \toverride drive type, possible values: GENERIC, PLEXTOR, LG_ASU2, LG_ASU3, LG_ASU8A, LG_ASU8B, LG_ASU8C");
        LOG("\t--drive-read-offset=VALUE       \toverride drive read offset");
        LOG("\t--drive-c2-shift=VALUE          \toverride drive C2 shift");
        LOG("\t--drive-pregap-start=VALUE      \toverride drive pre-gap start LBA");
        LOG("\t--drive-read-method=VALUE       \toverride drive read method, possible values: BE, D8, BE_CDDA");
        LOG("\t--drive-sector-order=VALUE      \toverride drive sector order, possible values: DATA_C2_SUB, DATA_SUB_C2, DATA_SUB, DATA_C2");
        LOG("");
        LOG("\t(drive specific)");
        LOG("\t--plextor-skip-leadin           \tskip dumping lead-in using negative range");
        LOG("\t--plextor-leadin-retries=VALUE  \tmaximum number of lead-in retries per session (default: {})", plextor_leadin_retries);
        LOG("\t--plextor-leadin-force-store    \tstore unverified lead-in");

        LOG("\t--asus-skip-leadout             \tskip extracting lead-out from drive cache");
        LOG("\t--asus-leadout-retries          \tnumber of preceding lead-out sector reads to fill up the cache (default: {})", asus_leadout_retries);
        LOG("\t--disable-cdtext                \tdisable CD-TEXT reading");
        LOG("");
        LOG("\t(offset)");
        LOG("\t--force-offset=VALUE            \toverride offset autodetection and use supplied value");
        LOG("\t--audio-silence-threshold=VALUE \tmaximum absolute sample value to treat it as silence (default: {})", audio_silence_threshold);
        LOG("\t--correct-offset-shift          \tcorrect disc write offset shift");
        LOG("\t--offset-shift-relocate         \tdon't merge offset groups with non-matching LBA");
        LOG("");
        LOG("\t(split)");
        LOG("\t--force-split                   \tforce track split with errors");
        LOG("\t--leave-unchanged               \tdon't replace erroneous sectors with generated ones");
        LOG("\t--force-qtoc                    \tforce QTOC based track split");
        LOG("\t--legacy-subs                   \treplicate DIC style subchannel based track split");
        LOG("\t--skip-fill=VALUE               \tfill byte value for skipped sectors (default: 0x{:02X})", skip_fill);
        LOG("\t--iso9660-trim                  \ttrim each ISO9660 data track to PVD volume size, useful for discs with fake TOC");
        LOG("");
        LOG("\t(drive test)");
        LOG("\t--drive-test-skip-plextor-leadin\tskip testing for PLEXTOR negative lead-in range access");
        LOG("\t--drive-test-skip-cache-read    \tskip testing for MEDIATEK cache read command (F1)");
        LOG("");
        LOG("\t(miscellaneous)");
        LOG("\t--continue=VALUE                \tcontinue \"cd\" command starting from VALUE command");
        LOG("\t--lba-start=VALUE               \tLBA to start dumping from");
        LOG("\t--lba-end=VALUE                 \tLBA to stop dumping at (everything before the value), useful for discs with fake TOC");
        LOG("\t--refine-subchannel             \tin addition to SCSI/C2, refine subchannel");
        LOG("\t--skip=VALUE                    \tLBA ranges of sectors to skip");
        LOG("\t--dump-write-offset=VALUE       \toffset hint for data sectors read using BE method");
        LOG("\t--dump-read-size=VALUE          \tnumber of sectors to read at once on initial dump, DVD only (default: {})", dump_read_size);
        LOG("\t--overread-leadout              \tdo not limit lead-out to the first hundred sectors, read until drive returns SCSI error");
        LOG("\t--force-unscrambled             \tdo not attempt to read data sectors as audio (BE read method only)");
        LOG("\t--force-refine                  \tdo not check TOC when refining a disc");
        LOG("\t--firmware=VALUE                \tfirmware filename");
        LOG("\t--skip-subcode-desync           \tskip storing sectors with mismatching subcode Q absolute MSF");
    }
};

}
