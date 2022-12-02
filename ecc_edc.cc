/*
 * This is a C++ rewrite of ECC/EDC routines by Neill Corlett
 * Original header is preserved:
 *
 * Copyright (C) 2002 Neill Corlett
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <algorithm>
#include "ecc_edc.hh"



namespace gpsxre
{

uint8_t ECC::_F_LUT[_LUT_SIZE];
uint8_t ECC::_B_LUT[_LUT_SIZE];
bool ECC::_initialized(false);


ECC::ECC()
{
	if(!_initialized)
	{
		InitLUTs();
		_initialized = true;
	}
}


Sector::ECC ECC::Generate(Sector &sector, bool zero_address)
{
	Sector::ECC ecc;

	// Save the address and zero it out
	Sector::Header header;
	if(zero_address)
	{
		header = sector.header;
		std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0);
	}

	// Compute ECC P code
	ComputeBlock(ecc.p_parity, (uint8_t *)&sector.header, 86, 24, 2, 86);

	// Compute ECC Q code
	ComputeBlock(ecc.q_parity, (uint8_t *)&sector.header, 52, 43, 86, 88);

	// Restore the address
	if(zero_address)
		sector.header = header;

	return ecc;
}


Sector::ECC ECC::Generate(const uint8_t *data)
{
	Sector::ECC ecc;

	// Compute ECC P code
	ComputeBlock(ecc.p_parity, data, 86, 24, 2, 86);

	// Compute ECC Q code
	ComputeBlock(ecc.q_parity, data, 52, 43, 86, 88);

	return ecc;
}


void ECC::InitLUTs()
{
	for(uint32_t i = 0; i < _LUT_SIZE; ++i)
	{
		_F_LUT[i] = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
		_B_LUT[i ^ _F_LUT[i]] = i;
	}
}


// Compute ECC for a block (can do either P or Q)
void ECC::ComputeBlock(uint8_t *parity, const uint8_t *data, uint32_t major_count, uint32_t minor_count, uint32_t major_mult, uint32_t minor_inc)
{
	uint32_t size = major_count * minor_count;
	for(uint32_t major = 0; major < major_count; ++major)
	{
		uint32_t index = (major >> 1) * major_mult + (major & 1);

		uint8_t ecc_a = 0;
		uint8_t ecc_b = 0;
		for(uint32_t minor = 0; minor < minor_count; ++minor)
		{
			uint8_t temp = data[index];
			index += minor_inc;
			if(index >= size)
				index -= size;
			ecc_a ^= temp;
			ecc_b ^= temp;
			ecc_a = _F_LUT[ecc_a];
		}

		parity[major] = _B_LUT[_F_LUT[ecc_a] ^ ecc_b];
		parity[major + major_count] = parity[major] ^ ecc_b;
	}
}


uint32_t EDC::_LUT[_LUT_SIZE];
bool EDC::_initialized(false);


EDC::EDC()
{
	if(!_initialized)
	{
		InitLUTs();
		_initialized = true;
	}
}


uint32_t EDC::ComputeBlock(uint32_t edc, const uint8_t *data, uint32_t size)
{
	while(size--)
		edc = edc >> 8 ^ _LUT[(edc ^ *data++) & 0xFF];
	return edc;
}


void EDC::InitLUTs()
{
	for(uint32_t i = 0; i < _LUT_SIZE; ++i)
	{
		uint32_t edc = i;
		for(uint32_t j = 0; j < 8; ++j)
			edc = edc >> 1 ^ (edc & 1 ? 0xD8018001 : 0);
		_LUT[i] = edc;
	}
}

}
