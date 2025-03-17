module;
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "throw_line.hh"

export module common;

import drive;
import options;
import scsi.mmc;
import scsi.sptd;



namespace gpsxre
{

export enum class State : uint8_t
{
    ERROR_SKIP, // must be first to support random offset file writes
    ERROR_C2,
    SUCCESS_C2_OFF,
    SUCCESS_SCSI_OFF,
    SUCCESS
};


export enum class DumpMode
{
    DUMP,
    VERIFY,
    REFINE
};


export struct Errors
{
    uint32_t scsi;
    uint32_t c2;
    uint32_t q;
};


export struct Context
{
    GET_CONFIGURATION_FeatureCode_ProfileList current_profile;
    std::shared_ptr<SPTD> sptd;
    DriveConfig drive_config;

    std::optional<std::vector<std::pair<int32_t, int32_t>>> rings;
    std::optional<Errors> dump_errors;
    std::vector<std::pair<int32_t, int32_t>> protection_hard;
    std::vector<std::pair<int32_t, int32_t>> protection_soft;
    std::optional<bool> protection_trim;
    std::optional<bool> refine;
    std::optional<std::vector<std::string>> dat;
};


export bool profile_is_cd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_R || profile == GET_CONFIGURATION_FeatureCode_ProfileList::CD_RW;
}


export bool profile_is_dvd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RAM
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW_RO || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_RW
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_R_DL_LJR
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_RW_DL || profile == GET_CONFIGURATION_FeatureCode_ProfileList::DVD_PLUS_R_DL;
}


export bool profile_is_bluray(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_R || profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_R_RRM
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_RW;
}


export bool profile_is_hddvd(GET_CONFIGURATION_FeatureCode_ProfileList profile)
{
    return profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_ROM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RAM || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW
        || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_R_DL || profile == GET_CONFIGURATION_FeatureCode_ProfileList::HDDVD_RW_DL;
}


export void image_check_overwrite(const Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    std::string state_path(image_prefix + ".state");

    if(!options.overwrite && std::filesystem::exists(state_path))
        throw_line("dump already exists (image name: {})", options.image_name);
}

}
