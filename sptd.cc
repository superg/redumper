module;
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <ntddscsi.h>
#include <scsi.h>
#endif

export module sptd;

import common;



namespace gpsxre
{

export class SPTD
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

std::map<uint8_t, std::string> SPTD::_SCSISTAT_STRINGS =
{
	{0x00, "GOOD"                        },
	{0x02, "CHECK CONDITION"             },
	{0x04, "CONDITION MET"               },
	{0x08, "BUSY"                        },
	{0x10, "INTERMEDIATE"                },
	{0x14, "INTERMEDIATE - CONDITION MET"},
	{0x18, "RESERVATION CONFLICT"        },
	{0x22, "COMMAND TERMINATED"          },
	{0x28, "TASK SET FULL"               },
	{0x30, "ACA ACTIVE"                  },
	{0x40, "TASK ABORTED"                }
};


std::map<uint8_t, std::string> SPTD::_SCSI_SENSE_STRINGS =
{
	{0x00, "NO SENSE"       },
	{0x01, "RECOVERED ERROR"},
	{0x02, "NOT READY"      },
	{0x03, "MEDIUM ERROR"   },
	{0x04, "HARDWARE ERROR" },
	{0x05, "ILLEGAL REQUEST"},
	{0x06, "UNIT ATTENTION" },
	{0x07, "DATA PROTECT"   },
	{0x08, "BLANK CHECK"    },
	{0x09, "VENDOR SPECIFIC"},
	{0x0A, "COPY ABORTED"   },
	{0x0B, "ABORTED COMMAND"},
	{0x0C, "EQUAL"          },
	{0x0D, "VOLUME OVERFLOW"},
	{0x0E, "MISCOMPARE"     },
	{0x0F, "COMPLETED"      }
};


std::map<uint8_t, std::string> SPTD::_SCSI_ADSENSE_STRINGS =
{
	{0x00, "NO ADDITIONAL SENSE INFORMATION"                              },
	{0x01, "NO INDEX/SECTOR SIGNAL"                                       },
	{0x02, "NO SEEK COMPLETE"                                             },
	{0x03, "PERIPHERAL DEVICE WRITE FAULT"                                },
	{0x04, "LOGICAL UNIT NOT READY, CAUSE NOT REPORTABLE"                 },
	{0x05, "LOGICAL UNIT DOES NOT RESPOND TO SELECTION"                   },
	{0x06, "NO REFERENCE POSITION FOUND"                                  },
	{0x07, "MULTIPLE PERIPHERAL DEVICES SELECTED"                         },
	{0x08, "LOGICAL UNIT COMMUNICATION FAILURE"                           },
	{0x09, "TRACK FOLLOWING ERROR"                                        },
	{0x0A, "ERROR LOG OVERFLOW"                                           },
	{0x0B, "WARNING"                                                      },
	{0x0C, "WRITE ERROR"                                                  },
	{0x0D, "ERROR DETECTED BY THIRD PARTY TEMPORARY INITIATOR"            },
	{0x0E, "INVALID INFORMATION UNIT"                                     },
	{0x10, "ID CRC OR ECC ERROR"                                          },
	{0x11, "UNRECOVERED READ ERROR"                                       },
	{0x12, "ADDRESS MARK NOT FOUND FOR ID FIELD"                          },
	{0x13, "ADDRESS MARK NOT FOUND FOR DATA FIELD"                        },
	{0x14, "RECORDED ENTITY NOT FOUND"                                    },
	{0x15, "RANDOM POSITIONING ERROR"                                     },
	{0x16, "DATA SYNCHRONIZATION MARK ERROR"                              },
	{0x17, "RECOVERED DATA WITH NO ERROR CORRECTION APPLIED"              },
	{0x18, "RECOVERED DATA WITH ERROR CORRECTION APPLIED"                 },
	{0x19, "DEFECT LIST ERROR"                                            },
	{0x1A, "PARAMETER LIST LENGTH ERROR"                                  },
	{0x1B, "SYNCHRONOUS DATA TRANSFER ERROR"                              },
	{0x1C, "DEFECT LIST NOT FOUND"                                        },
	{0x1D, "MISCOMPARE DURING VERIFY OPERATION"                           },
	{0x1E, "RECOVERED ID WITH ECC CORRECTION"                             },
	{0x1F, "PARTIAL DEFECT LIST TRANSFER"                                 },
	{0x20, "INVALID COMMAND OPERATION CODE"                               },
	{0x21, "LOGICAL BLOCK ADDRESS OUT OF RANGE"                           },
	{0x22, "ILLEGAL FUNCTION (USE 20 00, 24 00, OR 26 00)"                },
	{0x23, "INVALID TOKEN OPERATION, CAUSE NOT REPORTABLE"                },
	{0x24, "INVALID FIELD IN CDB"                                         },
	{0x25, "LOGICAL UNIT NOT SUPPORTED"                                   },
	{0x26, "INVALID FIELD IN PARAMETER LIST"                              },
	{0x27, "WRITE PROTECTED"                                              },
	{0x28, "NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED"           },
	{0x29, "POWER ON, RESET, OR BUS DEVICE RESET OCCURRED"                },
	{0x2A, "PARAMETERS CHANGED"                                           },
	{0x2B, "COPY CANNOT EXECUTE SINCE HOST CANNOT DISCONNECT"             },
	{0x2C, "COMMAND SEQUENCE ERROR"                                       },
	{0x2D, "OVERWRITE ERROR ON UPDATE IN PLACE"                           },
	{0x2E, "INSUFFICIENT TIME FOR OPERATION"                              },
	{0x2F, "COMMANDS CLEARED BY ANOTHER INITIATOR"                        },
	{0x30, "INCOMPATIBLE MEDIUM INSTALLED"                                },
	{0x31, "MEDIUM FORMAT CORRUPTED"                                      },
	{0x32, "NO DEFECT SPARE LOCATION AVAILABLE"                           },
	{0x33, "TAPE LENGTH ERROR"                                            },
	{0x34, "ENCLOSURE FAILURE"                                            },
	{0x35, "ENCLOSURE SERVICES FAILURE"                                   },
	{0x36, "RIBBON, INK, OR TONER FAILURE"                                },
	{0x37, "ROUNDED PARAMETER"                                            },
	{0x38, "EVENT STATUS NOTIFICATION"                                    },
	{0x39, "SAVING PARAMETERS NOT SUPPORTED"                              },
	{0x3A, "MEDIUM NOT PRESENT"                                           },
	{0x3B, "SEQUENTIAL POSITIONING ERROR"                                 },
	{0x3D, "INVALID BITS IN IDENTIFY MESSAGE"                             },
	{0x3E, "LOGICAL UNIT HAS NOT SELF-CONFIGURED YET"                     },
	{0x3F, "TARGET OPERATING CONDITIONS HAVE CHANGED"                     },
	{0x40, "RAM FAILURE (SHOULD USE 40 NN)"                               },
	{0x41, "DATA PATH FAILURE (SHOULD USE 40 NN)"                         },
	{0x42, "POWER-ON OR SELF-TEST FAILURE (SHOULD USE 40 NN)"             },
	{0x43, "MESSAGE ERROR"                                                },
	{0x44, "INTERNAL TARGET FAILURE"                                      },
	{0x45, "SELECT OR RESELECT FAILURE"                                   },
	{0x46, "UNSUCCESSFUL SOFT RESET"                                      },
	{0x47, "SCSI PARITY ERROR"                                            },
	{0x48, "INITIATOR DETECTED ERROR MESSAGE RECEIVED"                    },
	{0x49, "INVALID MESSAGE ERROR"                                        },
	{0x4A, "COMMAND PHASE ERROR"                                          },
	{0x4B, "DATA PHASE ERROR"                                             },
	{0x4C, "LOGICAL UNIT FAILED SELF-CONFIGURATION"                       },
	{0x4E, "OVERLAPPED COMMANDS ATTEMPTED"                                },
	{0x50, "WRITE APPEND ERROR"                                           },
	{0x51, "ERASE FAILURE"                                                },
	{0x52, "CARTRIDGE FAULT"                                              },
	{0x53, "MEDIA LOAD OR EJECT FAILED"                                   },
	{0x54, "SCSI TO HOST SYSTEM INTERFACE FAILURE"                        },
	{0x55, "SYSTEM RESOURCE FAILURE"                                      },
	{0x57, "UNABLE TO RECOVER TABLE-OF-CONTENTS"                          },
	{0x58, "GENERATION DOES NOT EXIST"                                    },
	{0x59, "UPDATED BLOCK READ"                                           },
	{0x5A, "OPERATOR REQUEST OR STATE CHANGE INPUT"                       },
	{0x5B, "LOG EXCEPTION"                                                },
	{0x5C, "RPL STATUS CHANGE"                                            },
	{0x5D, "FAILURE PREDICTION THRESHOLD EXCEEDED"                        },
	{0x5E, "LOW POWER CONDITION ON"                                       },
	{0x60, "LAMP FAILURE"                                                 },
	{0x61, "VIDEO ACQUISITION ERROR"                                      },
	{0x62, "SCAN HEAD POSITIONING ERROR"                                  },
	{0x63, "END OF USER AREA ENCOUNTERED ON THIS TRACK"                   },
	{0x64, "ILLEGAL MODE FOR THIS TRACK"                                  },
	{0x65, "VOLTAGE FAULT"                                                },
	{0x66, "AUTOMATIC DOCUMENT FEEDER COVER UP"                           },
	{0x67, "CONFIGURATION FAILURE"                                        },
	{0x68, "LOGICAL UNIT NOT CONFIGURED"                                  },
	{0x69, "DATA LOSS ON LOGICAL UNIT"                                    },
	{0x6A, "INFORMATIONAL, REFER TO LOG"                                  },
	{0x6B, "STATE CHANGE HAS OCCURRED"                                    },
	{0x6C, "REBUILD FAILURE OCCURRED"                                     },
	{0x6D, "RECALCULATE FAILURE OCCURRED"                                 },
	{0x6E, "COMMAND TO LOGICAL UNIT FAILED"                               },
	{0x6F, "COPY PROTECTION KEY EXCHANGE FAILURE - AUTHENTICATION FAILURE"},
	{0x71, "DECOMPRESSION EXCEPTION LONG ALGORITHM ID"                    },
	{0x72, "SESSION FIXATION ERROR"                                       },
	{0x73, "CD CONTROL ERROR"                                             },
	{0x74, "SECURITY ERROR"                                               }
};
// TODO: implement full ASCQ messages: https://www.t10.org/lists/asc-num.htm


std::set<std::string> SPTD::ListDrives()
{
	std::set<std::string> drives;

#ifdef _WIN32
	DWORD drive_mask = GetLogicalDrives();
	if(!drive_mask)
		throw_line(std::format("SYSTEM ({})", GetLastError()));

	for(uint32_t i = 0, n = sizeof(drive_mask) * CHAR_BIT; i < n; ++i)
	{
		if(drive_mask & 1 << i)
		{
			std::string drive(std::format("{}:", (char)('A' + i)));
			if(GetDriveType(std::format("{}\\", drive).c_str()) == DRIVE_CDROM)
				drives.emplace(drive);
			;
		}
	}
#else
	// detect available drives using sysfs
	// according to sysfs kernel rules, it's planned to merge all 3 classification directories
	// into "subsystem" so this directory is scanned first
	for(auto &ss : {"subsystem", "bus", "class", "block"})
	{
		std::filesystem::path devices_path(std::format("/sys/{}/scsi/devices", ss));
		if(std::filesystem::is_directory(devices_path))
		{
			for(auto const &de : std::filesystem::directory_iterator(devices_path))
			{
				if(!std::filesystem::is_directory(de.path()))
					continue;

				std::filesystem::path type_path(de.path() / "type");
				if(!std::filesystem::exists(type_path))
					continue;

				std::ifstream ifs(type_path);
				if(!ifs.is_open())
					continue;

				unsigned int type = 0;
				ifs >> type;
				if(!ifs)
					continue;

				if(type != 5)
					continue;

				// direct read-write ioctl requires a generic SCSI device
				std::filesystem::path scsi_generic_path(de.path() / "scsi_generic");
				if(std::filesystem::is_directory(scsi_generic_path))
				{
					auto it = std::filesystem::directory_iterator(scsi_generic_path);
					if(it != std::filesystem::directory_iterator() && std::filesystem::is_directory(it->path()))
						drives.emplace(std::format("/dev/{}", it->path().filename().string()));
				}
			}

			break;
		}
	}
#endif

	return drives;
}


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


SPTD::SPTD(const std::string &drive_path)
{
#ifdef _WIN32
	_handle = CreateFile(std::format("//./{}", drive_path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if(_handle == INVALID_HANDLE_VALUE)
#else
	_handle = open(drive_path.c_str(), O_RDWR | O_NONBLOCK | O_EXCL);
	if(_handle < 0)
#endif
		throw_line(std::format("unable to open drive ({}, SYSTEM: {})", drive_path, GetLastError()));
}


SPTD::~SPTD()
{
	bool success;
#ifdef _WIN32
	success = CloseHandle(_handle) == TRUE;
#else
	success = close(_handle) == 0;
#endif
	if(!success)
		std::cout << std::format("warning: unable to close drive (SYSTEM: {})", GetLastError()) << std::endl;
}


SPTD::Status SPTD::SendCommand(const void *cdb, uint8_t cdb_length, void *buffer, uint32_t buffer_length, uint32_t timeout)
{
	Status status = {};

#ifdef _WIN32
	//FIXME: simplify and reuse common SenseData
	SPTD_SD sptd_sd = {};
	sptd_sd.sptd.Length = sizeof(sptd_sd.sptd);
	sptd_sd.sptd.CdbLength = cdb_length;
	sptd_sd.sptd.SenseInfoLength = sizeof(sptd_sd.sd);
	sptd_sd.sptd.DataIn = SCSI_IOCTL_DATA_IN;
	sptd_sd.sptd.DataTransferLength = buffer_length;
	sptd_sd.sptd.TimeOutValue = timeout;
	sptd_sd.sptd.DataBuffer = buffer;
	sptd_sd.sptd.SenseInfoOffset = offsetof(SPTD_SD, sd);
	memcpy(sptd_sd.sptd.Cdb, cdb, cdb_length);

	DWORD bytes_returned;
	BOOL success = DeviceIoControl(_handle, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd_sd, sizeof(sptd_sd), &sptd_sd, sizeof(sptd_sd), &bytes_returned, nullptr);
	if(success != TRUE)
		throw_line(std::format("SYSTEM ({})", GetLastError()));

	if(sptd_sd.sptd.ScsiStatus != SCSISTAT_GOOD)
	{
		status.status_code = sptd_sd.sptd.ScsiStatus;
		status.sense_key = sptd_sd.sd.SenseKey;
		status.asc = sptd_sd.sd.AdditionalSenseCode;
		status.ascq = sptd_sd.sd.AdditionalSenseCodeQualifier;
	}
#else
	SenseData sense_data;

	sg_io_hdr hdr = {};
	hdr.interface_id = 'S';
	hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	hdr.cmd_len = cdb_length;
	hdr.mx_sb_len = sizeof(sense_data);
	hdr.dxfer_len = buffer_length;
	hdr.dxferp = buffer;
	hdr.cmdp = (unsigned char *)cdb;
	hdr.sbp = (unsigned char *)&sense_data;
	hdr.timeout = timeout;

	int result = ioctl(_handle, SG_IO, &hdr);
	if(result < 0)
		throw_line(std::format("SYSTEM ({})", GetLastError()));

	if(hdr.status)
	{
		status.status_code = hdr.status;
		status.sense_key = sense_data.sense_key;
		status.asc = sense_data.additional_sense_code;
		status.ascq = sense_data.additional_sense_code_qualifier;
	}
#endif

	return status;
}


std::string SPTD::GetLastError()
{
	std::string message;

#ifdef _WIN32
	LPSTR buffer = nullptr;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
				  nullptr, ::GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, nullptr);

	message = std::string(buffer);
	message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
	message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());

	LocalFree(buffer);
#else
	message = strerror(errno);
#endif

	return message;
}

}
