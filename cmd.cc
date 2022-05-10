#include <windows.h>
#include <scsi.h>
#include <ntddcdrm.h>
#include "cd.hh"
#include "common.hh"
#include "endian.hh"
#include "cmd.hh"



namespace gpsxre
{

enum class ReadCDExpectedSectorType : uint8_t
{
	ALL_TYPES = 0,
	CDDA = 1,
	MODE1 = 2,
	MODE2 = 3,
	MODE2_FORM1 = 4,
	MODE2_FORM2 = 5,
	RESERVED6 = 6,
	RESERVED7 = 7
};


enum class ReadCDHeaderCode : uint8_t
{
	NONE = 0,
	HEADER_ONLY = 1,
	SUBHEADER_ONLY = 2,
	ALL_HEADERS = 3
};


enum class ReadCDErrorFlags : uint8_t
{
	NONE = 0,
	C2 = 1,
	C2_BEB = 2,
	RESERVED3 = 3
};


enum class ReadCDSubChannelSelection : uint8_t
{
	NONE = 0,
	RAW = 1,
	Q = 2,
	RESERVED3 = 3,
	PW = 4,
	RESERVED5 = 5,
	RESERVED6 = 6,
	RESERVED7 = 7
};


enum class ReadCDDASubCode : uint8_t
{
	DATA = 0,
	DATA_WITH_SUBQ = 1,
	DATA_WITH_SUB_ALL = 2,
	SUB_ALL = 3,
	DATA_WITH_C2_WITH_SUB_ALL = 8
};


static const uint32_t READ_CDDA_SIZES[] =
{
	CD_DATA_SIZE,
	CD_DATA_SIZE + 16,
	CD_DATA_SIZE + CD_SUBCODE_SIZE,
	CD_SUBCODE_SIZE,
	0, //TODO: analyze other values
	0,
	0,
	0,
	CD_DATA_SIZE + CD_C2_SIZE + CD_SUBCODE_SIZE
};


SPTD::Status cmd_drive_ready(SPTD &sptd)
{
	CDB::_CDB6GENERIC cdb = {};
	cdb.OperationCode = SCSIOP_TEST_UNIT_READY;
	return sptd.SendCommand(&cdb, CDB6GENERIC_LENGTH, nullptr, 0);
}


DriveQuery cmd_drive_query(SPTD &sptd)
{
	DriveQuery drive_query;

	CDB::_CDB6INQUIRY3 cdb = {};
	cdb.OperationCode = SCSIOP_INQUIRY;
	cdb.AllocationLength = sizeof(INQUIRYDATA);
	INQUIRYDATA inquiry_data;
	auto status = sptd.SendCommand(&cdb, CDB6GENERIC_LENGTH, &inquiry_data, sizeof(inquiry_data));
	if(status.status_code)
		throw_line(std::format("unable to query drive info, SCSI ({})", SPTD::StatusMessage(status)));

	drive_query.vendor_id = normalize_string(std::string((char *)inquiry_data.VendorId, sizeof(inquiry_data.VendorId)));
	drive_query.product_id = normalize_string(std::string((char *)inquiry_data.ProductId, sizeof(inquiry_data.ProductId)));
	drive_query.product_revision_level = normalize_string(std::string((char *)inquiry_data.ProductRevisionLevel, sizeof(inquiry_data.ProductRevisionLevel)));
	drive_query.vendor_specific = normalize_string(std::string((char *)inquiry_data.VendorSpecific, sizeof(inquiry_data.VendorSpecific)));

	return drive_query;
}


std::vector<uint8_t> cmd_read_toc(SPTD &sptd)
{
	std::vector<uint8_t> toc(sizeof(CDROM_TOC));

	CDB::_READ_TOC cdb = {};
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_TOC;
	cdb.StartingTrack = 1;

	*(USHORT *)cdb.AllocationLength = endian_swap((USHORT)toc.size());
	auto status = sptd.SendCommand(&cdb, CDB10GENERIC_LENGTH, toc.data(), (uint32_t)toc.size());
	if(status.status_code)
		throw_line(std::format("unable to read TOC, SCSI ({})", SPTD::StatusMessage(status)));

	auto cdrom_toc = (CDROM_TOC *)toc.data();
	uint16_t toc_buffer_size = sizeof(cdrom_toc->Length) + endian_swap(*(uint16_t *)cdrom_toc->Length);
	toc.resize(toc_buffer_size);

	return toc;
}


std::vector<uint8_t> cmd_read_full_toc(SPTD &sptd)
{
	std::vector<uint8_t> full_toc;

	CDB::_READ_TOC cdb = {};
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_FULL_TOC;
	cdb.StartingTrack = 1;

	// read TOC header first to get the full TOC size
	CDROM_TOC_FULL_TOC_DATA toc_header;
	*(USHORT *)cdb.AllocationLength = endian_swap<USHORT>(sizeof(toc_header));
	auto status = sptd.SendCommand(&cdb, CDB10GENERIC_LENGTH, &toc_header, sizeof(toc_header));
	if(!status.status_code)
	{
		uint16_t toc_buffer_size = sizeof(toc_header.Length) + endian_swap(*(uint16_t *)toc_header.Length);
		full_toc.resize(round_up(toc_buffer_size, sizeof(uint32_t)));

		*(USHORT *)cdb.AllocationLength = endian_swap<USHORT>(toc_buffer_size);
		status = sptd.SendCommand(&cdb, CDB10GENERIC_LENGTH, full_toc.data(), (uint32_t)full_toc.size());
		if(status.status_code)
			full_toc.clear();
		else
			full_toc.resize(toc_buffer_size);
	}

	return full_toc;
}


SPTD::Status cmd_read_cd_text(SPTD &sptd, std::vector<uint8_t> &cd_text)
{
	SPTD::Status status;

	CDB::_READ_TOC cdb = {};
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_CDTEXT;

	// read CD-TEXT header first to get the full TOC size
	CDROM_TOC_CD_TEXT_DATA cd_text_header;
	*(USHORT *)cdb.AllocationLength = endian_swap<USHORT>(sizeof(cd_text_header));
	status = sptd.SendCommand(&cdb, CDB10GENERIC_LENGTH, &cd_text_header, sizeof(cd_text_header));
	if(!status.status_code)
	{
		uint16_t cd_text_buffer_size = sizeof(cd_text_header.Length) + endian_swap(*(uint16_t *)cd_text_header.Length);
		if(cd_text_buffer_size > sizeof(cd_text_header))
		{
			cd_text.resize(round_up(cd_text_buffer_size, sizeof(uint32_t)));

			*(USHORT *)cdb.AllocationLength = endian_swap<USHORT>(cd_text_buffer_size);
			status = sptd.SendCommand(&cdb, CDB10GENERIC_LENGTH, cd_text.data(), (uint32_t)cd_text.size());
			if(!status.status_code)
				cd_text.resize(cd_text_buffer_size);
		}
	}

	return status;
}


SPTD::Status cmd_read_sector(SPTD &sptd, uint8_t *buffer, int32_t start_lba, uint32_t transfer_length, ReadCommand command, ReadType type, ReadFilter filter)
{
	std::vector<uint8_t> cdb(CDB12GENERIC_LENGTH);

	if(command == ReadCommand::READ_CDDA)
	{
		auto &cdb_cdda = *(CDB::_PLXTR_READ_CDDA *)cdb.data();
		cdb_cdda.OperationCode = (uint8_t)command;
		cdb_cdda.LogicalUnitNumber = sptd.GetAddress().Lun;
		*(int32_t *)&cdb_cdda.LogicalBlockByte0 = endian_swap(start_lba);
		*(uint32_t *)&cdb_cdda.TransferBlockByte0 = endian_swap(transfer_length);
		cdb_cdda.SubCode = (uint8_t)type;
	}
	else if(command == ReadCommand::READ_CD)
	{
		if(type == ReadType::SUB)
			throw_line(std::format("invalid sector read type ({})", (uint8_t)type));

		auto &cdb_cd = *(CDB::_READ_CD *)cdb.data();

		cdb_cd.OperationCode = (uint8_t)command;
		cdb_cd.ExpectedSectorType = (uint8_t)filter;
		cdb_cd.Lun = sptd.GetAddress().Lun;
		*(int32_t *)cdb_cd.StartingLBA = endian_swap(start_lba);
		cdb_cd.TransferBlocks[0] = ((uint8_t *)&transfer_length)[2];
		cdb_cd.TransferBlocks[1] = ((uint8_t *)&transfer_length)[1];
		cdb_cd.TransferBlocks[2] = ((uint8_t *)&transfer_length)[0];
		cdb_cd.ErrorFlags = (uint8_t)(type == ReadType::DATA_C2_SUB ? ReadCDErrorFlags::C2 : ReadCDErrorFlags::NONE);
		cdb_cd.IncludeEDC = 1;
		cdb_cd.IncludeUserData = 1;
		cdb_cd.HeaderCode = (UCHAR)ReadCDHeaderCode::ALL_HEADERS;
		cdb_cd.IncludeSyncData = 1;
		cdb_cd.SubChannelSelection = (uint8_t)(type == ReadType::DATA ? ReadCDSubChannelSelection::NONE : ReadCDSubChannelSelection::RAW);
	}
	else
		throw_line(std::format("invalid sector read command ({:02X})", (uint8_t)command));

	uint32_t buffer_length = ((uint8_t)type < dim(READ_CDDA_SIZES) ? READ_CDDA_SIZES[(uint8_t)type] : 0) * transfer_length;
	if(buffer_length == 0)
		throw_line(std::format("invalid sector read type ({})", (uint8_t)type));

	return sptd.SendCommand(cdb.data(), (uint8_t)cdb.size(), buffer, buffer_length);
}


SPTD::Status cmd_plextor_reset(SPTD &sptd)
{
	CDB::_CDB6GENERIC cdb = {};
	cdb.OperationCode = 0xEE;

	return sptd.SendCommand(&cdb, CDB6GENERIC_LENGTH, nullptr, 0);
}


SPTD::Status cmd_synchronize_cache(SPTD &sptd)
{
	CDB::_CDB6GENERIC cdb = {};
	cdb.OperationCode = SCSIOP_SYNCHRONIZE_CACHE;

	return sptd.SendCommand(&cdb, CDB6GENERIC_LENGTH, nullptr, 0);
}


SPTD::Status cmd_flush_drive_cache(SPTD &sptd, int32_t lba)
{
	CDB::_READ12 cdb = {};
	cdb.OperationCode = SCSIOP_READ12;
	cdb.ForceUnitAccess = 1;
	*(int32_t *)cdb.LogicalBlock = endian_swap(lba);
	return sptd.SendCommand(&cdb, CDB12GENERIC_LENGTH, nullptr, 0);
}


SPTD::Status cmd_set_cd_speed(SPTD &sptd, uint16_t speed)
{
	CDB::_SET_CD_SPEED cdb = {};
	cdb.OperationCode = SCSIOP_SET_CD_SPEED;
	*(uint16_t *)cdb.ReadSpeed = endian_swap(speed);
	return sptd.SendCommand(&cdb, CDB12GENERIC_LENGTH, nullptr, 0);
}


SPTD::Status cmd_asus_read_cache(SPTD &sptd, uint8_t *buffer, uint32_t offset, uint32_t size)
{
	CDB_ASUS_ReadCache cdb;
	cdb.operation_code = 0xF1;
	cdb.unknown = 6;
	cdb.offset = endian_swap(offset);
	cdb.size = endian_swap(size);

	return sptd.SendCommand(&cdb, sizeof(cdb), buffer, size);
}

}