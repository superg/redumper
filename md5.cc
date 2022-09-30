#include <cmath>
#include "md5.hh"



namespace gpsxre
{

const uint32_t MD5::_S[] =
{
	7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
	5,	9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
	4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
	6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
};


MD5::MD5()
	: BlockHasher(16 * sizeof(uint32_t))
	, _hash(DefaultHash())
{
	for(uint32_t i = 0; i < 64; ++i)
		_k[i] = (uint32_t)floor((UINT64_C(1) << 32) * std::abs(sin(i + 1)));
}


std::vector<uint32_t> MD5::DefaultHash()
{
	return std::vector<uint32_t>{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
}


void MD5::UpdateBlock(const uint8_t *block)
{
	auto m = (uint32_t *)block;

	// initialize hash value for this chunk
	uint32_t a = _hash[0];
	uint32_t b = _hash[1];
	uint32_t c = _hash[2];
	uint32_t d = _hash[3];

	// main loop
	for(uint32_t i = 0; i < 64; ++i)
	{
		uint32_t f, g;
		// alternative i < 16 and i < 32 computation yields more optimized code in some cases
		if(i < 16)
		{
			f = d ^ b & (c ^ d);
			g = i;
		}
		else if(i < 32)
		{
			f = c ^ d & (b ^ c);
			g = (5 * i + 1) % 16;
		}
		else if(i < 48)
		{
			f = b ^ c ^ d;
			g = (3 * i + 5) % 16;
		}
		else
		{
			f = c ^ (b | ~d);
			g = (7 * i) % 16;
		}

		f += a + _k[i] + m[g];
		a = d;
		d = c;
		c = b;
		b += ROTL(f, _S[i]);
	}

	// add this chunk's hash to result so far
	_hash[0] += a;
	_hash[1] += b;
	_hash[2] += c;
	_hash[3] += d;
}


uint64_t MD5::ConvertML(uint64_t ml)
{
	return ml;
}


std::vector<uint32_t> MD5::Hash()
{
	auto h(_hash);
	_hash = DefaultHash();
	return h;
}

}
