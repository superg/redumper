#include <iomanip>
#include <sstream>
#include "hex_bin.hh"



namespace gpsxre
{

std::string hexdump(const uint8_t *data, uint32_t offset, uint32_t size)
{
	std::string dump;

	auto data_offset = data + offset;

	//FIXME: tail < 16 is not included, not needed right now
	uint32_t rows = size / 16;

	std::stringstream ss;
	ss << std::setfill('0');
	for(uint32_t r = 0; r < rows; ++r)
	{
		uint32_t row_offset = r * 16;

		ss << std::hex << std::uppercase;
		ss << std::setw(4) << offset + row_offset << " : ";

		for(uint32_t i = 0; i < 16; ++i)
		{
			if(i == 8)
				ss << ' ';
			ss << std::setw(2) << (uint32_t)data_offset[row_offset + i] << ' ';
		}

		ss << std::dec << "  ";

		for(uint32_t i = 0; i < 16; ++i)
		{
			auto c = data_offset[row_offset + i];
			ss << (c >= 0x20 && c < 0x80 ? (char)c : '.');
		}
		ss << std::endl;
	}

	dump = ss.str();

	return dump;
}

}
