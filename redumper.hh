#pragma once



#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <vector>
#include "cd.hh"
#include "drive.hh"
#include "options.hh"
#include "scsi.hh"
#include "toc.hh"



namespace gpsxre
{

static constexpr uint32_t SLOW_SECTOR_TIMEOUT = 5;
#if 1
static int32_t LBA_START = MSF_to_LBA(MSF_LEADIN_START); // -45150
#else
// easier debugging, LBA starts with 0, plextor lead-in and asus cache are disabled
 static constexpr int32_t LBA_START = 0;
											// GS2v3   13922 .. 17080-17090
											// GS2_1.1 12762 .. 17075
											// GS2_5.5 12859 .. 17130-17140
											// GS2_1.2 12739 .. 16930-16940
											// SC DISC  8546 .. 17100-17125
											// SC BOX  10547 .. 16940-16950
											// CB4 6407-7114 ..  9200- 9220
											// GS GCD   9162 .. 17000-17010  // F05 0004
											// XPLO FM  7770 .. 10700-10704
//static constexpr int32_t LBA_START = MSF_to_LBA(MSF_LEADIN_START);
#endif


enum class State : uint8_t
{
	ERROR_SKIP       = 0, // must be 0 to support random offset file writes
	ERROR_C2         = 1,
	SUCCESS_C2_OFF   = 2,
	SUCCESS_SCSI_OFF = 3,
	SUCCESS          = 4
};


std::string redumper_version();
void redumper(Options &options);

bool redumper_dump(const Options &options, bool refine);
void redumper_rings(const Options &options);
void redumper_subchannel(const Options &options);
void redumper_debug(const Options &options);

uint32_t percentage(int32_t value, uint32_t value_max);
std::string first_ready_drive();
void drive_init(SPTD &sptd, const Options &options);
SPTD::Status read_sector(uint8_t *sector_buffer, SPTD &sptd, const DriveConfig &drive_config, int32_t lba);
bool is_data_track(int32_t lba, const TOC &toc);
uint32_t state_from_c2(std::vector<State> &state, const uint8_t *c2_data);
void plextor_store_sessions_leadin(std::fstream &fs_scm, std::fstream &fs_sub, std::fstream &fs_state, SPTD &sptd, const std::vector<int32_t> &session_lba_start, const DriveConfig &di, const Options &options);
void debug_print_c2_scm_offsets(const uint8_t *c2_data, uint32_t lba_index, int32_t lba_start, int32_t drive_read_offset);
uint32_t debug_get_scram_offset(int32_t lba, int32_t write_offset);

}
