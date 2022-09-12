#include <format>
#include <map>
#include <stdexcept>

#include "common.hh"
#include "logger.hh"
#include "windows.hh"
#include "scsi.hh"



namespace gpsxre
{

const SPTD::Status SPTD::Status::SUCCESS  = {0x00, 0x00, 0x00, 0x00};
const SPTD::Status SPTD::Status::RESERVED = {0xFF, 0xFF, 0xFF, 0xFF};


std::map<UCHAR, std::string> SPTD::_SCSISTAT_STRINGS =
{
	{SCSISTAT_GOOD                 , "GOOD"                 },
	{SCSISTAT_CHECK_CONDITION      , "CHECK_CONDITION"      },
	{SCSISTAT_CONDITION_MET        , "CONDITION_MET"        },
	{SCSISTAT_BUSY                 , "BUSY"                 },
	{SCSISTAT_INTERMEDIATE         , "INTERMEDIATE"         },
	{SCSISTAT_INTERMEDIATE_COND_MET, "INTERMEDIATE_COND_MET"},
	{SCSISTAT_RESERVATION_CONFLICT , "RESERVATION_CONFLICT" },
	{SCSISTAT_COMMAND_TERMINATED   , "COMMAND_TERMINATED"   },
	{SCSISTAT_QUEUE_FULL           , "QUEUE_FULL"           }
};


std::map<UCHAR, std::string> SPTD::_SCSI_SENSE_STRINGS =
{
	{SCSI_SENSE_NO_SENSE       , "NO_SENSE"       },
	{SCSI_SENSE_RECOVERED_ERROR, "RECOVERED_ERROR"},
	{SCSI_SENSE_NOT_READY      , "NOT_READY"      },
	{SCSI_SENSE_MEDIUM_ERROR   , "MEDIUM_ERROR"   },
	{SCSI_SENSE_HARDWARE_ERROR , "HARDWARE_ERROR" },
	{SCSI_SENSE_ILLEGAL_REQUEST, "ILLEGAL_REQUEST"},
	{SCSI_SENSE_UNIT_ATTENTION , "UNIT_ATTENTION" },
	{SCSI_SENSE_DATA_PROTECT   , "DATA_PROTECT"   },
	{SCSI_SENSE_BLANK_CHECK    , "BLANK_CHECK"    },
	{SCSI_SENSE_UNIQUE         , "UNIQUE"         },
	{SCSI_SENSE_COPY_ABORTED   , "COPY_ABORTED"   },
	{SCSI_SENSE_ABORTED_COMMAND, "ABORTED_COMMAND"},
	{SCSI_SENSE_EQUAL          , "EQUAL"          },
	{SCSI_SENSE_VOL_OVERFLOW   , "VOL_OVERFLOW"   },
	{SCSI_SENSE_MISCOMPARE     , "MISCOMPARE"     },
	{SCSI_SENSE_RESERVED       , "RESERVED"       }
};


std::map<UCHAR, std::string> SPTD::_SCSI_ADSENSE_STRINGS =
{
	{SCSI_ADSENSE_NO_SENSE                             , "NO_SENSE"                             },
	{SCSI_ADSENSE_NO_SEEK_COMPLETE                     , "NO_SEEK_COMPLETE"                     },
	{SCSI_ADSENSE_WRITE                                , "WRITE"                                },
	{SCSI_ADSENSE_LUN_NOT_READY                        , "LUN_NOT_READY"                        },
	{SCSI_ADSENSE_LUN_COMMUNICATION                    , "LUN_COMMUNICATION"                    },
	{SCSI_ADSENSE_SERVO_ERROR                          , "SERVO_ERROR"                          },
	{SCSI_ADSENSE_WARNING                              , "WARNING"                              },
	{SCSI_ADSENSE_WRITE_ERROR                          , "WRITE_ERROR"                          },
	{SCSI_ADSENSE_COPY_TARGET_DEVICE_ERROR             , "COPY_TARGET_DEVICE_ERROR"             },
	{SCSI_ADSENSE_UNRECOVERED_ERROR                    , "UNRECOVERED_ERROR"                    },
	{SCSI_ADSENSE_TRACK_ERROR                          , "TRACK_ERROR"                          },
	{SCSI_ADSENSE_SEEK_ERROR                           , "SEEK_ERROR"                           },
	{SCSI_ADSENSE_REC_DATA_NOECC                       , "REC_DATA_NOECC"                       },
	{SCSI_ADSENSE_REC_DATA_ECC                         , "REC_DATA_ECC"                         },
	{SCSI_ADSENSE_DEFECT_LIST_ERROR                    , "DEFECT_LIST_ERROR"                    },
	{SCSI_ADSENSE_PARAMETER_LIST_LENGTH                , "PARAMETER_LIST_LENGTH"                },
	{SCSI_ADSENSE_MISCOMPARE_DURING_VERIFY_OPERATION   , "MISCOMPARE_DURING_VERIFY_OPERATION"   },
	{SCSI_ADSENSE_ILLEGAL_COMMAND                      , "ILLEGAL_COMMAND"                      },
	{SCSI_ADSENSE_ACCESS_DENIED                        , "ACCESS_DENIED"                        },
	{SCSI_ADSENSE_ILLEGAL_BLOCK                        , "ILLEGAL_BLOCK"                        },
	{SCSI_ADSENSE_INVALID_TOKEN                        , "INVALID_TOKEN"                        },
	{SCSI_ADSENSE_INVALID_CDB                          , "INVALID_CDB"                          },
	{SCSI_ADSENSE_INVALID_LUN                          , "INVALID_LUN"                          },
	{SCSI_ADSENSE_INVALID_FIELD_PARAMETER_LIST         , "INVALID_FIELD_PARAMETER_LIST"         },
	{SCSI_ADSENSE_WRITE_PROTECT                        , "WRITE_PROTECT"                        },
	{SCSI_ADSENSE_MEDIUM_CHANGED                       , "MEDIUM_CHANGED"                       },
	{SCSI_ADSENSE_BUS_RESET                            , "BUS_RESET"                            },
	{SCSI_ADSENSE_PARAMETERS_CHANGED                   , "PARAMETERS_CHANGED"                   },
	{SCSI_ADSENSE_INSUFFICIENT_TIME_FOR_OPERATION      , "INSUFFICIENT_TIME_FOR_OPERATION"      },
	{SCSI_ADSENSE_INVALID_MEDIA                        , "INVALID_MEDIA"                        },
	{SCSI_ADSENSE_DEFECT_LIST                          , "DEFECT_LIST"                          },
	{SCSI_ADSENSE_LB_PROVISIONING                      , "LB_PROVISIONING"                      },
	{SCSI_ADSENSE_NO_MEDIA_IN_DEVICE                   , "NO_MEDIA_IN_DEVICE"                   },
	{SCSI_ADSENSE_POSITION_ERROR                       , "POSITION_ERROR"                       },
	{SCSI_ADSENSE_LOGICAL_UNIT_ERROR                   , "LOGICAL_UNIT_ERROR"                   },
	{SCSI_ADSENSE_OPERATING_CONDITIONS_CHANGED         , "OPERATING_CONDITIONS_CHANGED"         },
	{SCSI_ADSENSE_DATA_PATH_FAILURE                    , "DATA_PATH_FAILURE"                    },
	{SCSI_ADSENSE_POWER_ON_SELF_TEST_FAILURE           , "POWER_ON_SELF_TEST_FAILURE"           },
	{SCSI_ADSENSE_INTERNAL_TARGET_FAILURE              , "INTERNAL_TARGET_FAILURE"              },
	{SCSI_ADSENSE_DATA_TRANSFER_ERROR                  , "DATA_TRANSFER_ERROR"                  },
	{SCSI_ADSENSE_LUN_FAILED_SELF_CONFIGURATION        , "LUN_FAILED_SELF_CONFIGURATION"        },
	{SCSI_ADSENSE_RESOURCE_FAILURE                     , "RESOURCE_FAILURE"                     },
	{SCSI_ADSENSE_OPERATOR_REQUEST                     , "OPERATOR_REQUEST"                     },
	{SCSI_ADSENSE_FAILURE_PREDICTION_THRESHOLD_EXCEEDED, "FAILURE_PREDICTION_THRESHOLD_EXCEEDED"},
	{SCSI_ADSENSE_ILLEGAL_MODE_FOR_THIS_TRACK          , "ILLEGAL_MODE_FOR_THIS_TRACK"          },
	{SCSI_ADSENSE_COPY_PROTECTION_FAILURE              , "COPY_PROTECTION_FAILURE"              },
	{SCSI_ADSENSE_POWER_CALIBRATION_ERROR              , "POWER_CALIBRATION_ERROR"              },
	{SCSI_ADSENSE_VENDOR_UNIQUE                        , "VENDOR_UNIQUE"                        },
	{SCSI_ADSENSE_MUSIC_AREA                           , "MUSIC_AREA"                           },
	{SCSI_ADSENSE_DATA_AREA                            , "DATA_AREA"                            },
	{SCSI_ADSENSE_VOLUME_OVERFLOW                      , "VOLUME_OVERFLOW"                      }
};


std::string SPTD::StatusMessage(const Status &status)
{
	std::string status_message;

	{
		auto it = _SCSISTAT_STRINGS.find(status.status_code);
		status_message += "SC: " + (it == _SCSISTAT_STRINGS.end() ? std::format("{:02X}", status.status_code) : it->second);
	}

	if(auto it = _SCSI_SENSE_STRINGS.find(status.sense_key); it != _SCSI_SENSE_STRINGS.end() && it->first || it == _SCSI_SENSE_STRINGS.end())
		status_message += ", SK: " + (it == _SCSI_SENSE_STRINGS.end() ? std::format("{:02X}", status.sense_key) : it->second);

	if(auto it = _SCSI_ADSENSE_STRINGS.find(status.asc); it != _SCSI_ADSENSE_STRINGS.end() && it->first || it == _SCSI_ADSENSE_STRINGS.end())
		status_message += ", ASC: " + (it == _SCSI_ADSENSE_STRINGS.end() ? std::format("{:02X}", status.asc) : it->second);

	if(status.ascq)
		status_message += ", ASCQ: " + std::format("{:02X}", status.ascq);

	return status_message;
}


SPTD::SPTD(const std::string &drive_path, uint32_t timeout)
	: _handle(CreateFile(std::format("//./{}", drive_path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr), &::CloseHandle)
	, _timeout(timeout)
{
	if(_handle.get() == INVALID_HANDLE_VALUE)
		throw_line(std::format("unable to open drive ({})", drive_path));

	// make sure the address is initialized if subsequent call fails
	memset(&_address, 0, sizeof(_address));

	// the call fails on Windows 7 32-bit
	// according to Microsoft, this request is not relevant to storage class drivers that support
	// Plug and Play because the port driver supplies the address information on behalf of the class drive
	DWORD bytes_returned;
	BOOL success = DeviceIoControl(_handle.get(), IOCTL_SCSI_GET_ADDRESS, &_address, sizeof(_address), &_address, sizeof(_address), &bytes_returned, nullptr);
	if(success != TRUE)
		LOG("warning: DeviceIoControl IOCTL_SCSI_GET_ADDRESS call failed ({})", get_last_error());
}


SPTD::Status SPTD::SendCommand(const void *cdb, uint8_t cdb_length, void *buffer, uint32_t buffer_length)
{
	Status status = {};

	SPTD_SD sptd_sd;
	sptd_sd.sptd.Length = sizeof(sptd_sd.sptd);
	sptd_sd.sptd.PathId = _address.PathId;
	sptd_sd.sptd.TargetId = _address.TargetId;
	sptd_sd.sptd.Lun = _address.Lun;
	sptd_sd.sptd.CdbLength = cdb_length;
	sptd_sd.sptd.SenseInfoLength = sizeof(sptd_sd.sd);
	sptd_sd.sptd.DataIn = SCSI_IOCTL_DATA_IN;
	sptd_sd.sptd.DataTransferLength = buffer_length;
	sptd_sd.sptd.TimeOutValue = _timeout;
	sptd_sd.sptd.DataBuffer = buffer;
	sptd_sd.sptd.SenseInfoOffset = offsetof(SPTD_SD, sd);
	memcpy(sptd_sd.sptd.Cdb, cdb, cdb_length);

	DWORD bytes_returned;
	BOOL success = DeviceIoControl(_handle.get(), IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd_sd, sizeof(sptd_sd), &sptd_sd, sizeof(sptd_sd), &bytes_returned, nullptr);
	if(success != TRUE)
		throw_line(std::format("WIN32 ({})", get_last_error()));

	if(sptd_sd.sptd.ScsiStatus != SCSISTAT_GOOD)
	{
		status.status_code = sptd_sd.sptd.ScsiStatus;
		status.sense_key = sptd_sd.sd.SenseKey;
		status.asc = sptd_sd.sd.AdditionalSenseCode;
		status.ascq = sptd_sd.sd.AdditionalSenseCodeQualifier;
	}

	return status;
}

}
