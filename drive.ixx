module;
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "throw_line.hh"

export module drive;

import cd.cd;
import cd.subcode;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export struct DriveQuery
{
    std::string vendor_id;
    std::string product_id;
    std::string product_revision_level;
    std::string vendor_specific;
};

export struct DriveConfig
{
    std::string vendor_id;
    std::string product_id;
    std::string product_revision_level;
    std::string vendor_specific;
    int32_t read_offset;
    uint32_t c2_shift;
    int32_t pregap_start;

    enum class ReadMethod
    {
        BE,
        D8,
        BE_CDDA
    } read_method;

    enum class SectorOrder
    {
        DATA_C2_SUB,
        DATA_SUB_C2,
        DATA_SUB,
        DATA_C2
    } sector_order;

    enum class Type
    {
        GENERIC,
        PLEXTOR,
        LG_ASU8A,
        LG_ASU8B,
        LG_ASU8C,
        LG_ASU3,
        LG_ASU2
    } type;
};

struct SectorLayout
{
    uint32_t data_offset;
    uint32_t c2_offset;
    uint32_t subcode_offset;
    uint32_t size;
};

static const std::unordered_map<std::string, int32_t> DRIVE_READ_OFFSETS = {
#include "driveoffsets.inc"
};


static const std::map<DriveConfig::Type, std::string> TYPE_STRING = {
    { DriveConfig::Type::GENERIC,  "GENERIC"  },
    { DriveConfig::Type::PLEXTOR,  "PLEXTOR"  },
    { DriveConfig::Type::LG_ASU8A, "LG_ASU8A" },
    { DriveConfig::Type::LG_ASU8B, "LG_ASU8B" },
    { DriveConfig::Type::LG_ASU8C, "LG_ASU8C" },
    { DriveConfig::Type::LG_ASU3,  "LG_ASU3"  },
    { DriveConfig::Type::LG_ASU2,  "LG_ASU2"  }
};


static const std::map<DriveConfig::ReadMethod, std::string> READ_METHOD_STRING = {
    { DriveConfig::ReadMethod::BE,      "BE"      },
    { DriveConfig::ReadMethod::D8,      "D8"      },
    { DriveConfig::ReadMethod::BE_CDDA, "BE_CDDA" }
};


static const std::map<DriveConfig::SectorOrder, std::string> SECTOR_ORDER_STRING = {
    { DriveConfig::SectorOrder::DATA_C2_SUB, "DATA_C2_SUB" },
    { DriveConfig::SectorOrder::DATA_SUB_C2, "DATA_SUB_C2" },
    { DriveConfig::SectorOrder::DATA_SUB,    "DATA_SUB"    },
    { DriveConfig::SectorOrder::DATA_C2,     "DATA_C2"     }
};


static const DriveConfig DRIVE_CONFIG_GENERIC = { "", "", "", "", 0, 0, 0, DriveConfig::ReadMethod::BE, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC };


