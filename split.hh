#pragma once



#include "redumper.hh"



namespace gpsxre
{

constexpr uint32_t OFFSET_DEVIATION_MAX = CD_PREGAP_SIZE * CD_DATA_SIZE_SAMPLES;
constexpr uint32_t OFFSET_SHIFT_MAX_SECTORS = 4;
constexpr uint32_t OFFSET_SHIFT_SYNC_TOLERANCE = 2;

struct TrackEntry
{
	std::string filename;

	uint32_t crc;
	std::string md5;
	std::string sha1;
};

void redumper_protection(Options &options);
void redumper_split(const Options &options);
void redumper_info(const Options &options);

}
