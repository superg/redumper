module;
#include <cstdint>
#include <string>

export module readers.disc_read_form1_reader;

import cd.cdrom;
import readers.sector_reader;
import scsi.cmd;
import scsi.sptd;



namespace gpsxre
{

export class Disc_READ_Reader : public SectorReader
{
public:
	Disc_READ_Reader(SPTD &sptd)
		: _sptd(sptd)
	{
		;
	}


	uint32_t read(uint8_t *sectors, uint32_t index, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
	{
		uint32_t sectors_read = 0;

		if(!form2)
		{
			auto status = cmd_read(_sptd, sectors, FORM1_DATA_SIZE, index, count, false);
			if(!status.status_code)
				sectors_read = count;
		}

		if(form_hint != nullptr)
			*form_hint = false;

		return sectors_read;
	}


	uint32_t sectorSize(bool form2 = false) override
	{
		return FORM1_DATA_SIZE;
	}

private:
	SPTD &_sptd;
};

}
