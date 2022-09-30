#include <cstring>
#include "cd.hh"



namespace gpsxre
{

MSF BCDMSF_to_MSF(MSF bcdmsf)
{
	MSF msf;
	msf.m = bcd_decode(bcdmsf.m);
	msf.s = bcd_decode(bcdmsf.s);
	msf.f = bcd_decode(bcdmsf.f);

	return msf;
}


MSF MSF_to_BCDMSF(MSF msf)
{
	MSF bcdmsf;
	bcdmsf.m = bcd_encode(msf.m);
	bcdmsf.s = bcd_encode(msf.s);
	bcdmsf.f = bcd_encode(msf.f);

	return bcdmsf;
}


int32_t MSF_to_LBA(MSF msf)
{
	return MSF_LIMIT.f * (MSF_LIMIT.s * msf.m + msf.s) + msf.f + MSF_LBA_SHIFT - (msf.m >= MSF_MINUTES_WRAP ? LBA_LIMIT : 0);
}


MSF LBA_to_MSF(int32_t lba)
{
	MSF msf;

	lba -= MSF_LBA_SHIFT;

	if(lba < 0)
		lba += LBA_LIMIT;

	msf.f = lba % MSF_LIMIT.f;
	lba /= MSF_LIMIT.f;
	msf.s = lba % MSF_LIMIT.s;
	lba /= MSF_LIMIT.s;
	msf.m = lba;

	return msf;
}


int32_t BCDMSF_to_LBA(MSF bcdmsf)
{
	return MSF_to_LBA(BCDMSF_to_MSF(bcdmsf));
}


MSF LBA_to_BCDMSF(int32_t lba)
{
	return MSF_to_BCDMSF(LBA_to_MSF(lba));
}


bool MSF_valid(MSF msf)
{
	return msf.m < MSF_LIMIT.m && msf.s < MSF_LIMIT.s && msf.f < MSF_LIMIT.f;
}


bool BCDMSF_valid(MSF bcdmsf)
{
	return MSF_valid(BCDMSF_to_MSF(bcdmsf));
}

}
