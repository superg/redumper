module;
#include <cstdint>

export module readers.raw_reader;

import cd.cd;
import readers.block_reader;



namespace gpsxre
{

export class RawReader : public BlockReader<uint32_t>
{
public:
	uint32_t blockSize() const override
	{
		return CD_DATA_SIZE;
	}
};

}
