#pragma once



#include <cstdint>
#include "cd.hh"



namespace gpsxre
{

class Scrambler
{
public:
	Scrambler();
	bool Unscramble(uint8_t *sector, int32_t lba) const;
	void Process(uint8_t *sector_out, const uint8_t *sector_in) const;

private:
	uint8_t _table[CD_DATA_SIZE];

	void GenerateTable();
	uint32_t Score(const uint8_t *sector) const;
};

}
