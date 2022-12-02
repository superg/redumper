// This is a C++ rewrite of ECC/EDC routines by Neill Corlett

#pragma once



#include <cstdint>
#include "cd.hh"



namespace gpsxre
{

class ECC
{
public:
	ECC();

	Sector::ECC Generate(const uint8_t *data);
	Sector::ECC Generate(Sector &sector, bool zero_address);

private:
	static const uint32_t _LUT_SIZE = 0x100;
	static uint8_t _F_LUT[_LUT_SIZE];
	static uint8_t _B_LUT[_LUT_SIZE];
	static bool _initialized;

	void InitLUTs();
	void ComputeBlock(uint8_t *parity, const uint8_t *data, uint32_t major_count, uint32_t minor_count, uint32_t major_mult, uint32_t minor_inc);
};

class EDC
{
public:
	EDC();

	uint32_t ComputeBlock(uint32_t edc, const uint8_t *data, uint32_t size);

private:
	static const uint32_t _LUT_SIZE = 0x100;
	static uint32_t _LUT[_LUT_SIZE];
	static bool _initialized;

	void InitLUTs();
};

}
