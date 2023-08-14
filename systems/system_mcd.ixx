module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <ostream>
#include <set>
#include <string_view>

export module systems.mcd;

import filesystem.image_browser;
import systems.system;
import utils.hex_bin;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemMCD : public SystemT<SystemMCD>
{
public:
	SystemMCD(const std::filesystem::path &track_path)
		: _trackPath(track_path)
		, _trackSize(std::filesystem::file_size(track_path))
	{
		;
	}

	static std::string name()
	{
		return "MCD";
	}

	Type getType() override
	{
		return Type::ISO;
	}

	void printInfo(std::ostream &os) const override;

private:
	static constexpr std::string_view _SYSTEM_MAGIC = "SEGADISCSYSTEM";
	static constexpr uint32_t _ROM_HEADER_OFFSET = 0x100;
	static const std::map<std::string, uint32_t> _ROM_MONTHS;
	static const std::map<char, std::string> _ROM_REGIONS;
	static const std::map<char, std::string> _ROM_REGIONS_NEW;

	struct ROMHeader
	{
		char system_name[16];
		char publisher[8];
		char date[8];
		char title[48];
		char title_international[48];

		union
		{
			struct
			{
				char serial[14];
				uint16_t checksum;
			};
			char serial_long[16];
		};
		char device_support[16];
		uint32_t rom_start;
		uint32_t rom_end;
		uint32_t ram_start;
		uint32_t ram_end;
		struct
		{
			char id[2];
			char type;
			char space;
			uint32_t start;
			uint32_t end;
		} backup_memory;
		struct
		{
			char id[2];
			char publisher[4];
			char game_number[2];
			char comma;
			char version;
			char region_microphone[2];
		} modem;
		char reserved1[40];
		char regions[3];
		char reserved2[13];
	};

	std::filesystem::path _trackPath;
	uint64_t _trackSize;


	std::string extractDate(std::string header_date) const
	{
		std::string date;

		uint32_t month_start = 5;

		// account for no delimiter
		if(header_date.back() == ' ')
		{
			header_date.pop_back();
			month_start = 4;
		}

		long long year = 0;
		stoll_try(year, std::string(header_date, 0, 4));
		if(year && year < 100)
			year += 1900;

		std::string month(header_date, month_start);
		uint32_t month_index = 0;
		auto it = _ROM_MONTHS.find(month);
		if(it != _ROM_MONTHS.end())
			month_index = it->second;

		if(year || month_index)
			date = std::format("{:04}-{:02}", year, month_index);

		return date;
	}
};


const std::map<std::string, uint32_t> SystemMCD::_ROM_MONTHS =
{
	{"JAN",  1},
	{"FEB",  2},
	{"MAR",  3},
	{"APR",  4},
	{"MAY",  5},
	{"JUN",  6},
	{"JUL",  7},
	{"AUG",  8},
	{"SEP",  9},
	{"OCT", 10},
	{"NOV", 11},
	{"DEC", 12},
};


const std::map<char, std::string> SystemMCD::_ROM_REGIONS =
{
	{'J', "Japan" },
	{'U', "USA"   },
	{'E', "Europe"}
};


const std::map<char, std::string> SystemMCD::_ROM_REGIONS_NEW =
{
	{'0', ""   },
	{'1', "J"  },
	{'2', ""   },
	{'3', "J"  },
	{'4', "U"  },
	{'5', "JU" },
	{'6', "U"  },
	{'7', "JU" },
	{'8', "E"  },
	{'9', "JE" },
	{'A', "E"  },
	{'B', "JE" },
	{'C', "UE" },
	{'D', "JUE"},
//	 'E' - reserved for Europe, prioritize old style
	{'F', "JUE"}
};


void SystemMCD::printInfo(std::ostream &os) const
{
	if(!ImageBrowser::IsDataTrack(_trackPath))
		return;

	ImageBrowser browser(_trackPath, 0, _trackSize, false);

	auto system_area = browser.getSystemArea();
	if(system_area.size() < _ROM_HEADER_OFFSET + sizeof(ROMHeader) || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
		return;

	auto rom_header = (ROMHeader *)(system_area.data() + _ROM_HEADER_OFFSET);

	//TODO:
	// http://redump.org/disc/3157/
	// http://redump.org/disc/42139/
	// review if ISO9660 PVD has the same dates in all cases

	std::string date = extractDate(std::string(rom_header->date, sizeof(rom_header->date)));
	if(!date.empty())
		os << std::format("  build date: {}", date) << std::endl;

	// MCD often reuse checksum field for serial
	std::string serial(rom_header->checksum == 1 ? std::string(rom_header->serial, sizeof(rom_header->serial)) : std::string(rom_header->serial_long, sizeof(rom_header->serial_long)));
	// erase software type if specified
	if(serial[2] == ' ')
		serial.erase(serial.begin(), serial.begin() + 2);
	erase_all_inplace(serial, ' ');
	if(!serial.empty())
		os << std::format("  serial: {}", serial) << std::endl;

	std::string regions(rom_header->regions, sizeof(rom_header->regions));
	erase_all_inplace(regions, ' ');

	// new style
	if(regions.length() == 1)
	{
		auto it = _ROM_REGIONS_NEW.find(regions.front());
		if(it != _ROM_REGIONS_NEW.end())
			regions = it->second;
	}

	std::set<std::string> unique_regions;
	for(auto r : regions)
	{
		auto it = _ROM_REGIONS.find(r);
		if(it != _ROM_REGIONS.end())
			unique_regions.insert(it->second);
	}

	os << (unique_regions.size() == 1 ? "  region: " : "  regions: ");
	bool comma = false;
	for(auto r : unique_regions)
	{
		os << (comma ? ", " : "") << r;
		comma = true;
	}
	os << std::endl;
	
	os << "  header:" << std::endl;
	os << std::format("{}", hexdump(system_area.data(), _ROM_HEADER_OFFSET, sizeof(ROMHeader)));
}

}
