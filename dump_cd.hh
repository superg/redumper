#pragma once



#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <vector>
#include "cd.hh"
#include "common.hh"
#include "drive.hh"
#include "options.hh"
#include "scsi.hh"

import cd.toc;



namespace gpsxre
{

struct LeadInEntry
{
	std::vector<uint8_t> data;
	int32_t lba_start;
	bool verified;
};

bool redumper_dump_cd(const Options &options, bool refine);
uint32_t percentage(int32_t value, uint32_t value_max);
SPTD::Status read_sector(uint8_t *sector_buffer, SPTD &sptd, const DriveConfig &drive_config, int32_t lba);
bool is_data_track(int32_t lba, const TOC &toc);
uint32_t state_from_c2(std::vector<State> &state, const uint8_t *c2_data);
void plextor_store_sessions_leadin(std::fstream &fs_scm, std::fstream &fs_sub, std::fstream &fs_state, SPTD &sptd, const std::vector<int32_t> &session_lba_start, const DriveConfig &di, const Options &options);
bool refine_needed(std::fstream &fs_state, int32_t lba_start, int32_t lba_end, int32_t read_offset);
void debug_print_c2_scm_offsets(const uint8_t *c2_data, uint32_t lba_index, int32_t lba_start, int32_t drive_read_offset);
uint32_t debug_get_scram_offset(int32_t lba, int32_t write_offset);

}
