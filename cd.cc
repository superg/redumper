#include <cstring>
#include "cd.hh"



namespace gpsxre
{

bool is_unscrambled_data_sector(const uint8_t *sector, int32_t lba)
{
	auto s = (Sector *)sector;

	// sync check
//	if(memcmp(s->sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
//	{
//		// [CDI] can still be a data sector if zeroed
//		for(uint32_t i = 0; i < sizeof(CD_DATA_SYNC); ++i)
//			if(s->sync[i])
//				return false;
//	}

//	if(s->header.address.m >= bcd_encode(MSF_LIMIT.m) || s->header.address.s >= bcd_encode(MSF_LIMIT.s) || s->header.address.f >= bcd_encode(MSF_LIMIT.f))
//		return false;

//	if(s->header.mode > 2)
//		return false;

	// mode 0 / all zeroes
	if(s->header.mode == 0)
	{
		for(uint32_t i = 0; i < sizeof(s->mode2.user_data); ++i)
			if(s->mode2.user_data[i])
				return false;
	}
	else
	{
		// MSF
		if(BCDMSF_to_LBA(s->header.address) != lba)
		{
			// intermediate zeroes in mode 1
			if(s->header.mode == 1)
			{
				for(uint32_t i = 0; i < sizeof(s->mode1.intermediate); ++i)
					if(s->mode1.intermediate[i])
						return false;
			}
			if(s->header.mode == 2)
			{
				;
			}
			else
			{
				return false;
			}
		}
	}

	return true;
}

}
