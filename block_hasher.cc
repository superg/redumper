module;
#include <algorithm> // std::min
#include <climits>   // CHAR_BIT
#include <cstdint>
#include <string>
#include <vector>

export module hash.block_hasher;

import hex_bin;



namespace gpsxre
{

export class BlockHasher
{
public:
	BlockHasher(uint32_t block_size)
		: _blockSize(block_size)
		, _blocksHashed(0)
	{
		_tail.reserve(_blockSize);
	}

	void Update(const uint8_t *data, uint64_t size)
	{
		auto data_end = data + size;

		if(!_tail.empty())
		{
			auto size_to_copy = std::min((uint64_t)_blockSize - _tail.size(), size);
			_tail.insert(_tail.end(), data, data + size_to_copy);
			data += size_to_copy;

			if(_tail.size() == _blockSize)
			{
				Update(_tail.data());
				_tail.clear();
			}
		}

		for(; (uint32_t)(data_end - data) >= _blockSize; data += _blockSize)
			Update(data);

		_tail.insert(_tail.end(), data, data_end);
	}

	std::string Final()
	{
		// calculate original message length in bits
		uint64_t ml = (_blocksHashed * _blockSize + _tail.size()) * CHAR_BIT;

		// append the bit '1' to the message e.g. by adding 0x80
		_tail.push_back(0x80);

		// pad chunk with '0' bits
		auto bytes_left = _blockSize - (uint64_t)_tail.size();
		_tail.resize(_blockSize, 0);

		// if no space to store message length, store it in the next block
		if(bytes_left < sizeof(uint64_t))
		{
			Update(_tail.data());
			std::fill(_tail.begin(), _tail.end() - sizeof(uint64_t), 0);
		}

		// store message length (will be swapped again inside)
		*(uint64_t *)(_tail.data() + _blockSize - sizeof(uint64_t)) = ConvertML(ml);
		Update(_tail.data());

		_blocksHashed = 0;
		_tail.clear();

		return bin2hex(Hash());
	}

protected:
	virtual void UpdateBlock(const uint8_t *block) = 0;
	virtual uint64_t ConvertML(uint64_t ml) = 0;
	virtual std::vector<uint32_t> Hash() = 0;

	static uint32_t ROTL(uint32_t x, uint32_t n)
	{
		return x << n | x >> ((0 - n) & 31);
	}

private:
	uint32_t _blockSize;
	uint64_t _blocksHashed;
	std::vector<uint8_t> _tail;

	void Update(const uint8_t *block)
	{
		UpdateBlock(block);
		++_blocksHashed;
	}
};

}
