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
DriveQuery cmd_drive_query(SPTD &sptd);
std::vector<uint8_t> cmd_read_toc(SPTD &sptd);
std::vector<uint8_t> cmd_read_full_toc(SPTD &sptd);
SPTD::Status cmd_read_cd_text(SPTD &sptd, std::vector<uint8_t> &cd_text);
SPTD::Status cmd_read_cd(SPTD &sptd, std::vector<uint8_t> &sector_buffer, int32_t start_lba, uint32_t transfer_length, READ_CD_ExpectedSectorType expected_sector_type, READ_CD_SubChannel sub_channel);
SPTD::Status cmd_read_cdda(SPTD &sptd, std::vector<uint8_t> &sector_buffer, int32_t start_lba, uint32_t transfer_length, READ_CDDA_SubCode sub_code);
SPTD::Status cmd_plextor_reset(SPTD &sptd);
SPTD::Status cmd_synchronize_cache(SPTD &sptd);
SPTD::Status cmd_flush_drive_cache(SPTD &sptd, int32_t lba);
SPTD::Status cmd_set_cd_speed(SPTD &sptd, uint16_t speed);
SPTD::Status cmd_asus_read_cache(SPTD &sptd, uint8_t *buffer, uint32_t offset, uint32_t size);
SPTD::Status cmd_get_configuration(SPTD &sptd);

}
