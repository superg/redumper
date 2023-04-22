#pragma once



#include <cstdint>
#include <string>
#include <vector>
#include "mmc.hh"
#include "scsi.hh"



namespace gpsxre
{

struct DriveQuery
{
	std::string vendor_id;
	std::string product_id;
	std::string product_revision_level;
	std::string vendor_specific;
};


SPTD::Status cmd_drive_ready(SPTD &sptd);
SPTD::Status cmd_inquiry(SPTD &sptd, uint8_t *data, uint32_t data_size, INQUIRY_VPDPageCode page_code, bool command_support_data, bool enable_vital_product_data);
DriveQuery cmd_drive_query(SPTD &sptd);
uint32_t cmd_inquiry_vpd_block_limits_optimal_transfer_length(SPTD &sptd);
std::vector<uint8_t> cmd_read_toc(SPTD &sptd);
std::vector<uint8_t> cmd_read_full_toc(SPTD &sptd);
SPTD::Status cmd_read_cd_text(SPTD &sptd, std::vector<uint8_t> &cd_text);
SPTD::Status cmd_read_dvd_structure(SPTD &sptd, std::vector<uint8_t> &response_data, uint32_t address, uint8_t layer_number, READ_DVD_STRUCTURE_Format format, uint8_t agid);
SPTD::Status cmd_read(SPTD &sptd, uint8_t *buffer, uint32_t block_size, int32_t start_lba, uint32_t transfer_length, bool force_unit_access);
SPTD::Status cmd_read_cd(SPTD &sptd, uint8_t *sector, int32_t start_lba, uint32_t transfer_length, READ_CD_ExpectedSectorType expected_sector_type, READ_CD_ErrorField error_field, READ_CD_SubChannel sub_channel);
SPTD::Status cmd_read_cdda(SPTD &sptd, uint8_t *sector, int32_t start_lba, uint32_t transfer_length, READ_CDDA_SubCode sub_code);
SPTD::Status cmd_plextor_reset(SPTD &sptd);
SPTD::Status cmd_synchronize_cache(SPTD &sptd);
SPTD::Status cmd_flush_drive_cache(SPTD &sptd, int32_t lba);
SPTD::Status cmd_set_cd_speed(SPTD &sptd, uint16_t speed);
SPTD::Status cmd_asus_read_cache(SPTD &sptd, uint8_t *buffer, uint32_t offset, uint32_t size);
SPTD::Status cmd_get_configuration(SPTD &sptd);
SPTD::Status cmd_get_configuration_current_profile(SPTD &sptd, GET_CONFIGURATION_FeatureCode_ProfileList &current_profile);

}
