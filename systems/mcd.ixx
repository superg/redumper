module;

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <ostream>
#include <set>
#include <string_view>
#include <vector>

export module systems.mcd;

import filesystem.iso9660;
import readers.sector_reader;
import systems.system;
import utils.hex_bin;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemMCD : public System
{
public:
	std::string getName() override
	{
		return "MCD";
	}

	Type getType() override
	{
		return Type::ISO;
	}

	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override
	{
		auto system_area = iso9660::Browser::readSystemArea(sector_reader);
		if(system_area.size() < _ROM_HEADER_OFFSET + sizeof(ROMHeader) || memcmp(system_area.data(), _SYSTEM_MAGIC.data(), _SYSTEM_MAGIC.size()))
			return;

		std::vector<uint8_t> rom_header_data(system_area.data() + _ROM_HEADER_OFFSET, system_area.data() + _ROM_HEADER_OFFSET + sizeof(ROMHeader));

		// Power Factory (USA)
		// data is shifted due to one space character missing between serial and device support
		if(rom_header_data.back() == '\0')
		{
			rom_header_data.pop_back();
			rom_header_data.insert(rom_header_data.begin() + offsetof(struct ROMHeader, device_support) - 1, ' ');
		}

		auto rom_header = (ROMHeader *)rom_header_data.data();

		std::string date = extractDate(std::string(rom_header->publisher_date_title, sizeof(rom_header->publisher_date_title)));
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
			auto it = _REGIONS_NEW.find(regions.front());
			if(it != _REGIONS_NEW.end())
				regions = it->second;
		}

		std::set<std::string> unique_regions;
		for(auto r : regions)
		{
			auto it = _REGIONS.find(r);
			if(it != _REGIONS.end())
				unique_regions.insert(it->second);
		}

		if(!unique_regions.empty())
		{
			os << (unique_regions.size() == 1 ? "  region: " : "  regions: ");
			bool comma = false;
			for(auto r : unique_regions)
			{
				os << (comma ? ", " : "") << r;
				comma = true;
			}
			os << std::endl;
		}

		os << "  header:" << std::endl;
		os << std::format("{}", hexdump(system_area.data(), _ROM_HEADER_OFFSET, sizeof(ROMHeader)));
	}

private:
	static constexpr std::string_view _SYSTEM_MAGIC = "SEGADISCSYSTEM";
	static constexpr uint32_t _ROM_HEADER_OFFSET = 0x100;
	static constexpr uint32_t _DATE_OFFSET = 8;
	static constexpr uint32_t _YEAR_SYMBOLS = 4;
	static constexpr uint32_t _MONTH_SYMBOLS = 3;
	static const std::map<std::string, long long> _MONTHS;
	static const std::map<char, std::string> _REGIONS;
	static const std::map<char, std::string> _REGIONS_NEW;
	static const std::set<char> _DATE_DELIMITERS;

	struct ROMHeader
	{
		char system_name[16];
		char publisher_date_title[64];
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


	std::string extractDate(std::string publisher_date_title) const
	{
		// check the usual date location
		auto date = decodeDate(std::string(publisher_date_title, _DATE_OFFSET));
		if(!date.first)
		{
			// find potential date
			for(size_t i = 0; i < publisher_date_title.length(); ++i)
			{
				auto d = decodeDate(std::string(publisher_date_title, i));
				if(number_is_year(d.first))
				{
					date = d;
					break;
				}
			}
		}

		return date.first ? std::format("{:04}-{:02}", date.first, date.second) : "";
	}


	std::pair<uint32_t, uint32_t> decodeDate(std::string header_date) const
	{
		std::pair<uint32_t, uint32_t> date;

		if(header_date.length() >= _YEAR_SYMBOLS)
		{
			int64_t year_index;

			// 4-digit year
			if(auto year_index = str_to_uint64(std::string(header_date, 0, _YEAR_SYMBOLS)))
			{
				date.first = *year_index;
			}
			// 2-digit year
			else if(header_date[0] == ' ' && header_date[1] == ' ')
			{
				if(auto year_index = str_to_uint64(std::string(header_date, 2, _YEAR_SYMBOLS - 2)))
					date.first = *year_index + 1900;
			}

			// extract month
			if(header_date.length() > _YEAR_SYMBOLS)
			{
				size_t month_start = _YEAR_SYMBOLS;

				// skip delimiter if used
				if(_DATE_DELIMITERS.find(header_date[_YEAR_SYMBOLS]) != _DATE_DELIMITERS.end())
					++month_start;

				std::string month(header_date, month_start, _MONTH_SYMBOLS);
				auto it = _MONTHS.find(str_uppercase(month));
				if(it == _MONTHS.end())
				{
					// attempt to decode numeric month value
					if(auto month_index = str_to_uint64(std::string(month, 0, month.find(' '))); number_is_month(*month_index))
						date.second = *month_index;
				}
				else
					date.second = it->second;
			}
		}

		return date;
	}
};


const std::map<std::string, long long> SystemMCD::_MONTHS =
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


const std::map<char, std::string> SystemMCD::_REGIONS =
{
	{'J', "Japan" },
	{'U', "USA"   },
	{'E', "Europe"}
};


const std::map<char, std::string> SystemMCD::_REGIONS_NEW =
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


const std::set<char> SystemMCD::_DATE_DELIMITERS =
{
	'.', ' ', ',', '_'
};

}
