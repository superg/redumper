#pragma once



#include <list>
#include <memory>
#include <string>



namespace gpsxre
{

struct Options
{
	std::string command;

	std::list<std::string> positional;

	bool help;
	bool verbose;

	std::string image_path;
	std::string image_name;
	bool overwrite;
	bool force_split;
	bool leave_unchanged;

	std::string drive;
	std::unique_ptr<std::string> drive_type;
	std::unique_ptr<int> drive_read_offset;
	std::unique_ptr<int> drive_c2_shift;
	std::unique_ptr<int> drive_pregap_start;
	std::unique_ptr<std::string> drive_read_method;
	std::unique_ptr<std::string> drive_sector_order;
	std::unique_ptr<int> speed;
	int retries;
	bool refine_subchannel;
	std::unique_ptr<int> lba_start;
	std::unique_ptr<int> lba_end;
	bool force_qtoc;
	std::string skip;
	int skip_fill;
	bool iso9660_trim;
	bool plextor_skip_leadin;
	bool asus_skip_leadout;
	bool correct_offset_shift;
	bool cdi_ready_normalize;
	std::unique_ptr<int> force_offset;
	int audio_silence_threshold;

	Options(int argc, const char *argv[]);

	void PrintUsage();
};

}
