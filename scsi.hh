#pragma once



#include <cstdint>
#include <map>
#include <set>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <ntddscsi.h>
#include <scsi.h>
#endif



namespace gpsxre
{

class SPTD
{
public:
	static constexpr uint32_t DEFAULT_TIMEOUT = 50000;

	struct Status
	{
		uint8_t status_code;
		uint8_t sense_key;
		uint8_t asc;
		uint8_t ascq;
	};

	static std::set<std::string> ListDrives();
	static std::string StatusMessage(const Status &status);

	SPTD(const std::string &drive_path);
	~SPTD();

	Status SendCommand(const void *cdb, uint8_t cdb_length, void *buffer, uint32_t buffer_length, uint32_t timeout = DEFAULT_TIMEOUT);

private:
	struct SenseData
	{
		uint8_t error_code       :7;
		uint8_t valid            :1;
		uint8_t segment_number;
		uint8_t sense_key        :4;
		uint8_t reserved         :1;
		uint8_t incorrect_length :1;
		uint8_t end_of_media     :1;
		uint8_t file_mark        :1;
		uint8_t information[4];
		uint8_t additional_sense_length;
		uint8_t command_specific_information[4];
		uint8_t additional_sense_code;
		uint8_t additional_sense_code_qualifier;
		uint8_t field_replaceable_unit_code;
		uint8_t sense_key_specific[3];
		uint8_t additional_sense_bytes[0];
	};

	static std::map<uint8_t, std::string> _SCSISTAT_STRINGS;
	static std::map<uint8_t, std::string> _SCSI_SENSE_STRINGS;
	static std::map<uint8_t, std::string> _SCSI_ADSENSE_STRINGS;

#ifdef _WIN32
	struct SPTD_SD
	{
		SCSI_PASS_THROUGH_DIRECT sptd;
		SENSE_DATA sd;
	};

	HANDLE _handle;
#else
	int _handle;
#endif

	static std::string GetLastError();
};

}
