module;
#include <cstdint>

export module readers.block_reader;



namespace gpsxre
{

export template<typename T, T BLOCK_SIZE>
class BlockReader
{
public:
	virtual bool read(uint8_t *block, T index, T count) = 0;
	T blockSize() const
	{
		return BLOCK_SIZE;
	}

	virtual T blocksCount() const
	{
		return 0;
	}
};

}