// drive strings are normalized (trimmed and exactly one space between words)
// the same normalize operation is performed when detecting the drive and looking up the read offset
// match is performed on the vendor / product / revision level, vendor specific is just for my reference for the drives I own
// if string is empty, the match is always true
// clang-format off
static const std::vector<DriveConfig> KNOWN_DRIVES =
{
    // PLEXTOR OLD
//    {"PLEXTOR", "CD-ROM PX-8XCS"  , "", ""},
//    {"PLEXTOR", "CD-ROM PX-12CS"  , "", ""},
//    {"PLEXTOR", "CD-ROM PX-12TS"  , "", ""},
//    {"PLEXTOR", "CD-ROM PX-20TS"  , "", ""},
//    {"PLEXTOR", "CD-ROM PX-32CS"  , "", ""},
//    {"PLEXTOR", "CD-ROM PX-32TS"  , "", ""},
//    {"HP", "CD-ROM CD32X", "1.02", "01/13/98 01:02"},
//    {"PLEXTOR", "CD-ROM PX-40TS"  , "", ""},
//    {"PLEXTOR", "CD-ROM PX-40TSUW", "", ""},
//    {"PLEXTOR", "CD-ROM PX-40TW"  , "", ""},

    // PLEXTOR CD
    {"PLEXTOR", "CD-R PREMIUM"  , "1.07", "10/04/06 16:00",  +30, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "CD-R PREMIUM2" , ""    , ""              ,  +30, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-320A"  , "1.06", "07/04/03 10:30",  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "CD-R PX-R412C" , ""    , ""              , +355, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-R820T" , ""    , ""              , +355, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-S88T"  , "1.06", "08/05/02 15:00",  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::GENERIC}, // CHECKED
    {"PLEXTOR", "CD-R PX-W1210A", ""    , ""              ,  +99, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W1210S", ""    , ""              ,  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W124TS", ""    , ""              , +943, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W1610A", ""    , ""              ,  +99, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W2410A", ""    , ""              ,  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W4012A", "1.07", "03/22/06 09:00",  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "CD-R PX-W4012S", ""    , ""              ,  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W4220T", ""    , ""              , +355, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB,    DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W4824A", "1.07", "03/24/06 14:00",  +98, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC}, // CHECKED
    {"PLEXTOR", "CD-R PX-W5224A", "1.04", "04/10/06 17:00",  +30, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "CD-R PX-W8220T", ""    , ""              , +355, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "CD-R PX-W8432T", ""    , ""              , +355, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::PLEXTOR},
    // PLEXTOR DVD
    {"PLEXTOR", "DVDR PX-704A"  , ""    , ""              ,  +30, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "DVDR PX-708A"  , "1.12", "03/13/06 21:00",  +30, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "DVDR PX-708A2" , ""    , ""              ,  +30, 294, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "DVDR PX-712A"  , "1.09", "03/31/06 10:00",  +30, 295, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "DVDR PX-714A"  , ""    , ""              ,  +30, 295, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR},
    {"PLEXTOR", "DVDR PX-716A"  , "1.11", "03/23/07 15:10",  +30, 295, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "DVDR PX-716AL" , "1.00", "05/11/05 15:00",  +30, 295, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "DVDR PX-755A"  , "1.08", "08/18/07 15:10",  +30, 295, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED
    {"PLEXTOR", "DVDR PX-760A"  , "1.07", "08/18/07 15:10",  +30, 295, -75, DriveConfig::ReadMethod::D8, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::PLEXTOR}, // CHECKED

    // LG/ASUS (8Mb/3Mb/2Mb cache)
    {"ATAPI"   , "iHBS112 2"       , "PL06", "2012/09/17 10:50"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // CHECKED: LITE-ON
    {"HL-DT-ST", "BD-RE BU40N"     , "1.00", "N003103MOAL36D3653" , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8B},// RibShark
    {"ASUS"    , "BW-16D1HT"       , "3.02", "W000800KL8J9NJ3134" , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU3}, // CHECKED
    {"HL-DT-ST", "BD-RE BH16NS55"  , "1.02", "N000200SIK92G9OF211", +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU3}, // TheMuso
    {"HL-DT-ST", "BD-RE BP50NB40"  , "1.00", "N005505MD8F8BD0700" , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU3}, // olofolleola4
    {"Slimtype", "BD E DS4E1S"     , "EA2B", "2009/11/13 15:21"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU2}, // olofolleola4
    {"Optiarc" , "BD RW BD-5300S"  , "2.03", "2012/02/07 11:25"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"TEAC"    , "BD-W512GSA"      , "PT11", "2012/12/05 19:08"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"ASUS"    , "BW-12B1ST"       , "1.03", "2011/04/18 21:48"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"SONY"    , "BD RW BWU-500S"  , "2.63", "2012/02/07 11:48"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"PLDS"    , "BD-RE DH-8B2SH"  , "SD11", "2011/01/11 17:17"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"hp"      , "BD B DH8B2SHB"   , "SHDQ", "2012/05/09 11:33"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"PLEXTOR" , "BD-R PX-B950SA"  , "1.04", "2012/10/30 10:10"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"PLEXTOR" , "BD-R PX-B950UE"  , "1.05", "2012/10/30 10:10"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"PLEXTOR" , "BD-R PX-LB950SA" , "1.04", "2012/10/30 10:10"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"PLEXTOR" , "BD-R PX-LB950UE" , "1.05", "2012/10/30 10:10"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"HP"      , "BD Writer bd335e", "YH23", "2011/09/09 13:10"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"HP"      , "BD Writer bd335i", "QH21", "2011/05/26 13:49"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"ATAPI"   , "eHBU212 2"       , "ZL06", "2012/11/05 16:10"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"ATAPI"   , "iHBS212 2"       , "HL05", "2012/09/17 10:50"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"ATAPI"   , "iHBS312 2"       , "PL17", "2012/10/31 13:50"   , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8A}, // olofolleola4
    {"HL-DT-ST", "BD-RE WH14NS40"  , "1.03", "N0A09A0K9HF6ND5914" , +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::LG_ASU8C}, // Lugamo

    // OTHER
    {"ASUS"    , "BW-16D1HT"       , "3.10", "WM01601KLZL4TG5625" ,    +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC}, // default or RibShark FW definition
    {"ASUS"    , "SDRW-08D2S-U"    , "B901", "2015/03/03 15:29"   ,    +6, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB_C2, DriveConfig::Type::GENERIC}, // internal model: DU-8A6NH11B
    {"ASUS"    , "SDRW-08U9M-U"    , "A112", "M045600 K0QL92H5616",    +6, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB_C2, DriveConfig::Type::GENERIC},
    {"Lite-On" , "LTN483S 48x Max" , "PD03", ""                   , -1164, 0,    0, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_C2    , DriveConfig::Type::GENERIC},
    {"hp"      , "DVD-ROM TS-H353C", "H410", "R67468CZ11"         ,    +6, 0,    0, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC},
    {"TSSTcorp", "DVD-ROM TS-H352C", "DE02", ""                   ,    +6, 0,    0, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::GENERIC}, // supports C2 only on data
    {"PIONEER" , "BD-RW BDR-209D"  , "1.10", "13/09/10 PIONEER"   ,  +667, 0,    0, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::GENERIC}, // BE_CDDA unscrambles data sectors
    {"HL-DT-ST", "BD-RE WH16NS40"  , "1.05", "N000900KLZL4TG5625" ,    +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC},
    {"HL-DT-ST", "BD-RE WH16NS60"  , "1.02", "NM00100SIK9PH7MJ032",    +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC},
    {"HL-DT-ST", "DVD+-RW GH50N"   , "B103", "000819093O9CQ82239" ,  +667, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::GENERIC},
    {"CREATIVE", "CD5233E-N"       , "0.20", "BTC"                ,   +12, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC},
    {"PLEXTOR" , "DVDR PX-740A"    , "1.02", "12/19/05"           ,  +618, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB   , DriveConfig::Type::GENERIC}, // doesn't stop on lead-out but always returns same sector
    {"PLEXTOR" , "DVDR PX-L890SA"  , "1.07", "2011/11/15 10:15"   ,    +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_SUB_C2, DriveConfig::Type::GENERIC},
    {"HL-DT-ST", "DVDRAM GH24NSC0" , "LY00", "C010101 KMIJ8O50256",    +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_SUB_C2, DriveConfig::Type::GENERIC},
    {"LITE-ON" , "DVD SOHD-167T"   , "9S1B", "2005/03/31 16:41"   ,   +12, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC},
    {"QPS"     , "CD-W524E"        , "1.5A", "10/23/01"           ,  +685, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_C2_SUB, DriveConfig::Type::GENERIC}, // TEAC
    {"ASUS"    , "DRW-24D5MT"      , "1.00", "O01790C K82G7MG1309",    +6, 0, -135, DriveConfig::ReadMethod::BE_CDDA, DriveConfig::SectorOrder::DATA_SUB_C2, DriveConfig::Type::GENERIC}, // Silent
//    {"hp"      , "DVD-RAM SW810"   , "HA05", "1228TP0310"         ,    +6, 0, -135, DriveConfig::ReadMethod::BE     , DriveConfig::SectorOrder::DATA_SUB_C2, DriveConfig::Type::GENERIC},
};
// clang-format on


// AccurateRip database provides already "processed" drive offsets e.g.
// the drive offset number has to be added to the data read start in order to get it corrected
// (positive offset means that data has to be shifted left, negative - right)
int32_t drive_get_generic_read_offset(const std::string &vendor, const std::string &product)
{
    int32_t offset = std::numeric_limits<int32_t>::max();

    // FIXME: clean up this AccurateRip mess later
    std::string v(vendor);
    if(vendor == "HL-DT-ST")
        v = "LG Electronics";
    else if(vendor == "JLMS")
        v = "Lite-ON";
    else if(vendor == "Matshita")
        v = "Panasonic";
    else
        v = vendor;

    std::string vendor_product(std::format("{} - {}", v, product));
    if(auto it = DRIVE_READ_OFFSETS.find(vendor_product); it != DRIVE_READ_OFFSETS.end())
        offset = it->second;

    return offset;
}


export DriveQuery cmd_drive_query(SPTD &sptd)
{
    DriveQuery drive_query;

    INQUIRY_StandardData inquiry_data;
    auto status = cmd_inquiry(sptd, (uint8_t *)&inquiry_data, sizeof(inquiry_data), INQUIRY_VPDPageCode::SUPPORTED_PAGES, false, false);
    if(status.status_code)
        throw_line("unable to query drive info, SCSI ({})", SPTD::StatusMessage(status));

    drive_query.vendor_id = normalize_string(std::string((char *)inquiry_data.vendor_id, sizeof(inquiry_data.vendor_id)));
    drive_query.product_id = normalize_string(std::string((char *)inquiry_data.product_id, sizeof(inquiry_data.product_id)));
    drive_query.product_revision_level = normalize_string(std::string((char *)inquiry_data.product_revision_level, sizeof(inquiry_data.product_revision_level)));
    drive_query.vendor_specific = normalize_string(std::string((char *)inquiry_data.vendor_specific, sizeof(inquiry_data.vendor_specific)));

    return drive_query;
}


export DriveConfig drive_get_config(const DriveQuery &drive_query)
{
    DriveConfig drive_config = DRIVE_CONFIG_GENERIC;

    bool found = false;
    for(auto const &di : KNOWN_DRIVES)
    {
        if((di.vendor_id.empty() || di.vendor_id == drive_query.vendor_id) && (di.product_id.empty() || di.product_id == drive_query.product_id)
            && (di.product_revision_level.empty() || di.product_revision_level == drive_query.product_revision_level))
        {
            drive_config = di;
            found = true;
            break;
        }
    }

    drive_config.vendor_id = drive_query.vendor_id;
    drive_config.product_id = drive_query.product_id;
    drive_config.product_revision_level = drive_query.product_revision_level;
    drive_config.vendor_specific = drive_query.vendor_specific;

    if(!found)
    {
        int32_t database_read_offset = drive_get_generic_read_offset(drive_config.vendor_id, drive_config.product_id);
        if(database_read_offset == std::numeric_limits<int32_t>::max())
            LOG("warning: drive read offset not found in the database");
        else
            drive_config.read_offset = database_read_offset;
    }

    return drive_config;
}


export void drive_override_config(DriveConfig &drive_config, const std::string *type, const int *read_offset, const int *c2_shift, const int *pregap_start, const std::string *read_method,
    const std::string *sector_order)
{
    if(type != nullptr)
        drive_config.type = string_to_enum(*type, TYPE_STRING);

    if(read_offset != nullptr)
        drive_config.read_offset = *read_offset;

    if(c2_shift != nullptr)
        drive_config.c2_shift = *c2_shift;

    if(pregap_start != nullptr)
        drive_config.pregap_start = *pregap_start;

    if(read_method != nullptr)
        drive_config.read_method = string_to_enum(*read_method, READ_METHOD_STRING);

    if(sector_order != nullptr)
        drive_config.sector_order = string_to_enum(*sector_order, SECTOR_ORDER_STRING);
}


export std::string drive_info_string(const DriveConfig &drive_config)
{
    return std::format("{} - {} (revision level: {}, vendor specific: {})", drive_config.vendor_id, drive_config.product_id,
        drive_config.product_revision_level.empty() ? "<empty>" : drive_config.product_revision_level, drive_config.vendor_specific.empty() ? "<empty>" : drive_config.vendor_specific);
}


export std::string drive_config_string(const DriveConfig &drive_config)
{
    return std::format("{} (read offset: {:+}, C2 shift: {}, pre-gap start: {:+}, read method: {}, sector order: {})", enum_to_string(drive_config.type, TYPE_STRING), drive_config.read_offset,
        drive_config.c2_shift, drive_config.pregap_start, enum_to_string(drive_config.read_method, READ_METHOD_STRING), enum_to_string(drive_config.sector_order, SECTOR_ORDER_STRING));
}


export void print_supported_drives()
{
    LOG("");
    LOG("supported drives: ");
    for(auto const &di : KNOWN_DRIVES)
        if(di.type != DriveConfig::Type::GENERIC)
            LOG("{}", drive_info_string(di));
    LOG("");
}


export SectorLayout sector_order_layout(const DriveConfig::SectorOrder &sector_order)
{
    SectorLayout sector_layout;

    switch(sector_order)
    {
    default:
    case DriveConfig::SectorOrder::DATA_C2_SUB:
        sector_layout.data_offset = 0;
        sector_layout.c2_offset = sector_layout.data_offset + CD_DATA_SIZE;
        sector_layout.subcode_offset = sector_layout.c2_offset + CD_C2_SIZE;
        sector_layout.size = sector_layout.subcode_offset + CD_SUBCODE_SIZE;
        break;

    case DriveConfig::SectorOrder::DATA_SUB_C2:
        sector_layout.data_offset = 0;
        sector_layout.subcode_offset = sector_layout.data_offset + CD_DATA_SIZE;
        sector_layout.c2_offset = sector_layout.subcode_offset + CD_SUBCODE_SIZE;
        sector_layout.size = sector_layout.c2_offset + CD_C2_SIZE;
        break;

    case DriveConfig::SectorOrder::DATA_SUB:
        sector_layout.data_offset = 0;
        sector_layout.subcode_offset = sector_layout.data_offset + CD_DATA_SIZE;
        sector_layout.size = sector_layout.subcode_offset + CD_SUBCODE_SIZE;
        sector_layout.c2_offset = CD_RAW_DATA_SIZE;
        break;

    case DriveConfig::SectorOrder::DATA_C2:
        sector_layout.data_offset = 0;
        sector_layout.c2_offset = sector_layout.data_offset + CD_DATA_SIZE;
        sector_layout.size = sector_layout.c2_offset + CD_C2_SIZE;
        sector_layout.subcode_offset = CD_RAW_DATA_SIZE;
        break;
    }

    return sector_layout;
}

}
