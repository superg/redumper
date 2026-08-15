module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

export module filesystem.udf_size;



export namespace gpsxre::udf
{

constexpr std::optional<uint32_t> get_volume_sectors_count(uint32_t partition_end, uint32_t reserve_vds_location, uint32_t reserve_vds_length, uint32_t sector_size)
{
    if(!partition_end || !sector_size)
        return std::nullopt;

    uint64_t reserve_length = ((uint64_t)reserve_vds_length + sector_size - 1) / sector_size;
    uint64_t reserve_end = reserve_length ? (uint64_t)reserve_vds_location + reserve_length : 0;
    uint64_t metadata_end = std::max<uint64_t>(partition_end, reserve_end);

    // Account for the trailing AVDP immediately following the filesystem metadata.
    if(metadata_end >= std::numeric_limits<uint32_t>::max())
        return std::nullopt;

    return metadata_end + 1;
}

}
