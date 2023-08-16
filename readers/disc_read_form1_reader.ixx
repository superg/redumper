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
		, _sectorsCount(sectors_count)
	{
		;
	}


	bool read(uint8_t *block, uint32_t index, uint32_t count) override
	{
		auto status = cmd_read(_sptd, block, getSectorSize(), index, count, false);

		return !status.status_code;
	}
	
	
	uint32_t getSectorsCount() const override
	{
		return _sectorsCount;
	}
	
private:
	SPTD &_sptd;
	uint32_t _sectorsCount;
};

}
