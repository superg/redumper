module;
#include <cstdint>

export module readers.raw_reader;

import cd.cd;
import readers.sector_reader;



namespace gpsxre
{

export class RawReader : public SectorReader
{
public:
	uint32_t sectorSize() const override
	{
		return CD_DATA_SIZE;
	}
};

}
