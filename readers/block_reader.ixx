module;
#include <cstdint>

export module readers.block_reader;



namespace gpsxre
{

export template<typename T>
class BlockReader
{
public:
	virtual bool read(uint8_t *block, T index, T count) = 0;
	virtual T blockSize() const = 0;
	virtual T blocksCount() const = 0;
};

export using BlockReader32 = BlockReader<uint32_t>;

}
