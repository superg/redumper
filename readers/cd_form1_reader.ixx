module;
#include <cstdint>

export module readers.cd_form1_reader;

import readers.form1_reader;
import scsi.cmd;
import scsi.sptd;



namespace gpsxre
{

export class CDForm1Reader : public Form1Reader
{
public:
	CDForm1Reader(SPTD &sptd, uint32_t blocks_count)
		: _sptd(sptd)
		, _blocksCount(blocks_count)
	{
		;
	}


	bool read(uint8_t *block, uint32_t index, uint32_t count) override
	{
		auto status = cmd_read(_sptd, block, blockSize(), index, count, false);

		return !status.status_code;
	}
	
	
	uint32_t blocksCount() const override
	{
		return _blocksCount;
	}
	
private:
	SPTD &_sptd;
	uint32_t _blocksCount;
};

}
