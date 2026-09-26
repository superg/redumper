module;
// #include <algorithm>
// #include <climits>
// #include <cstdint>
// #include <filesystem>
// #include <fstream>
// #include <map>
// #include <string>
// #include <vector>
// #include "throw_line.hh"

// export module dvd.key;
//
// import cd.cdrom;
// import common;
// import dvd.css;
// import filesystem.iso9660;
// import options;
// import readers.disc_read_reader;
// import readers.image_iso_reader;
// import readers.data_reader;
// import scsi.cmd;
// import scsi.mmc;
// import scsi.sptd;
// import utils.logger;
// import utils.misc;
// import utils.strings;


// import readers.data_reader;
// import utils.strings;
// import utils.file_io;
// import utils.hex_bin;

#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include "systems/system.hh"
#include "throw_line.hh"

export module bd.key;

import common;
import hash.sha1;
import options;
import readers.data_reader;


import filesystem.udf;

// import readers.image_iso_reader;

import readers.image_iso_reader;

// import utils.logger;


import cd.cdrom;
import cd.common;
// import common;
import filesystem.iso9660;
// import options;
import readers.image_bin_reader;
// import readers.image_iso_reader;
import readers.image_raw_reader;
// import readers.data_reader;
import systems.systems;
import utils.hex_bin;
import utils.logger;
import utils.misc;
import utils.strings;


// import filesystem.iso9660;
// import readers.data_reader;
// import utils.misc;
// import utils.strings;

// import filesystem.iso9660;
// import readers.image_iso_form1_reader;

// import readers.image_simple_reader;

