module;
#include <cstdint>

export module readers.form1_reader;

import cd.cdrom;
import readers.sector_reader;



namespace gpsxre
{

export class Form1Reader : public SectorReader
{
public:
	uint32_t sectorSize() const override
	{
		return FORM1_DATA_SIZE;
	}
};

}
