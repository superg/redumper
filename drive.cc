#include <chrono>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>
#include "cmd.hh"
#include "common.hh"
#include "crc16_gsm.hh"
#include "endian.hh"
#include "file_io.hh"
#include "logger.hh"
#include "subcode.hh"
#include "drive.hh"



namespace gpsxre
{

std::unordered_map<std::string, int32_t> DRIVE_READ_OFFSETS =
{
#include "driveoffsets.inc"
};


static const DriveInfo DRIVE_INFO_GENERIC = {"", "", "", "", 0, 0, -150, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::GENERIC};

// drive strings are normalized (trimmed and exactly one space between words)
// the same normalize operation is performed when detecting the drive and looking up the read offset
// match is performed on the vendor / product / revision level, vendor specific is just for my reference for the drives I own
// if string is empty, the match is always true
static const std::vector<DriveInfo> KNOWN_DRIVES =
{
	// PLEXTOR OLD
//	{"PLEXTOR", "CD-ROM PX-8XCS"  , "", ""},
//	{"PLEXTOR", "CD-ROM PX-12CS"  , "", ""},
//	{"PLEXTOR", "CD-ROM PX-12TS"  , "", ""},
//	{"PLEXTOR", "CD-ROM PX-20TS"  , "", ""},
//	{"PLEXTOR", "CD-ROM PX-32CS"  , "", ""},
//	{"PLEXTOR", "CD-ROM PX-32TS"  , "", ""},
//	{"HP", "CD-ROM CD32X", "1.02", "01/13/98 01:02"},
//	{"PLEXTOR", "CD-ROM PX-40TS"  , "", ""},
//	{"PLEXTOR", "CD-ROM PX-40TSUW", "", ""},
//	{"PLEXTOR", "CD-ROM PX-40TW"  , "", ""},

	// PLEXTOR CD
	{"PLEXTOR", "CD-R PREMIUM"  , "1.04", "09/04/03 15:00",  +30, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PREMIUM2" , ""    , ""              ,  +30, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-320A"  , ""    , ""              ,  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-R412C" , ""    , ""              , +355, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-R820T" , ""    , ""              , +355, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-S88T"  , ""    , ""              ,  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W1210A", ""    , ""              ,  +99, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W1210S", ""    , ""              ,  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W124TS", ""    , ""              , +943, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W1610A", ""    , ""              ,  +99, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W2410A", ""    , ""              ,  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W4012A", "1.07", "03/22/06 09:00",  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "CD-R PX-W4012S", ""    , ""              ,  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W4220T", ""    , ""              , +355, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W4824A", "1.07", "03/24/06 14:00",  +98, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED: extremely slow reading lead-in
	{"PLEXTOR", "CD-R PX-W5224A", "1.04", "04/10/06 17:00",  +30, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "CD-R PX-W8220T", ""    , ""              , +355, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "CD-R PX-W8432T", ""    , ""              , +355, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},

	// PLEXTOR DVD
	{"PLEXTOR", "DVDR PX-704A" , ""    , ""              ,  +30, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "DVDR PX-708A" , "1.12", "03/13/06 21:00",  +30, 294, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-708A2", ""    , ""              ,  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "DVDR PX-712A" , "1.09", "03/31/06 10:00",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-714A" , ""    , ""              ,  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
	{"PLEXTOR", "DVDR PX-716A" , "1.11", "03/23/07 15:10",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-716A" , "1.58", "03/23/07 15:10",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-716A" , "1.59", "12/15/05 09:20",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-716A" , "1.5A", "10/19/06 15:00",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-716AL", ""    , ""              ,  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR},
//	{"PLEXTOR", "DVDR PX-740A" , "1.02", "12/19/05"      , +618,   0, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::GENERIC}, // CHECKED: no D8
	{"PLEXTOR", "DVDR PX-755A" , "1.08", "08/18/07 15:10",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED
	{"PLEXTOR", "DVDR PX-760A" , "1.07", "08/18/07 15:10",  +30, 295, -75, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // CHECKED

	// LG/ASUS
	{"ASUS"    , "BW-16D1HT"      , "3.02", "W000800KL8J9NJ3134" , +6, 0, -135, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::LG_ASUS}, // CHECKED
//	{"ASUS"    , "BW-16D1HT"      , "3.10", "WM01601KL8J9NJ3134" , +6, 0, -135, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::GENERIC}, // RIB
//	{"HL-DT-ST", "DVDRAM GH24NSC0", "LY00", "C010101 KMIJ8O50256", +6, 0, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::LG_ASUS},
//	{"HL-DT-ST", "BD-RE WH16NS40" , "1.05", "N000900KLZL4TG5625" , +6, 0, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::LG_ASUS},
	{"ASUS"    , "SDRW-08D2S-U"   , "B901", "2015/03/03 15:29"   , +6, 0, 0, DriveInfo::SectorOrder::DATA_SUB_C2, DriveInfo::Type::GENERIC},

	// OTHER
//	{"QPS"    , "CD-W524E"      , "1.5A", "10/23/01"      ,  +686, 0, DriveInfo::SectorOrder::DATA_C2_SUB, DriveInfo::Type::PLEXTOR}, // TEAC
};


// Plextor firmware blocked LBA ranges:
// BE [-inf .. -20000], (-1000 .. -75)
// D8 [-inf .. -20150], (-1150 .. -75)
// 
// BE is having trouble reading LBA close to -1150]
// D8 range boundaries are shifted left by 150 sectors comparing to BE
// 
// It is also possible to read large negative offset ranges (starting from 0xFFFFFFFF - smallest negative 32-bit integer)
// with BE command with disabled C2 if the disc first track is a data track. Regardless of a starting point, such reads
// are "virtualized" by Plextor drive and will always start close to lead-in toc area and sequentially read until the end
// of the data track under normal circumstances. After end of the data track, the drive is wrapping around and starts in
// the lead-in toc area again. This process will continue until drive reaches firmware blocked LBA ranges specified here.
// However, any external pause between sequential reads (Debugger pause, sleep() call etc.) will lead to drive counter
// resetting and starting reading in the lead-in toc area again.
// 
// However the following range, while preserving the above behaviour, is unlocked for both BE and D8 commands with
// disabled C2. Use it to dynamically find first second of pregap based on the Q subcode and prepend it to the rest of
// [-75 .. leadout) Plextor dump, optionally saving the lead-in
static const std::pair<int32_t, int32_t> PLEXTOR_TOC_RANGE = {-20150, -1150};


//	LG/ASUS cache map:
//	0x0000 main
//	0x0930 raw P-W
//	0x0990 Q
//	0x09A0 unknown
//	0x09A4 C2
//	0x0ACA unknown
//	0x0B00 end
constexpr uint32_t ASUS_CACHE_ENTRY_SIZE = 0xB00;
constexpr uint32_t ASUS_CACHE_ENTRIES_COUNT = 1070;


DriveInfo drive_get_info(const DriveQuery &drive_query)
{
	DriveInfo drive_info = DRIVE_INFO_GENERIC;

	bool found = false;
	for(auto const &di : KNOWN_DRIVES)
	{
		if((di.vendor_id.empty() || di.vendor_id == drive_query.vendor_id) &&
		   (di.product_id.empty() || di.product_id == drive_query.product_id) &&
		   (di.product_revision_level.empty() || di.product_revision_level == drive_query.product_revision_level))
		{
			drive_info = di;
			found = true;
			break;
		}
	}

	drive_info.vendor_id = drive_query.vendor_id;
	drive_info.product_id = drive_query.product_id;
	drive_info.product_revision_level = drive_query.product_revision_level;
	drive_info.vendor_specific = drive_query.vendor_specific;

	if(!found)
		drive_info.read_offset = drive_get_generic_read_offset(drive_info.vendor_id, drive_info.product_id);

	return drive_info;
}


// AccurateRip database provides already "processed" drive offsets e.g.
// the drive offset number has to be added to the data read start in order to get it corrected
// (positive offset means that data has to be shifted left, negative - right)
int32_t drive_get_generic_read_offset(const std::string &vendor, const std::string &product)
{
	int32_t offset = 0;

	//FIXME: clean up this AccurateRip mess later
	std::string v(vendor);
	if(vendor == "HL-DT-ST")
		v = "LG Electronics";
	else if(vendor == "JLMS")
		v = "Lite-ON";
	else if(vendor == "Matshita")
		v = "Panasonic";
	else
		v = vendor;

	std::string vendor_product(fmt::format("{} - {}", v, product));
	if(auto it = DRIVE_READ_OFFSETS.find(vendor_product); it != DRIVE_READ_OFFSETS.end())
		offset = it->second;
	else
		throw_line(fmt::format("drive read offset not found ({})", vendor_product));

	return offset;
}


std::string drive_info_string(const DriveInfo &di)
{
	std::string revision_level(di.product_revision_level.empty() ? "" : fmt::format(", revision level: {}", di.product_revision_level));
	std::string vendor_specific(di.vendor_specific.empty() ? "" : fmt::format(", vendor specific: {}", di.vendor_specific));

	return fmt::format("{} - {} (read offset: {:+}, C2 offset: {}{}{})", di.vendor_id, di.product_id, di.read_offset, di.c2_offset, revision_level, vendor_specific);
}


void print_supported_drives()
{
	LOG("");
	LOG("supported drives: ");
	for(auto const &di : KNOWN_DRIVES)
		if(di.type != DriveInfo::Type::GENERIC)
			LOG("{}", drive_info_string(di));
	LOG("");
}


std::vector<uint8_t> plextor_read_leadin(SPTD &sptd, const DriveInfo &di)
{
	std::vector<uint8_t> buffer;

	buffer.reserve(5000 * PLEXTOR_LEADIN_ENTRY_SIZE);

	int32_t lba_start = PLEXTOR_TOC_RANGE.first + 1;
	int32_t lba_end = PLEXTOR_TOC_RANGE.second + 1;

	for(int32_t lba = lba_start; lba < lba_end; ++lba)
	{
		uint32_t lba_index = lba - lba_start;
		buffer.resize((lba_index + 1) * PLEXTOR_LEADIN_ENTRY_SIZE);
		uint8_t *entry = &buffer[lba_index * PLEXTOR_LEADIN_ENTRY_SIZE];
		auto &status = *(SPTD::Status *)entry;

		LOG_R();
		LOGC_F("[LBA: {:6}]", lba);
		
		std::vector<uint8_t> sector_buffer;
		status = cmd_read_cdda(sptd, sector_buffer, lba, 1, READ_CDDA_SubCode::DATA_SUB);

		if(!status.status_code)
		{
			memcpy(entry + sizeof(SPTD::Status), sector_buffer.data(), sector_buffer.size());
			uint8_t *sub_data = entry + sizeof(SPTD::Status) + CD_DATA_SIZE;

			ChannelQ Q;
			subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

			//DEBUG
//			LOG_R();
//			LOGC("{}", Q.Decode());

			if(Q.Valid())
			{
				uint8_t adr = Q.control_adr & 0x0F;
				if(adr == 1 && Q.mode1.tno)
				{
					int32_t lba_stop = lba + (di.pregap_start - MSF_LBA_SHIFT);
					if(lba_stop < lba_end)
						lba_end = lba_stop;
				}
			}
		}
	}

	return buffer;
}


std::vector<uint8_t> asus_cache_read(SPTD &sptd)
{
	constexpr uint32_t read_size = 1024 * 64; // 64Kb

	std::vector<uint8_t> cache(1024 * 1024 * 3); // 3Mb
	if(cache.size() < ASUS_CACHE_ENTRY_SIZE * ASUS_CACHE_ENTRIES_COUNT)
		throw_line("unexpected cache configuration");

	for(uint32_t offset = 0, n = (uint32_t)cache.size(); offset < n; offset += read_size)
	{
		SPTD::Status status = cmd_asus_read_cache(sptd, cache.data() + offset, offset, std::min(read_size, n - offset));
		if(status.status_code)
			throw_line(fmt::format("read cache failed, SCSI ({})", SPTD::StatusMessage(status)));
	}

	return cache;
}


std::vector<uint8_t> asus_cache_extract(const std::vector<uint8_t> &cache, int32_t lba_start, uint32_t entries_count)
{
	int32_t index_start = ASUS_CACHE_ENTRIES_COUNT;
	std::pair<int32_t, int32_t> index_range = {ASUS_CACHE_ENTRIES_COUNT, ASUS_CACHE_ENTRIES_COUNT};
	std::pair<int32_t, int32_t> lba_range;

	// try to find the exact match
	for(uint32_t i = 0; i < ASUS_CACHE_ENTRIES_COUNT; ++i)
	{
		auto entry = (uint8_t *)&cache[ASUS_CACHE_ENTRY_SIZE * i];
		uint8_t *sub_data = entry + 0x0930;

		ChannelQ Q;
		subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

		if(!Q.Valid())
			continue;

		uint8_t adr = Q.control_adr & 0x0F;
		if(adr != 1 || !Q.mode1.tno)
			continue;
			
		int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
		if(lba == lba_start)
		{
			index_start = i;
			break;
		}
		else if(lba < lba_start)
		{
			if(index_range.first == ASUS_CACHE_ENTRIES_COUNT || lba > lba_range.first)
			{
				index_range.first = i;
				lba_range.first = lba;
			}
		}
		else if(lba > lba_start)
		{
			if(index_range.second == ASUS_CACHE_ENTRIES_COUNT || lba < lba_range.second)
			{
				index_range.second = i;
				lba_range.second = lba;
			}
		}
	}

	// calculate index_start based on valid range boundaries
	if(index_start == ASUS_CACHE_ENTRIES_COUNT && index_range.first != ASUS_CACHE_ENTRIES_COUNT && index_range.second != ASUS_CACHE_ENTRIES_COUNT)
	{
		if(index_range.first > index_range.second)
			index_range.second += ASUS_CACHE_ENTRIES_COUNT;

		if(lba_range.second - lba_range.first == index_range.second - index_range.first)
			index_start = (index_range.first + lba_start - lba_range.first) % ASUS_CACHE_ENTRIES_COUNT;
	}

	std::vector<uint8_t> data;

	if(!entries_count || entries_count > ASUS_CACHE_ENTRIES_COUNT)
		entries_count = ASUS_CACHE_ENTRIES_COUNT;

	if(index_start != ASUS_CACHE_ENTRIES_COUNT)
	{
		data.reserve(entries_count * CD_RAW_DATA_SIZE);

		for(uint32_t i = 0; i < entries_count; ++i)
		{
			uint32_t index = (index_start + i) % ASUS_CACHE_ENTRIES_COUNT;
			auto entry = (uint8_t *)&cache[ASUS_CACHE_ENTRY_SIZE * index];
			uint8_t *main_data = entry + 0x0000;
			uint8_t *c2_data = entry + 0x09A4;
			uint8_t *sub_data = entry + 0x0930;

			ChannelQ Q;
			subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

			if(Q.Valid())
			{
				uint8_t adr = Q.control_adr & 0x0F;
				if(adr == 1 && Q.mode1.tno)
				{
					if(lba_start + i != BCDMSF_to_LBA(Q.mode1.a_msf))
						break;
				}
			}

			data.insert(data.end(), main_data, main_data + CD_DATA_SIZE);
			data.insert(data.end(), c2_data, c2_data + CD_C2_SIZE);
			data.insert(data.end(), sub_data, sub_data + CD_SUBCODE_SIZE);
		}
	}
	
	return data;
}


void asus_cache_print_subq(const std::vector<uint8_t> &cache)
{
	for(uint32_t i = 0; i < ASUS_CACHE_ENTRIES_COUNT; ++i)
	{
		auto entry = (uint8_t *)&cache[ASUS_CACHE_ENTRY_SIZE * i];
		uint8_t *sub_data = entry + 0x0930;

		ChannelQ Q;
		subcode_extract_channel((uint8_t *)&Q, sub_data, Subchannel::Q);

		int32_t lba = BCDMSF_to_LBA(Q.mode1.a_msf);
		LOG("{:4} {:6}: {}", i, lba, Q.Decode());
	}
}

}
