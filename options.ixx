module;
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "throw_line.hh"

export module options;

import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

struct UsageOption
{
    std::string keys;
    std::string description;
};


struct UsageGroup
{
    std::string name;
    std::vector<UsageOption> options;
};


struct UsageCommand
{
    std::string name;
    std::string description;
};


export struct Options
{
    // built-in defaults for the value options below, shared by the constructor and the usage/man
    // rendering so the printed "default" always reflects the compile-time value, not parsed argv
    static constexpr int retries_default = 0;
    static constexpr int skip_fill_default = 0x55;
    static constexpr int plextor_leadin_retries_default = 4;
    static constexpr int mediatek_leadout_retries_default = 32;
    static constexpr int audio_silence_threshold_default = 32;
    static constexpr int cdr_error_threshold_default = 16;
    static constexpr int scsi_timeout_default = 50000;

    std::string command;
    std::string arguments;

    bool help;
    bool man;
    bool version;
    bool verbose;
    bool list_recommended_drives;
    bool list_all_drives;
    bool auto_eject;
    bool skeleton;
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
    bool refine_sector_mode;
    std::unique_ptr<std::string> cd_continue;
    std::unique_ptr<int> lba_start;
    std::unique_ptr<int> lba_end;
    bool lba_end_by_subcode;
    bool force_qtoc;
    bool legacy_subs;
    std::string skip;
    int skip_fill;
    bool filesystem_trim;
    bool plextor_skip_leadin;
    int plextor_leadin_retries;
    bool plextor_leadin_force_store;
    bool mediatek_skip_leadout;
    int mediatek_leadout_retries;
    bool kreon_partial_ss;
    bool dvd_raw;
    bool bd_raw;
    bool disable_cdtext;
    bool correct_offset_shift;
    bool offset_shift_relocate;
    std::unique_ptr<int> force_offset;
    int audio_silence_threshold;
    std::unique_ptr<int> dump_write_offset;
    std::unique_ptr<int> dump_read_size;
    bool overread_leadout;
    bool force_unscrambled;
    bool force_refine;
    std::string firmware;
    bool force_flash;
    bool drive_test_skip_plextor_leadin;
    bool drive_test_skip_cache_read;
    bool skip_subcode_desync;
    bool rings;
    int cdr_error_threshold;
    int scsi_timeout;


