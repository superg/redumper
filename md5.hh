#pragma once



#include <vector>

import hash.block_hasher;



namespace gpsxre
{

class MD5 : public BlockHasher
{
public:
	MD5();

private:
	static const uint32_t _S[];
	uint32_t _k[64];
	std::vector<uint32_t> _hash;

	virtual void UpdateBlock(const uint8_t *block);
	virtual uint64_t ConvertML(uint64_t ml);
	virtual std::vector<uint32_t> Hash();

	std::vector<uint32_t> DefaultHash();
};

}
