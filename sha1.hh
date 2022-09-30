#pragma once



#include "block_hasher.hh"



namespace gpsxre
{

class SHA1 : public BlockHasher
{
public:
	SHA1();

private:
	std::vector<uint32_t> _hash;

	virtual void UpdateBlock(const uint8_t *block);
	virtual uint64_t ConvertML(uint64_t ml);
	virtual std::vector<uint32_t> Hash();

	std::vector<uint32_t> DefaultHash();
};

}
