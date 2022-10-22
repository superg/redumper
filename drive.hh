#pragma once



#include <cstdint>
#include <string>
#include <vector>
#include "cd.hh"
#include "cmd.hh"
#include "scsi.hh"



namespace gpsxre
{

struct DriveConfig
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
        LG_ASU8,
        LG_ASU3
    } type;
};

struct SectorLayout
{
	uint32_t data_offset;
	uint32_t c2_offset;
	uint32_t subcode_offset;
	uint32_t size;
};

inline constexpr uint32_t PLEXTOR_LEADIN_ENTRY_SIZE = sizeof(SPTD::Status) + CD_DATA_SIZE + CD_SUBCODE_SIZE;

DriveConfig drive_get_config(const DriveQuery &drive_query);
void drive_override_config(DriveConfig &drive_config, const std::string *type, const int *read_offset, const int *c2_shift, const int *pregap_start, const std::string *read_method, const std::string *sector_order);
int32_t drive_get_generic_read_offset(const std::string &vendor, const std::string &product);
std::string drive_info_string(const DriveConfig &drive_config);
std::string drive_config_string(const DriveConfig &drive_config);
bool drive_is_asus(const DriveConfig &drive_config);
void print_supported_drives();
std::vector<uint8_t> plextor_read_leadin(SPTD &sptd, uint32_t tail_size);
std::vector<uint8_t> asus_cache_read(SPTD &sptd, DriveConfig::Type drive_type);
std::vector<uint8_t> asus_cache_extract(const std::vector<uint8_t> &cache, int32_t lba_start, uint32_t entries_count, DriveConfig::Type drive_type);
void asus_cache_print_subq(const std::vector<uint8_t> &cache, DriveConfig::Type drive_type);
SectorLayout sector_order_layout(const DriveConfig::SectorOrder &sector_order);

}
