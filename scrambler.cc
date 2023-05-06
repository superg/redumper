module;
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

export module scrambler;

import common;
import cd;



namespace gpsxre
{

export class Scrambler
{
public:
	Scrambler();
	bool Descramble(uint8_t *sector, int32_t *lba, uint32_t size = CD_DATA_SIZE) const;
	void Process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size) const;

private:
	uint8_t _table[CD_DATA_SIZE];

	void GenerateTable();
};

Scrambler::Scrambler()
{
	GenerateTable();
}


bool Scrambler::Descramble(uint8_t *sector, int32_t *lba, uint32_t size) const
{
	bool unscrambled = false;

	// zeroed or not enough data to analyze
	if(is_zeroed(sector, size) || size < sizeof(Sector::sync) + sizeof(Sector::header))
		return unscrambled;

	// unscramble sector
	Process(sector, sector, 0, size);

	auto s = (Sector *)sector;

	// MSF matches, strong check
	if(lba != nullptr && BCDMSF_to_LBA(s->header.address) == *lba)
	{
		unscrambled = true;
	}
	// sync matches
	else if(!memcmp(sector, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
	{
		if(s->header.mode == 0)
		{
			uint32_t size_to_check = std::min(size - offsetof(Sector, mode2.user_data), sizeof(s->mode2.user_data));

			// whole sector data is expected to be zeroed
			unscrambled = is_zeroed(s->mode2.user_data, size_to_check);
		}
		else if(s->header.mode == 1 || s->header.mode == 2)
		{
			unscrambled = true;
		}
	}

	// if unsuccessful, scramble sector back (unlikely)
	if(!unscrambled)
		Process(sector, sector, 0, size);

	return unscrambled;
}


// ECMA-130
void Scrambler::GenerateTable()
{
	// after the Sync of the Sector, the register is pre-set with the value 0000 0000 0000 0001, where the ONE is the least significant bit
	uint16_t shift_register = 0x0001;

	// if sector sync is processed through scramble / unscramble cycle, this ensures that it will remain unchanged
	// this is not required but remains here for the reference
	memset(_table, 0, sizeof(CD_DATA_SYNC));

	for(uint16_t i = sizeof(CD_DATA_SYNC); i < CD_DATA_SIZE; ++i)
	{
		_table[i] = (uint8_t)shift_register;

		for(uint8_t b = 0; b < CHAR_BIT; ++b)
		{
			// each bit in the input stream of the scrambler is added modulo 2 to the least significant bit of a maximum length register
			bool carry = shift_register & 1 ^ shift_register >> 1 & 1;
			// the 15-bit register is of the parallel block synchronized type, and fed back according to polynomial x15 + x + 1
			shift_register = ((uint16_t)carry << 15 | shift_register) >> 1;
		}
	}
}


void Scrambler::Process(uint8_t *output, const uint8_t *data, uint32_t offset, uint32_t size) const
{
	for(uint32_t i = 0; i < size; ++i)
		output[i] = data[i] ^ _table[offset + i];
}

}
