#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <set>
#include <vector>

#include "cd.hh"
#include "common.hh"
#include "file_io.hh"
#include "scrambler.hh"



using namespace gpsxre;



bool test_scale()
{
	bool success = true;

	std::vector<std::pair<std::pair<int32_t, uint32_t>, int32_t>> cases =
	{
		{{ 0, 16}, 0},
		{{ 1, 16}, 1},
		{{15, 16}, 1},
		{{16, 16}, 1},
		{{17, 16}, 2},
		{{20, 16}, 2},
		{{32, 16}, 2},
		{{33, 16}, 3},

		{{ -1, 16}, -1},
		{{-15, 16}, -1},
		{{-16, 16}, -1},
		{{-17, 16}, -2},
		{{-20, 16}, -2},
		{{-32, 16}, -2},
		{{-33, 16}, -3}
	};

	for(size_t i = 0; i < cases.size(); ++i)
	{
		std::cout << fmt::format("scale_up({}, {}) -> {}... ", cases[i].first.first, cases[i].first.second, cases[i].second) << std::flush;
		auto s = scale(cases[i].first.first, cases[i].first.second);
		if(s == cases[i].second)
			std::cout << "success";
		else
		{
			std::cout << fmt::format("failure, result: {}", s);
			success = false;
		}

		std::cout << std::endl;
	}

	return success;
}


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

	//DEBUG
	if(0)
	{
		std::vector<uint8_t> sector = read_vector("unscramble/11_invalid_mode_non_zeroed_intermediate_last_byte.uns.0.fail");
		scrambler.Process(sector.data(), sector.data(), sector.size());
		std::ofstream ofs("unscramble/11_invalid_mode_non_zeroed_intermediate_last_byte.0.fail", std::fstream::binary);
		ofs.write((char *)sector.data(), sector.size());
	}

	std::set<std::filesystem::path> test_files;
	for(auto const &f : std::filesystem::directory_iterator("unscramble"))
    	if(f.is_regular_file())
			test_files.insert(f.path());

	for(auto const &f : test_files)
	{
		std::cout << fmt::format("descramble: {}... ", f.filename().string()) << std::flush;

		std::vector<uint8_t> sector = read_vector(f);

		auto tokens = tokenize(f.filename().string(), ".", nullptr);
		if(tokens.size() == 3)
		{
			int32_t lba = 0;
			int32_t *lba_ptr = &lba;
			if(tokens[1] == "null")
				lba_ptr = nullptr;
			else
				*lba_ptr = std::stol(tokens[1]);
			bool scrambled = tokens[2] == "pass";
			bool unscrambled = scrambler.Descramble(sector.data(), lba_ptr, sector.size());

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

	return success;
}


bool test_unscramble_dic()
{
	bool success = true;

	Scrambler scrambler;

	std::set<std::filesystem::path> test_files;
	for(auto const &f : std::filesystem::directory_iterator("unscramble/dic"))
    	if(f.is_regular_file())
			test_files.insert(f.path());

	for(auto const &f : test_files)
	{
		std::cout << fmt::format("descramble DIC: {}... ", f.filename().string()) << std::flush;

		std::vector<uint8_t> sector = read_vector(f);

		auto tokens = tokenize(f.filename().string(), ".", nullptr);
		if(tokens.size() == 3)
		{
			int32_t lba = 0;
			int32_t *lba_ptr = &lba;
			if(tokens[1] == "null")
				lba_ptr = nullptr;
			else
				*lba_ptr = std::stol(tokens[1]);
			bool scrambled = tokens[2] == "pass";
			bool unscrambled = scrambler.DescrambleDIC(sector.data(), lba_ptr, sector.size());

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

	return success;
}



int main(int argc, char *argv[])
{
	int success = 0;

	success |= (int)!test_scale();
	std::cout << std::endl;
	success |= (int)!test_cd();
	std::cout << std::endl;
	success |= (int)!test_unscramble();
	std::cout << std::endl;
	success |= (int)!test_unscramble_dic();
	std::cout << std::endl;

	return success;
}
