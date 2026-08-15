module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

export module filesystem.udf_size;



export namespace gpsxre::udf
{

constexpr uint32_t TRAILING_AVDP_SEARCH_SECTORS = 256;


constexpr std::optional<uint32_t> get_trailing_avdp_lba(uint32_t partition_end, uint32_t reserve_vds_location, uint32_t reserve_vds_length, uint32_t sector_size)
{
    if(!partition_end || !sector_size)
        return std::nullopt;

    uint64_t reserve_length = ((uint64_t)reserve_vds_length + sector_size - 1) / sector_size;
    uint64_t reserve_end = reserve_length ? (uint64_t)reserve_vds_location + reserve_length : 0;
    uint64_t metadata_end = std::max<uint64_t>(partition_end, reserve_end);

    // The sector at metadata_end is the first possible location of the trailing AVDP.
    if(metadata_end >= std::numeric_limits<uint32_t>::max())
        return std::nullopt;

    return metadata_end;
}


constexpr bool is_trailing_avdp_search_lba(uint32_t lba, uint32_t expected_lba)
{
    return lba >= expected_lba && (uint64_t)lba < (uint64_t)expected_lba + TRAILING_AVDP_SEARCH_SECTORS;
}

}
