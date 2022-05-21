#include <climits>
#include <cstring>
#include "scrambler.hh"



namespace gpsxre
{

Scrambler::Scrambler()
{
	GenerateTable();
}


bool Scrambler::Unscramble(uint8_t *sector, int32_t lba) const
{
	uint32_t score_before = Score(sector);
	Process(sector, sector);

	bool unscrambled = BCDMSF_to_LBA(((Sector *)sector)->header.address) == lba;
	if(!unscrambled)
	{
		uint32_t score_after = Score(sector);
		if(score_after > score_before)
			unscrambled = true;
		else
			Process(sector, sector);
	}

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


uint32_t Scrambler::Score(const uint8_t *sector) const
{
	uint32_t score = 0;

	for(uint16_t i = sizeof(CD_DATA_SYNC); i < CD_DATA_SIZE - 1; ++i)
		if(sector[i] == sector[i + 1])
			++score;

	return score;
}


void Scrambler::Process(uint8_t *sector_out, const uint8_t *sector_in) const
{
	for(uint32_t i = 0; i < CD_DATA_SIZE; ++i)
		sector_out[i] = sector_in[i] ^ _table[i];
}

}
