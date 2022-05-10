#include "endian.hh"
#include "sha1.hh"



namespace gpsxre
{

SHA1::SHA1()
	: BlockHasher(16 * sizeof(uint32_t))
	, _hash(DefaultHash())
{
	;
}


std::vector<uint32_t> SHA1::DefaultHash()
{
	return std::vector<uint32_t>{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
}


void SHA1::UpdateBlock(const uint8_t *block)
{
	uint32_t w[80];

	// break chunk into sixteen 32-bit big-endian words
	for(uint32_t i = 0; i < 16; ++i)
		w[i] = endian_swap(((uint32_t *)block)[i]);

	// extend the sixteen 32-bit words into eighty 32-bit words
	for(uint32_t i = 16; i < 32; ++i)
		w[i] = ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	// alternative 32-79 rounds computation keeps 64-bit alignment which allows efficient SIMD implementation
	for(uint32_t i = 32; i < 80; ++i)
		w[i] = ROTL(w[i - 6] ^ w[i - 16] ^ w[i - 28] ^ w[i - 32], 2);

	// initialize hash value for this chunk
	uint32_t a = _hash[0];
	uint32_t b = _hash[1];
	uint32_t c = _hash[2];
	uint32_t d = _hash[3];
	uint32_t e = _hash[4];

	// main loop
	for(uint32_t i = 0; i < 80; ++i)
	{
		uint32_t f, k;

		if(i < 20)
		{
			f = b & c | ~b & d;
			k = 0x5A827999;
		}
		else if(i < 40)
		{
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		}
		else if(i < 60)
		{
			f = b & c | b & d | c & d;
			k = 0x8F1BBCDC;
		}
		else
		{
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}

		uint32_t temp = ROTL(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = ROTL(b, 30);
		b = a;
		a = temp;
	}

	// add this chunk's hash to result so far
	_hash[0] += a;
	_hash[1] += b;
	_hash[2] += c;
	_hash[3] += d;
	_hash[4] += e;
}


uint64_t SHA1::ConvertML(uint64_t ml)
{
	return endian_swap(ml);
}


std::vector<uint32_t> SHA1::Hash()
{
	auto h(_hash);
	std::transform(h.begin(), h.end(), h.begin(), endian_swap<uint32_t>);
	_hash = DefaultHash();

	return h;
}

}
