#pragma once



#include <cstdint>
#include <string>
#include <vector>
#include "cd.hh"
#include "cmd.hh"
#include "scsi.hh"



namespace gpsxre
{

struct DriveInfo
{
    std::string vendor_id;
    std::string product_id;
    std::string product_revision_level;
    std::string vendor_specific;
    int32_t read_offset;
    uint32_t c2_offset;
    int32_t pregap_start;

    enum class SectorOrder
    {
        DATA_C2_SUB,
        DATA_SUB_C2
    } sector_order;

    enum class Type
    {
        GENERIC,
        PLEXTOR,
        LG_ASUS
    } type;
};

inline constexpr uint32_t PLEXTOR_LEADIN_ENTRY_SIZE = sizeof(SPTD::Status) + CD_DATA_SIZE + CD_SUBCODE_SIZE;

DriveInfo drive_get_info(const DriveQuery &drive_query);
int32_t drive_get_generic_read_offset(const std::string &vendor, const std::string &product);
std::string drive_info_string(const DriveInfo &di);
void print_supported_drives();
std::vector<uint8_t> plextor_read_leadin(SPTD &sptd, const DriveInfo &di);
std::vector<uint8_t> asus_cache_read(SPTD &sptd);
std::vector<uint8_t> asus_cache_extract(const std::vector<uint8_t> &cache, int32_t lba_start, uint32_t entries_count);
void asus_cache_print_subq(const std::vector<uint8_t> &cache);

}
