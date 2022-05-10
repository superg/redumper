#include <format>
#include <iostream>
#include <vector>

#include "cd.hh"



bool test_cd()
{
	using namespace gpsxre;

	bool success = true;

	std::vector<std::pair<MSF, int32_t>> cases =
	{
		{{ 0,  0,  0},   -150},
		{{ 0,  0,  1},   -149},
		{{ 0,  0, 73},    -77},
		{{ 0,  0, 74},    -76},
		{{ 0,  1,  0},    -75},
		{{ 0,  2,  0},      0},
		{{79, 59, 74}, 359849},
		{{80,  0,  0}, 359850},
		{{89, 59, 74}, 404849},
		{{90,  0,  0}, -45150},
		{{90,  0,  1}, -45149},
		{{90,  1,  0}, -45075},
		{{99, 59, 74},   -151}
	};

	for(size_t i = 0; i < cases.size(); ++i)
	{
		std::cout << std::format("{:02}:{:02}:{:02} <=> {:6}", cases[i].first.m, cases[i].first.s, cases[i].first.f, cases[i].second) << std::endl;
		auto lba = MSF_to_LBA(cases[i].first);
		if(lba != cases[i].second)
		{
			std::cout << std::format("MSF_to_LBA failed, lba: {:6}", lba) << std::endl;
			success = false;
		}

		auto msf = LBA_to_MSF(cases[i].second);
		if(msf.m != cases[i].first.m || msf.s != cases[i].first.s || msf.f != cases[i].first.f)
		{
			std::cout << std::format("LBA_to_MSF failed, msf: {:02}:{:02}:{:02}", msf.m, msf.s, msf.f) << std::endl;
			success = false;
		}
	}

	return success;
}



int main(int argc, char *argv[])
{
	int success = 0;

	success |= (int)test_cd();
	
	return success;
}
