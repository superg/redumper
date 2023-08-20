module;
#include <cstdint>

export module readers.disc_read_form1_reader;

import readers.form1_reader;
import scsi.cmd;
import scsi.sptd;



namespace gpsxre
{

export class Disc_READ_Form1Reader : public Form1Reader
{
public:
	Disc_READ_Form1Reader(SPTD &sptd, uint32_t sectors_count)
		: _sptd(sptd)
		, _count(sectors_count)
	{
		;
	}


	bool read(uint8_t *sectors, uint32_t index, uint32_t count) override
	{
		auto status = cmd_read(_sptd, sectors, sectorSize(), index, count, false);

		return !status.status_code;
	}
	
	
	uint32_t count() const override
	{
		return _count;
	}
	
private:
	SPTD &_sptd;
	uint32_t _count;
};

}
