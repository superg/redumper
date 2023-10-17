module;
#include <cstdint>
#include <limits>

export module readers.sector_reader;



namespace gpsxre
{

export class SectorReader
{
public:
	virtual uint32_t read(uint8_t *sectors, uint32_t index, uint32_t count) = 0;
	virtual uint32_t readLBA(uint8_t *sectors, uint32_t lba, uint32_t count) { return read(sectors, lba, count); }
	virtual uint32_t sectorSize() const = 0;
	virtual uint32_t sectorsCount() const { return std::numeric_limits<uint32_t>::max(); }
};

}
