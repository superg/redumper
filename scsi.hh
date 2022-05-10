#pragma once



#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <windows.h> //FIXME: remove after GetAddress() removal
#include <ntddscsi.h>
#include <scsi.h>



namespace gpsxre
{

class SPTD
{
public:
    struct Status
    {
        uint8_t status_code;
        uint8_t sense_key;
        uint8_t asc;
        uint8_t ascq;

        static const Status SUCCESS;
        static const Status RESERVED;
    };

    static std::string StatusMessage(const Status &status);

    SPTD(const std::string &drive_path, uint32_t timeout);
    Status SendCommand(const void *cdb, uint8_t cdb_length, void *buffer, uint32_t buffer_length);

    //FIXME: remove later?
    SCSI_ADDRESS GetAddress() { return _address; }

private:
    static std::map<UCHAR, std::string> _SCSISTAT_STRINGS;
    static std::map<UCHAR, std::string> _SCSI_SENSE_STRINGS;
    static std::map<UCHAR, std::string> _SCSI_ADSENSE_STRINGS;

    struct SPTD_SD
    {
        SCSI_PASS_THROUGH_DIRECT sptd;
        SENSE_DATA sd;
    };

    std::shared_ptr<std::remove_pointer<HANDLE>::type> _handle;
    uint32_t _timeout;
    SCSI_ADDRESS _address;
};

}
