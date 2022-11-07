#pragma once



#include "redumper.hh"



namespace gpsxre
{

constexpr uint32_t OFFSET_SHIFT_MAX = 4;
constexpr uint32_t OFFSET_SHIFT_SYNC_TOLERANCE = 2;

struct TrackEntry
{
	std::string filename;
	bool data;

	uint32_t ecc_errors;
	uint32_t edc_errors;
	uint32_t subheader_errors;
	uint32_t redump_errors;

	uint32_t crc;
	std::string md5;
	std::string sha1;
};

void redumper_protection(Options &options);
void redumper_split(const Options &options);
void redumper_info(const Options &options);

}
