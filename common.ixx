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


export enum class DiscType
{
    CD,
    DVD,
    HDDVD,
    BLURAY,
    BLURAY_R
};


export struct Errors
{
    uint32_t scsi;
    uint32_t c2;
    uint32_t q;
};


export struct Context
{
    DiscType disc_type;
    std::shared_ptr<SPTD> sptd;
    DriveConfig drive_config;

    std::optional<bool> dreamcast;
    std::optional<bool> nintendo;
    std::optional<Errors> dump_errors;
    std::vector<std::pair<int32_t, int32_t>> protection;
    std::optional<bool> protection_trim;
    std::optional<bool> refine;
    std::optional<std::vector<std::string>> dat;
};


export void image_check_overwrite(const Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    std::string state_path(image_prefix + ".state");

    if(!options.overwrite && std::filesystem::exists(state_path))
        throw_line("dump already exists (image name: {})", options.image_name);
}


export void image_check_exists(const Options &options)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();
    std::string state_path(image_prefix + ".state");

    if(!std::filesystem::exists(state_path))
        throw_line("dump doesn't exist (image name: {})", options.image_name);
}

}