namespace gpsxre
{

// std::string region_string(uint8_t region_bits)
// {
//     std::string region;
//
//     for(uint32_t i = 0; i < CHAR_BIT; ++i)
//         if(!(region_bits & 1 << i))
//             region += std::to_string(i + 1) + " ";
//
//     if(!region.empty())
//         region.pop_back();
//
//     return region;
// }
//
//
// std::map<std::string, std::pair<uint32_t, uint32_t>> hextract_vob_list(DataReader *data_reader)
// {
//     std::map<std::string, std::pair<uint32_t, uint32_t>> titles;
//
//     LOG("HUMBUG surely we get here at LEAST");
//
//     iso9660::PrimaryVolumeDescriptor pvd;
//     if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
//         return titles;
//
//     // awwwwwwwwwwwww FUCK
//     // it doesnt work with udf
//     // FUCK FUCK FUCK FUCK FUCK FUCK FUCK FUCK FUCK
//     // this is going to be a fun comment to read in the morning
//
//     LOG("HUMBUG so far so good");
//
//     auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);
//     if (!root_directory) {
//         LOG("HUMBUG aw fuck");
//     }
//
//     auto exe_file = root_directory->subEntry("AACS/MKB_RO.inf");
//     if(!exe_file) {
//         LOG("HUMBUG well we werent able to get the file opene");
//         return titles;
//         // return exit_code;
//     }
//
//     // okay interesting thigns are happening
//     // ohh
//     // okay
//     // https://github.com/superg/redumper/issues/128
//     // http://www.osta.org/pdf/udf260.pdf
//     // https://www.psdevwiki.com/ps3/images/7/74/Udf260.pdf
//
//     // ill have to be a real developer and everything
//     // reading specs and whatnot
//     // too existential in the comments reflection of the ongoing situation and lack of release valves ah well
//
//     // AssasinsCreedIVBlackFlag.iso works
//     // straight ps4 udf
//     // but
//     // oh
//     // her@bailey ~/tmp> file AssasinsCreedIVBlackFlag.iso
//     // AssasinsCreedIVBlackFlag.iso: ISO 9660 CD-ROM filesystem data 'PS4VOLUME'
//     // her@bailey ~/tmp> file SpidermanBonusDisc.iso
//     // SpidermanBonusDisc.iso: ISO 9660 CD-ROM filesystem data 'SPIDERMAN' + UDF filesystem data (version 1.x)
//     // her@bailey ~/tmp> file Ukko\'s\ Journey\ \(Denmark\).iso
//     // Ukko's Journey (Denmark).iso: UDF filesystem data (version 2.x)
//     // her@bailey ~/tmp> file Netflix\ Instant\ Streaming\ Disc\ for\ PlayStation\ 3\ \(USA\).iso
//     // Netflix Instant Streaming Disc for PlayStation 3 (USA).iso: UDF filesystem data (version 2.x)
//     // wellllllllll shit
//     // fucl
//     // balls
//     // triple balls
//     // her@bailey ~/tmp> file ../Preservation/XBOXSERIESX/BigBuckHunterUltimateTrophy/BigBuckHunterUltimateTrophy.iso
//     // ../Preservation/XBOXSERIESX/BigBuckHunterUltimateTrophy/BigBuckHunterUltimateTrophy.iso: ISO 9660 CD-ROM filesystem data 'CD_ROM' + UDF filesystem data (version 1.x)
//     // broooooooo
//     // yove got to be kidding me
//     //for FUCKS sake
//     // oh and also the file command is wrong
//     // whatever
//
//     // yeah anyway new plan
//     // just use the moutned path
//     // wait its redumper
//     // do we have a mounted path
//     // ../README.md:The disc has to be unmounted before running redumper. I suggest disabling removable drives automounting.
//     // no
//     // no we dont.
//     // gREAT
//
//     // 6.16.4.1 Requirements for BDAV and BDMV Application usage
//     // The following additional requirements are applied for BDAV and BDMV Application
//     // usage:
//     // 1. A volume set shall consist of only one volume.
//     // 2. Only one prevailing Partition Descriptor shall be recorded in the Volume
//     // Descriptor Sequence.
//     // 3. A Metadata Partition Map shall be recorded.
//     // 4. Symbolic Links shall not be used for all files and directories (the value of the File
//     // Type field in the ICB shall not be 12).
//     // 5. Hard Link shall not be used for all files and directories.
//     // 6. Multisession and VAT recording shall not be used.
//
//     // auto exe_file = root_directory->subEntry("AACS/MKB_RO.inf");
//
//
//     auto exe = exe_file->read();
//     // if(exe.size() < _EXE_MAGIC.size() || !std::equal(_EXE_MAGIC.cbegin(), _EXE_MAGIC.cend(), exe.cbegin()))
//     //     return exit_code;
//
//     std::stringstream os;
//
//     os << std::format("  EXE: {}", "AACS slash MKB_RO.inf") << std::endl;
//
//     {
//         time_t t = exe_file->dateTime();
//         std::stringstream ss;
//         ss << std::put_time(localtime(&t), "%Y-%m-%d");
//         os << std::format("  EXE date: {}", ss.str()) << std::endl;
//     }
//
//     LOG_F("{}", os.str());
//
//     LOG("HUMBUG ok well we've gotten to the end of the file so uhhh ?");
//
//
//     auto video_ts = root_directory->subEntry("VIDEO_TS");
//     if(!video_ts)
//         return titles;
//
//     auto entries = video_ts->entries();
//     for(auto e : entries)
//     {
//         if(e->isDirectory())
//             continue;
//
//         if(e->name().ends_with(".VOB"))
//             titles[e->name()] = std::pair(e->sectorsLBA(), e->sectorsLBA() + e->sectorsSize());
//     }
//
//     return titles;
//
//
// }
//
//
// std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> create_vts_groups(const std::map<std::string, std::pair<uint32_t, uint32_t>> &vobs)
// {
//     std::vector<std::pair<uint32_t, uint32_t>> groups;
//
//     for(auto const &v : vobs)
//         groups.push_back(v.second);
//     std::sort(groups.begin(), groups.end(), [](const std::pair<uint32_t, uint32_t> &v1, const std::pair<uint32_t, uint32_t> &v2) -> bool { return v1.first < v2.first; });
//     for(bool merge = true; merge;)
//     {
//         merge = false;
//         for(uint32_t i = 0; i + 1 < groups.size(); ++i)
//         {
//             if(groups[i].second == groups[i + 1].first)
//             {
//                 groups[i].second = groups[i + 1].second;
//                 groups.erase(groups.begin() + i + 1);
//
//                 merge = true;
//                 break;
//             }
//         }
//     }
//
//     std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> vts;
//     for(auto const &g : groups)
//         vts[g] = std::vector<uint8_t>();
//
//     return vts;
// }
//
//
// export int redumper_dvdkey(Context &ctx, Options &options)
// {
//     int exit_code = 0;
//
//     if(ctx.disc_type != DiscType::DVD)
//         return exit_code;
//
//     // protection
//     std::vector<uint8_t> copyright;
//     auto status = cmd_read_disc_structure(*ctx.sptd, copyright, 0, 0, 0, READ_DISC_STRUCTURE_Format::COPYRIGHT, 0);
//     if(!status.status_code)
//     {
//         strip_response_header(copyright);
//
//         auto ci = (READ_DVD_STRUCTURE_CopyrightInformation *)copyright.data();
//         auto cpst = (READ_DVD_STRUCTURE_CopyrightInformation_CPST)ci->copyright_protection_system_type;
//
//         LOG("copyright: ");
//
//         std::string protection("unknown");
//         if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::NONE)
//             protection = "<none>";
//         else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
//             protection = "CSS/CPPM";
//         else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CPRM)
//             protection = "CPRM";
//         LOG("  protection system type: {}", protection);
//         LOG("  region management information: {}", region_string(ci->region_management_information));
//
//         if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
//         {
//             Disc_READ_Reader reader(*ctx.sptd, 0);
//             auto vobs = extract_vob_list(&reader);
//
//             bool cppm = false;
//
//             CSS css(*ctx.sptd);
//
//             auto disc_key = css.getDiscKey(cppm);
//             if(!disc_key.empty())
//                 LOG("  disc key: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}", disc_key[0], disc_key[1], disc_key[2], disc_key[3], disc_key[4]);
//
//             if(!vobs.empty())
//             {
//                 // determine continuous VTS groups
//                 auto vts = create_vts_groups(vobs);
//
//                 // attempt to get title keys from the disc
//                 for(auto &v : vts)
//                     v.second = css.getTitleKey(disc_key, v.first.first, cppm);
//
//                 // authenticate for reading
//                 css.getDiscKey(cppm);
//
//                 // crack remaining title keys (region lock)
//                 for(auto &v : vts)
//                     if(v.second.empty())
//                         v.second = CSS::crackTitleKey(v.first.first, v.first.second, reader);
//
//                 // assign keys from VTS groups to individual files
//                 std::map<std::string, std::vector<uint8_t>> title_keys;
//                 for(auto const &v : vobs)
//                 {
//                     for(auto const &vv : vts)
//                         if(v.second.first >= vv.first.first && v.second.second <= vv.first.second)
//                         {
//                             title_keys[v.first] = vv.second;
//                             break;
//                         }
//                 }
//
//                 LOG("  title keys:");
//                 for(auto const &t : title_keys)
//                 {
//                     std::string title_key;
//                     if(t.second.empty())
//                         title_key = "<error>";
//                     else if(is_zeroed(t.second.data(), t.second.size()))
//                         title_key = "<none>";
//                     else
//                         title_key = std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", t.second[0], t.second[1], t.second[2], t.second[3], t.second[4]);
//
//                     LOG("    {}: {}", t.first, title_key);
//                 }
//             }
//         }
//         else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CPRM)
//         {
//             LOG("warning: CPRM protection is unsupported");
//         }
//     }
//
//     return exit_code;
// }

export int redumper_bdisokey(Context &ctx, Options &options)
{
    int exit_code = 0;

    // sha1sum "/media/her/NETFLIX INSTANT STREAMING DISC/AACS/Unit_Key_RO.inf"
    // ed7c6349776512fb4e20e56b26c4b49c2ecb4d5e  /media/her/NETFLIX INSTANT STREAMING DISC/AACS/Unit_Key_RO.inf

    /*
        auto eimage_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();

        Image_ISO_Reader ereader(eimage_prefix + ".iso");
        auto vobs = hextract_vob_list(&ereader);*/

    // auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();

    // Image_ISO_Reader iso_reader(image_prefix + ".iso");
    // DataReader data_reader = iso_reader.get();

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    Image_ISO_Reader reader(image_prefix + ".iso");
    LOG("erm so heres the sector sice {}", reader.sectorSize());

    // auto root_directory_but_its_just_a_number = udf::Browser::rootDirectory(&reader);
    auto root = udf::Browser::rootDirectory(&reader);
    LOG("TEST root is directory: {}", root && root->isDirectory());
    if(root)
    {
        auto directory_data = root->read();
        LOG("TEST root read size: {}", directory_data.size());
        for(uint32_t i = 0; i < directory_data.size(); ++i)
        {
            // LOG("TEST root data {:03d} {:02x}", i, directory_data[i]);
        }
        auto root_entries = root->entries();
        LOG("TEST root entry count: {}", root_entries.size());
        for(auto const &e : root_entries)
            LOG("TEST root entry: {} {}", e->name(), e->isDirectory() ? "DIR" : "FILE");

        // auto sub = root->subEntry("PS3_GAME");
        // LOG("TEST subEntry PS3_GAME found: {}", sub != nullptr);
        auto sub = root->subEntry("AACS");
        LOG("TEST subEntry AACS found: {}", sub != nullptr);
        if(sub)
        {
            auto aacs_entries = sub->entries();
            LOG("TEST AACS entry count: {}", aacs_entries.size());
            for(auto const &e : aacs_entries)
                LOG("TEST AACS entry: {} {}", e->name(), e->isDirectory() ? "DIR" : "FILE");

            auto mkb_ro = sub->subEntry("MKB_RO.inf");
            LOG("TEST subEntry AACS/MKB_RO.inf found: {}", mkb_ro != nullptr);
            if(mkb_ro)
            {
                auto data = mkb_ro->read();
                LOG("TEST AACS/MKB_RO.inf read size: {}", data.size());
                LOG("TEST AACS/MKB_RO.inf head: {}", rawhexdump(data.data(), std::min<uint32_t>((uint32_t)data.size(), 32), 2, 16));
            }

            auto unit_key = sub->subEntry("Unit_Key_RO.inf");
            LOG("TEST subEntry AACS/Unit_Key_RO.inf found: {}", unit_key != nullptr);
            if(unit_key)
            {
                auto data = unit_key->read();
                LOG("TEST AACS/Unit_Key_RO.inf read size: {}", data.size());
                LOG("TEST AACS/Unit_Key_RO.inf head: {}", rawhexdump(data.data(), std::min<uint32_t>((uint32_t)data.size(), 32), 2, 16));
                SHA1 sha1;
                sha1.update(data.data(), data.size());
                LOG("TEST AACS/Unit_Key_RO.inf sha1: {}", sha1.final());
            }
        }
    }

    return 0;

    // NOTE HOLD ========================================================================================================================

    // auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    //
    // // Image_ISO_Reader iso_reader(image_prefix + ".iso");
    //
    // Image_ISO_Reader reader(image_prefix + ".iso");
    //
    // LOG("erm so heres the sector sice {}", reader.sectorSize());
    //
    // iso9660::PrimaryVolumeDescriptor pvd;
    // // iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY);
    //
    // if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, &reader, iso9660::VolumeDescriptorType::PRIMARY)) {
    //     LOG("we werent able to get the pvd, for some reaosn??");
    //     LOG("heres the something i gues {}", image_prefix + ".iso");
    //     // LOG("throwing something at the wall {}", reader);
    //     return exit_code;
    // }
    //     // return exit_code;
    // auto root_directory = iso9660::Browser::rootDirectory(&reader, pvd);

    // NOTE HOLD ========================================================================================================================

    // auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();

    // ok so we know the file exists

    // std::list<std::pair<std::string, TrackType>> tracks;
    // if(std::filesystem::exists(image_prefix + ".cue"))
    // {
    //     for(auto const &t : cue_get_entries(image_prefix + ".cue"))
    //         tracks.emplace_back((std::filesystem::path(options.image_path) / t.first).string(), t.second);
    // }
    // else if(std::filesystem::exists(image_prefix + ".iso"))
    // {
    //     tracks.emplace_back(image_prefix + ".iso", TrackType::MODE1_2048);
    // }
    // else
    //     throw_line("image file not found");


    // tracks.emplace_back(image_prefix + ".iso", TrackType::MODE1_2048);

    // Image_ISO_Form1Reader reader(iso_path);
    //
    // iso9660::PrimaryVolumeDescriptor pvd;
    // if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, &reader, iso9660::VolumeDescriptorType::PRIMARY))
    // {
    //     auto root_directory = iso9660::Browser::rootDirectory(&reader, pvd);
    //     auto entry = root_directory->subEntry("AACS/MKB_RW.inf");
    //     if(entry)
    //     {
    //         auto data = entry->read(); // std::vector<uint8_t>
    //         // use data...
    //     }
    // }

    // bool separate_nl = false;
    // for(auto const &t : tracks)
    // {
    // std::shared_ptr<DataReader> raw_reader;
    // std::shared_ptr<DataReader> form1_reader;

    // if(track_type_is_data_iso(t.second))
    //     form1_reader = std::make_shared<Image_ISO_Reader>(t.first);
    // else if(track_type_is_data_raw(t.second))
    // {
    //     raw_reader = std::make_shared<Image_RAW_Reader>(t.first);
    //     form1_reader = std::make_shared<Image_BIN_Reader>(t.first);
    // }

    // Image_ISO_Reader iso_reader(image_prefix + ".iso");

    // form1_reader = std::make_shared<Image_ISO_Reader>(t.first);
    // form1_reader = std::make_shared<Image_ISO_Reader>(image_prefix + ".iso");
    //
    // // Image_ISO_Reader iso_reader(image_prefix + ".iso");
    //
    // auto reader = form1_reader;
    // // auto reader = iso_reader;
    //
    // Image_ISO_Form1Reader reader(iso_path);
    // Image_ISO_Form1Reader reader(image_prefix + ".iso");

    // Image_SimpleReader<SectorReader, 2048> reader(iso_path);
    // Image_SimpleReader<SectorReader, 2048> reader(image_prefix + ".iso");
    // Image_SimpleReader<DataReader, 2048> reader(image_prefix + ".iso");
    // export using Image_ISO_Reader = Image_SimpleReader<DataReader, FORM1_DATA_SIZE>;

    // std::shared_ptr<DataReader> form1_reader;

    // ohh because it makes shared we need to do use get or something
    // ermmmmmmmmmmmmmmmm
    // yeahhh im not too confirmed bout that whole thing
    // form1_reader = std::make_shared<Image_ISO_Reader>(image_prefix + ".iso");

    // Image_ISO_Reader reader(image_prefix + ".iso");
    //
    // LOG("erm so heres the sector sice {}", reader.sectorSize());
    //
    // // hextract_vob_list(&reader);
    // auto uh = hextract_vob_list(&reader);

    // sectorSize

    // auto reader = form1_reader;

    // DataReader *reader = form1_reader.get();


    // export bool track_type_is_data_iso(TrackType track_type)
    // {
    //     return track_type == TrackType::MODE1_2048 || track_type == TrackType::MODE2_2048;
    // }

    // ughhhhhhhh
    // Image_ISO_Reader reader(image_prefix + ".iso");

    // ohforFUCKS sake why doesnt THIS work AAAAAAAAAA

    // iso9660::PrimaryVolumeDescriptor pvd;
    // if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, reader, iso9660::VolumeDescriptorType::PRIMARY))
    // return;

    // iso9660::PrimaryVolumeDescriptor pvd;
    // // iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, form1_reader.get(), iso9660::VolumeDescriptorType::PRIMARY);
    //
    // if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, &reader, iso9660::VolumeDescriptorType::PRIMARY)) {
    //   LOG("we werent able to get the pvd, for some reaosn??");
    //   LOG("heres the something i gues {}", image_prefix + ".iso");
    //   // LOG("throwing something at the wall {}", reader);
    //   return exit_code;
    // }
    //     // return exit_code;
    // auto root_directory = iso9660::Browser::rootDirectory(&reader, pvd);

    // iso9660::PrimaryVolumeDescriptor pvd;
    // if(iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, &reader, iso9660::VolumeDescriptorType::PRIMARY))
    // {
    //     auto root_directory = iso9660::Browser::rootDirectory(&reader, pvd);
    //     auto entry = root_directory->subEntry("AACS/MKB_RW.inf");
    //     if(entry)
    //     {
    //         auto data = entry->read(); // std::vector<uint8_t>
    //     }
    // }


    // auto reader = system->getType() == System::Type::ISO ? form1_reader : raw_reader;
    // if(!reader) {
    //     LOG("bro we couldnt even get a reader tf");
    //     return exit_code;
    //     // continue;
    // }


    // DataReader *data_reader = reader.get();

    // ohhh okay we cant get the iso9660 filesystem because its a bluray which doesnt have that
    // i think
    // blurays have udf instead  ?
    // ohh okay uhh
    // UDF shares the basic volume descriptor format with ISO 9660.
    // A "UDF Bridge" format is defined since 1.50 so that a disc can also contain a ISO 9660 file system making references to files on the UDF part.[5]

    // so the volume descripter should work fine..
    // if i had to guess the dubious data reader is to blame

    // iso9660::PrimaryVolumeDescriptor pvd;
    // if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, reader.get(), iso9660::VolumeDescriptorType::PRIMARY)) {
    //   LOG("we werent able to get the pvd, for some reaosn??");
    //   LOG("heres the something i gues {}", image_prefix);
    //   return exit_code;
    // }
    //     // return exit_code;
    // auto root_directory = iso9660::Browser::rootDirectory(reader.get(), pvd);

    // iso9660::PrimaryVolumeDescriptor pvd;
    // if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
    //     return;
    // auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);
    //
    // auto ps3_disc_sfb = loadSFB(root_directory->subEntry("PS3_DISC.SFB"));

    // update missing info from SFO

    // ok so we know for real that we

    // if(serial.empty() || version.empty())
    // {
    //     auto sfo_entry = root_directory->subEntry("PS3_GAME/PARAM.SFO");

    // iso9660::PrimaryVolumeDescriptor pvd;
    // if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY)) {
    //   LOG("we werent able to get the pvd, for some reaosn??");
    //   LOG("heres the something i gues {}", image_prefix);
    //   return exit_code;
    // }
    //     // return exit_code;
    // auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);

    // auto system_cnf = loadCNF(root_directory, "SYSTEM.CNF");
    // auto it = system_cnf.find("BOOT2");
    // if(it == system_cnf.end())
    //     return;
    //
    // std::smatch matches;
    // std::regex_match(it->second, matches, std::regex("^cdrom0?:\\\\*(.*?)(?:;.*|$)"));
    // if(matches.size() != 2)
    //     return;
    //
    // auto exe_path = str_uppercase(matches.str(1));
    // AACS\MKB_RO.inf

    // auto exe_file = root_directory->subEntry(exe_path);
    // auto exe_file = root_directory->subEntry("AACS\MKB_RO.inf");
    // auto exe_file = root_directory->subEntry("AACS\\MKB_RO.inf");
    // ermmmmmmmmmmmm
    // auto exe_file = root_directory->subEntry("AACS")->subEntry("MKB_RO.inf");

    // NOTE HOLD ========================================================================================================================

    // auto exe_file = root_directory->subEntry("AACS/MKB_RO.inf");
    // if(!exe_file) {
    //   LOG("well we werent able to get the file opene");
    //   return exit_code;
    // }
    //
    // // auto exe_file = root_directory->subEntry("AACS/MKB_RO.inf");
    //
    //
    // auto exe = exe_file->read();
    // // if(exe.size() < _EXE_MAGIC.size() || !std::equal(_EXE_MAGIC.cbegin(), _EXE_MAGIC.cend(), exe.cbegin()))
    // //     return exit_code;
    //
    // std::stringstream os;
    //
    // os << std::format("  EXE: {}", "AACS slash MKB_RO.inf") << std::endl;
    //
    // {
    //     time_t t = exe_file->dateTime();
    //     std::stringstream ss;
    //     ss << std::put_time(localtime(&t), "%Y-%m-%d");
    //     os << std::format("  EXE date: {}", ss.str()) << std::endl;
    // }
    //
    // LOG_F("{}", os.str());
    //
    // LOG("ok well we've gotten to the end of the file so uhhh ?");

    // NOTE HOLD ========================================================================================================================

    // return

    // for(auto const &s : Systems::get())
    // {
    //     auto system = s();
    //
    //     auto reader = system->getType() == System::Type::ISO ? form1_reader : raw_reader;
    //     if(!reader)
    //         continue;
    //
    //     std::stringstream ss;
    //     // system->printInfo(ss, reader.get(), t.first, options.verbose);
    //
    //     if(separate_nl)
    //         LOG("");
    //     separate_nl = true;
    //
    //     // ohfor FUCKS SAKE aAAAAAAAAAAAAAAAAAaaa AAAAAAAAA
    //
    //     LOG("{} [{}]:", system->getName(), std::filesystem::path(t.first).filename().string());
    //     LOG_F("{}", ss.str());
    //
    //     // if(ss.rdbuf()->in_avail())
    //     // {
    //     //     if(separate_nl)
    //     //         LOG("");
    //     //     separate_nl = true;
    //     //
    //     //     LOG("{} [{}]:", system->getName(), std::filesystem::path(t.first).filename().string());
    //     //     LOG_F("{}", ss.str());
    //     // }
    // }
    // }

    // void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &track_path, bool) const override

    // for(auto const &s : Systems::get())
    // {
    //     auto system = s();
    //
    //     auto reader = system->getType() == System::Type::ISO ? form1_reader : raw_reader;
    //     if(!reader)
    //         continue;
    //
    //     std::stringstream ss;
    //     system->printInfo(ss, reader.get(), t.first, options.verbose);
    //
    //     if(ss.rdbuf()->in_avail())
    //     {
    //         if(separate_nl)
    //             LOG("");
    //         separate_nl = true;
    //
    //         LOG("{} [{}]:", system->getName(), std::filesystem::path(t.first).filename().string());
    //         LOG_F("{}", ss.str());
    //     }
    // }

    // export void read_entry(std::fstream &fs, uint8_t *data, uint64_t entry_size, uint64_t index, uint64_t count, int64_t byte_offset, uint8_t fill_byte)

    // BD-Video Protection: AACS (MKB version 15)


    // the format is also literally documented:
    // The version is in the AACS\MKB_RO.inf file
    //
    // https://aacsla.com/aacs-specifications/
    // Introduction and Common Cryptographic Elements Book Rev 0.953 (PDF, 557 KB)
    // 3.2.5.1.1 Type and Version Record
    //
    // big endian
    // pretty sure it starts at 0x00000008 and goes to and including 0x0000000B


    // LOG("ermmmmmmm");
    // auto vobs = extract_vob_list(&reader);
    // if(!vobs.empty())
    // {
    //     // determine continuous VTS groups
    //     auto vts = create_vts_groups(vobs);
    //
    //     // crack title keys
    //     for(auto &v : vts)
    //         v.second = CSS::crackTitleKey(v.first.first, v.first.second, reader);
    //
    //     // assign keys from VTS groups to individual files
    //     std::map<std::string, std::vector<uint8_t>> title_keys;
    //     for(auto const &v : vobs)
    //     {
    //         for(auto const &vv : vts)
    //             if(v.second.first >= vv.first.first && v.second.second <= vv.first.second)
    //             {
    //                 title_keys[v.first] = vv.second;
    //                 break;
    //             }
    //     }
    //
    //     LOG("title keys:");
    //     for(auto const &t : title_keys)
    //     {
    //         std::string title_key;
    //         if(t.second.empty())
    //             title_key = "<error>";
    //         else if(is_zeroed(t.second.data(), t.second.size()))
    //             title_key = "<none>";
    //         else
    //             title_key = std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", t.second[0], t.second[1], t.second[2], t.second[3], t.second[4]);
    //
    //         LOG("  {}: {}", t.first, title_key);
    //     }
    // }

    return exit_code;
}


}