    Options(int argc, const char *argv[])
        : help(false)
        , man(false)
        , version(false)
        , verbose(false)
        , list_recommended_drives(false)
        , list_all_drives(false)
        , auto_eject(false)
        , skeleton(false)
        , debug(false)
        , overwrite(false)
        , force_split(false)
        , leave_unchanged(false)
        , retries(retries_default)
        , refine_subchannel(false)
        , refine_sector_mode(false)
        , lba_end_by_subcode(false)
        , force_qtoc(false)
        , legacy_subs(false)
        , skip_fill(skip_fill_default)
        , filesystem_trim(false)
        , plextor_skip_leadin(false)
        , plextor_leadin_retries(plextor_leadin_retries_default)
        , plextor_leadin_force_store(false)
        , mediatek_skip_leadout(false)
        , mediatek_leadout_retries(mediatek_leadout_retries_default)
        , kreon_partial_ss(false)
        , dvd_raw(false)
        , bd_raw(false)
        , disable_cdtext(false)
        , correct_offset_shift(false)
        , offset_shift_relocate(false)
        , audio_silence_threshold(audio_silence_threshold_default)
        , overread_leadout(false)
        , force_unscrambled(false)
        , force_refine(false)
        , force_flash(false)
        , drive_test_skip_plextor_leadin(false)
        , drive_test_skip_cache_read(false)
        , skip_subcode_desync(false)
        , rings(false)
        , cdr_error_threshold(cdr_error_threshold_default)
        , scsi_timeout(scsi_timeout_default)
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
                    else if(key == "--man")
                        man = true;
                    else if(key == "--version")
                        version = true;
                    else if(key == "--verbose")
                        verbose = true;
                    else if(key == "--list-recommended-drives")
                        list_recommended_drives = true;
                    else if(key == "--list-all-drives")
                        list_all_drives = true;
                    else if(key == "--auto-eject")
                        auto_eject = true;
                    else if(key == "--skeleton")
                        skeleton = true;
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
                    else if(key == "--refine-sector-mode")
                        refine_sector_mode = true;
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
                    else if(key == "--lba-end-by-subcode")
                        lba_end_by_subcode = true;
                    else if(key == "--force-qtoc")
                        force_qtoc = true;
                    else if(key == "--legacy-subs")
                        legacy_subs = true;
                    else if(key == "--skip")
                        s_value = &skip;
                    else if(key == "--skip-fill")
                        i_value = &skip_fill;
                    else if(key == "--filesystem-trim")
                        filesystem_trim = true;
                    else if(key == "--plextor-skip-leadin")
                        plextor_skip_leadin = true;
                    else if(key == "--plextor-leadin-retries")
                        i_value = &plextor_leadin_retries;
                    else if(key == "--plextor-leadin-force-store")
                        plextor_leadin_force_store = true;
                    else if(key == "--mediatek-skip-leadout")
                        mediatek_skip_leadout = true;
                    else if(key == "--mediatek-leadout-retries")
                        i_value = &mediatek_leadout_retries;
                    else if(key == "--kreon-partial-ss")
                        kreon_partial_ss = true;
                    else if(key == "--dvd-raw")
                        dvd_raw = true;
                    else if(key == "--bd-raw")
                        bd_raw = true;
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
                    {
                        dump_read_size = std::make_unique<int>();
                        i_value = dump_read_size.get();
                    }
                    else if(key == "--overread-leadout")
                        overread_leadout = true;
                    else if(key == "--force-unscrambled")
                        force_unscrambled = true;
                    else if(key == "--force-refine")
                        force_refine = true;
                    else if(key == "--firmware")
                        s_value = &firmware;
                    else if(key == "--force-flash")
                        force_flash = true;
                    else if(key == "--drive-test-skip-plextor-leadin")
                        drive_test_skip_plextor_leadin = true;
                    else if(key == "--drive-test-skip-cache-read")
                        drive_test_skip_cache_read = true;
                    else if(key == "--skip-subcode-desync")
                        skip_subcode_desync = true;
                    else if(key == "--rings")
                        rings = true;
                    else if(key == "--cdr-error-threshold")
                        i_value = &cdr_error_threshold;
                    else if(key == "--scsi-timeout")
                        i_value = &scsi_timeout;
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


    static std::string_view helpKeys()
    {
        return "--help,-h";
    }


    void printUsage()
    {
        auto commands = usageCommands();
        auto groups = usageGroups();

        // align descriptions to the widest entry in each section
        std::string::size_type command_width = 0;
        for(auto &c : commands)
            if(c.name.length() > command_width)
                command_width = c.name.length();

        std::string::size_type option_width = 0;
        for(auto &g : groups)
            for(auto &o : g.options)
                if(o.keys.length() > option_width)
                    option_width = o.keys.length();

        LOG("usage: redumper [command] [options]");
        LOG("");

        LOG("COMMANDS:");
        for(auto &c : commands)
            LOG("\t{:<{}}\t{}", c.name, command_width, c.description);
        LOG("");

        LOG("OPTIONS:");
        bool first = true;
        for(auto &g : groups)
        {
            if(!first)
                LOG("");
            first = false;

            LOG("\t({})", g.name);
            for(auto &o : g.options)
                LOG("\t{:<{}}\t{}", o.keys, option_width, o.description);
        }
    }


