#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <vector>

#include "cd.hh"
#include "common.hh"
#include "file_io.hh"
#include "scrambler.hh"



using namespace gpsxre;



bool test_cd()
{
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
		std::cout << fmt::format("MSF_to_LBA: {:02}:{:02}:{:02} -> {:6}... ", cases[i].first.m, cases[i].first.s, cases[i].first.f, cases[i].second) << std::flush;
		auto lba = MSF_to_LBA(cases[i].first);
		if(lba == cases[i].second)
			std::cout << "success";
		else
		{
			std::cout << fmt::format("failure, lba: {:6}", lba);
			success = false;
		}

		std::cout << std::endl;
	}

	for(size_t i = 0; i < cases.size(); ++i)
	{
		std::cout << fmt::format("LBA_to_MSF: {:6} -> {:02}:{:02}:{:02}... ", cases[i].second, cases[i].first.m, cases[i].first.s, cases[i].first.f) << std::flush;
		auto msf = LBA_to_MSF(cases[i].second);
		if(msf.m == cases[i].first.m && msf.s == cases[i].first.s && msf.f == cases[i].first.f)
			std::cout << "success";
		else
		{
			std::cout << fmt::format("failure, msf: {:02}:{:02}:{:02}", msf.m, msf.s, msf.f);
			success = false;
		}

		std::cout << std::endl;
	}

	return success;
}


bool test_unscramble()
{
	bool success = true;

	Scrambler scrambler;

	for(auto const &f : std::filesystem::directory_iterator("unscramble"))
	{
    	if(f.is_regular_file())
		{
        	std::cout << fmt::format("descramble: {}... ", f.path().filename().string()) << std::flush;

			std::vector<uint8_t> sector = read_vector(f.path());

			//DEBUG
			// scramble
			if(0)
			{
				if(f.path().extension() == ".fail")
				{
					auto output_path(f.path());
					output_path.replace_extension();
					output_path += ".pass";
					if(!std::filesystem::exists(output_path))
					{
						std::vector<uint8_t> sector_scrambled(sector);
						scrambler.Process(sector_scrambled.data(), sector_scrambled.data(), sector_scrambled.size());
						std::ofstream ofs(output_path, std::fstream::binary);
						ofs.write((char *)sector_scrambled.data(), sector_scrambled.size());
					}
				}
			}

			auto tokens = tokenize(f.path().filename().string(), ".", nullptr);
			if(tokens.size() == 4)
			{
				int32_t lba = std::stol(tokens[2]);
				bool scrambled = tokens[3] == "pass";
				bool unscrambled = scrambler.Descramble(sector.data(), &lba, sector.size());

				if(unscrambled == scrambled)
					std::cout << "success";
				else
				{
					std::cout << "failure";
					success = false;
				}
			}

			std::cout << std::endl;
    	}
	}

	return success;
}



int main(int argc, char *argv[])
{
	int success = 0;

	success |= (int)!test_cd();
	success |= (int)!test_unscramble();

	return success;
}