    // emit a roff man page from the same usage model as printUsage(), for packagers (redumper --man > redumper.1)
    void printUsageMan()
    {
        auto commands = usageCommands();
        auto groups = usageGroups();

        LOGC(".TH REDUMPER 1 \"\" \"redumper {}\" \"User Commands\"", REDUMPER_VERSION_BUILD);
        LOGC(".SH NAME");
        LOGC("redumper \\- {}", manEscape("low-level byte-perfect CD/DVD/HD-DVD/Blu-ray disc dumper"));
        LOGC(".SH SYNOPSIS");
        LOGC(".B redumper");
        LOGC("[command] [options]");

        LOGC(".SH COMMANDS");
        for(auto &c : commands)
        {
            LOGC(".TP");
            LOGC(".B {}", manEscape(c.name));
            manParagraph(c.description);
        }

        LOGC(".SH OPTIONS");
        for(auto &g : groups)
        {
            LOGC(".SS {}", manEscape(g.name));
            for(auto &o : g.options)
            {
                LOGC(".TP");
                LOGC(".B {}", manEscape(o.keys));
                manParagraph(o.description);
            }
        }
    }

private:
    static std::vector<UsageCommand> usageCommands()
    {
        return {
            { "disc",           "aggregate mode that does everything (default)"                                },
            { "dump",           "dumps disc to primary dump files"                                             },
            { "dump::extra",    "dumps extended disc areas such as lead-in and lead-out using specific drives" },
            { "refine",         "refines dump files by re-reading the disc"                                    },
            { "dvdkey",         "extracts DVD CSS keys from the disc or cracks title keys on region mismatch"  },
            { "eject",          "ejects drive tray"                                                            },
            { "dvdisokey",      "cracks DVD CSS keys directly from iso dump, no drive required"                },
            { "protection",     "scans dump files for protections"                                             },
            { "split",          "generates BIN/CUE track split from dump files"                                },
            { "hash",           "outputs XML DAT hash entries (CUE/BIN or ISO)"                                },
            { "info",           "outputs basic image information (CUE/BIN or ISO)"                             },
            { "skeleton",       "generates image file with zeroed content"                                     },
            { "flash::mt1339",  "flashes MT1339 drive firmware"                                                },
            { "flash::mt1959",  "flashes MT1959 drive firmware"                                                },
            { "flash::sd616",   "flashes SD-616F/T drive firmware"                                             },
            { "flash::plextor", "flashes PLEXTOR drive firmware"                                               },
        };
    }


    std::vector<UsageGroup> usageGroups() const
    {
        return {
            { "general",
             {
                    { std::string(helpKeys()), "print usage" },
                    { "--version", "print version" },
                    { "--verbose", "verbose output" },
                    { "--list-recommended-drives", "list recommended drives" },
                    { "--list-all-drives", "list all supported drives" },
                    { "--auto-eject", "auto eject after dump" },
                    { "--skeleton", "generate skeleton after dump" },
                    { "--drive=VALUE", "drive to use, first available drive with disc, if not provided" },
                    { "--speed=VALUE", "drive read speed, optimal drive speed will be used if not provided" },
                    { "--retries=VALUE", std::format("number of sector retries in case of SCSI/C2 error (default: {})", retries_default) },
                    { "--image-path=VALUE", "dump files base directory" },
                    { "--image-name=VALUE", "dump files prefix, autogenerated in dump mode if not provided" },
                    { "--overwrite", "overwrites previously generated dump files" },
                    { "--disc-type=VALUE", "override detected disc type (current profile), possible values: CD, DVD, BLURAY, BLURAY-R, HD-DVD" },
                } },
            { "drive configuration",
             {
                    { "--drive-type=VALUE", "override drive type, possible values: GENERIC, PLEXTOR, MTK2, MTK3, MTK8A, MTK8B, MTK8C" },
                    { "--drive-read-offset=VALUE", "override drive read offset" },
                    { "--drive-c2-shift=VALUE", "override drive C2 shift" },
                    { "--drive-pregap-start=VALUE", "override drive pre-gap start LBA" },
                    { "--drive-read-method=VALUE", "override drive read method, possible values: BE, D8, BE_CDDA" },
                    { "--drive-sector-order=VALUE", "override drive sector order, possible values: DATA_C2_SUB, DATA_SUB_C2, DATA_SUB, DATA_C2" },
                } },
            { "drive specific",
             {
                    { "--plextor-skip-leadin", "skip dumping lead-in using negative range" },
                    { "--plextor-leadin-retries=VALUE", std::format("maximum number of lead-in retries per session (default: {})", plextor_leadin_retries_default) },
                    { "--plextor-leadin-force-store", "store unverified lead-in" },
                    { "--kreon-partial-ss", "get minimal security sector (fixes bad firmware)" },
                    { "--dvd-raw", "dump raw DVD sectors (OmniDrive)" },
                    { "--bd-raw", "dump raw BD sectors (OmniDrive)" },
                    { "--mediatek-skip-leadout", "skip extracting lead-out from drive cache" },
                    { "--mediatek-leadout-retries=VALUE", std::format("number of preceding lead-out sector reads to fill up the cache (default: {})", mediatek_leadout_retries_default) },
                    { "--disable-cdtext", "disable CD-TEXT reading" },
                } },
            { "offset",
             {
                    { "--force-offset=VALUE", "override offset autodetection and use supplied value" },
                    { "--audio-silence-threshold=VALUE", std::format("maximum absolute sample value to treat it as silence (default: {})", audio_silence_threshold_default) },
                    { "--correct-offset-shift", "correct disc write offset shift" },
                    { "--offset-shift-relocate", "don't merge offset groups with non-matching LBA" },
                } },
            { "split",
             {
                    { "--force-split", "force track split with errors" },
                    { "--leave-unchanged", "don't replace erroneous sectors with generated ones" },
                    { "--force-qtoc", "force QTOC based track split" },
                    { "--legacy-subs", "replicate DIC style subchannel based track split" },
                    { "--skip-fill=VALUE", std::format("fill byte value for skipped sectors (default: 0x{:02X})", skip_fill_default) },
                    { "--filesystem-trim", "trim data track to filesystem size (ISO9660: all media, UDF: DVD and later)" },
                } },
            { "drive test",
             {
                    { "--drive-test-skip-plextor-leadin", "skip testing for PLEXTOR negative lead-in range access" },
                    { "--drive-test-skip-cache-read", "skip testing for MEDIATEK cache read command (F1)" },
                } },
            { "miscellaneous",
             {
                    { "--continue=VALUE", "continue \"disc\" command starting from VALUE command" },
                    { "--lba-start=VALUE", "LBA to start dumping from" },
                    { "--lba-end=VALUE", "LBA to stop dumping at (everything before the value), useful for discs with fake TOC" },
                    { "--lba-end-by-subcode", "Dynamically determine LBA end by the last session subcode" },
                    { "--refine-subchannel", "in addition to SCSI/C2, refine subchannel" },
                    { "--refine-sector-mode", "update sector data only if whole sector is C2 error free" },
                    { "--skip=VALUE", "LBA ranges of sectors to skip" },
                    { "--dump-write-offset=VALUE", "offset hint for data sectors read using BE method" },
                    { "--dump-read-size=VALUE", "number of sectors to read at once on initial dump, DVD only" },
                    { "--overread-leadout", "do not limit lead-out to the first hundred sectors, read until drive returns SCSI error" },
                    { "--force-unscrambled", "do not attempt to read data sectors as audio (BE read method only)" },
                    { "--force-refine", "do not check TOC when refining a disc" },
                    { "--firmware=VALUE", "firmware filename" },
                    { "--force-flash", "skip drive vendor/model verification when flashing firmware (WARNING: can brick your drive)" },
                    { "--skip-subcode-desync", "skip storing sectors with mismatching subcode Q absolute MSF" },
                    { "--rings", "enable filesystem based rings detection" },
                    { "--cdr-error-threshold=VALUE", std::format("maximum number of trailing C2 errors allowed on a CD-R (default: {})", cdr_error_threshold_default) },
                    { "--scsi-timeout=VALUE", std::format("SCSI command timeout, milliseconds (default: {})", scsi_timeout_default) },
                } },
        };
    }


    static std::string manEscape(std::string_view s)
    {
        std::string escaped;
        for(char c : s)
        {
            if(c == '\\' || c == '-')
                escaped += '\\';
            escaped += c;
        }
        return escaped;
    }


    // emit a description as a fill-mode paragraph, wrapping the escaped text at word
    // boundaries so no roff source line exceeds the conventional width
    static void manParagraph(std::string_view text)
    {
        std::string escaped = manEscape(text);

        std::string line;
        std::string::size_type start = 0;
        while(start < escaped.length())
        {
            auto end = escaped.find(' ', start);
            auto word = escaped.substr(start, end == std::string::npos ? std::string::npos : end - start);

            if(!line.empty() && line.length() + 1 + word.length() > 78)
            {
                LOGC("{}", line);
                line.clear();
            }

            if(line.empty())
                line = word;
            else
                line += " " + word;

            if(end == std::string::npos)
                break;
            start = end + 1;
        }

        if(!line.empty())
            LOGC("{}", line);
    }
};

}
