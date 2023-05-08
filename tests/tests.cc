#include <filesystem>
#include <format>
#include <iostream>
#include <set>
#include <vector>

import cd.cd;
import cd.edc;
import cd.scrambler;
import common;
import crc.crc;
import crc.crc16_gsm;
import crc.crc32;
import file_io;



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
		std::cout << std::format("scale_up({}, {}) -> {}... ", cases[i].first.first, cases[i].first.second, cases[i].second) << std::flush;
		auto s = scale_up(cases[i].first.first, cases[i].first.second);
		if(s == cases[i].second)
			std::cout << "success";
		else
		{
			std::cout << std::format("failure, result: {}", s);
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
		std::cout << std::format("MSF_to_LBA: {:02}:{:02}:{:02} -> {:6}... ", cases[i].first.m, cases[i].first.s, cases[i].first.f, cases[i].second) << std::flush;
		auto lba = MSF_to_LBA(cases[i].first);
		if(lba == cases[i].second)
			std::cout << "success";
		else
		{
			std::cout << std::format("failure, lba: {:6}", lba);
			success = false;
		}

		std::cout << std::endl;
	}

	for(size_t i = 0; i < cases.size(); ++i)
	{
		std::cout << std::format("LBA_to_MSF: {:6} -> {:02}:{:02}:{:02}... ", cases[i].second, cases[i].first.m, cases[i].first.s, cases[i].first.f) << std::flush;
		auto msf = LBA_to_MSF(cases[i].second);
		if(msf.m == cases[i].first.m && msf.s == cases[i].first.s && msf.f == cases[i].first.f)
			std::cout << "success";
		else
		{
			std::cout << std::format("failure, msf: {:02}:{:02}:{:02}", msf.m, msf.s, msf.f);
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
		scrambler.process(sector.data(), sector.data(), 0, sector.size());
		std::ofstream ofs("unscramble/11_invalid_mode_non_zeroed_intermediate_last_byte.0.fail", std::fstream::binary);
		ofs.write((char *)sector.data(), sector.size());
	}

	std::set<std::filesystem::path> test_files;
	for(auto const &f : std::filesystem::directory_iterator("unscramble"))
		if(f.is_regular_file())
			test_files.insert(f.path());

	for(auto const &f : test_files)
	{
		std::cout << std::format("descramble: {}... ", f.filename().string()) << std::flush;

		std::vector<uint8_t> sector = read_vector(f);

		auto tokens = tokenize(f.filename().string(), ".", nullptr);
		if(tokens.size() == 3)
		{
			int32_t lba = 0;
			int32_t *lba_ptr = &lba;
			if(tokens[1] == "null")
				lba_ptr = nullptr;
			else
				*lba_ptr = stoll_strict(tokens[1]);
			bool scrambled = tokens[2] == "pass";
			bool unscrambled = scrambler.descramble(sector.data(), lba_ptr, sector.size());

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

bool test_crc()
{
	bool success = true;

	std::string check_value("123456789");

	// CRC-16/GSM
	auto crc16 = CRC16_GSM().update((uint8_t *)check_value.data(), check_value.length()).final();
	auto crc16_match = crc16 == 0xCE3C;
	std::cout << std::format("CRC-16/GSM: 0x{:04X}, {}", crc16, crc16_match ? "success" : "failure") << std::endl;
	if(!crc16_match)
		success = false;

	// CRC-32
	auto crc32 = CRC32().update((uint8_t *)check_value.data(), check_value.length()).final();
	auto crc32_match = crc32 == 0xCBF43926;
	std::cout << std::format("CRC-32: 0x{:08X}, {}", crc32, crc32_match ? "success" : "failure") << std::endl;
	if(!crc32_match)
		success = false;

	// EDC
	auto edc = EDC().update((uint8_t *)check_value.data(), check_value.length()).final();
	auto edc_match = edc == 0x6EC2EDC4;
	std::cout << std::format("EDC: 0x{:08X}, {}", edc, edc_match ? "success" : "failure") << std::endl;
	if(!edc_match)
		success = false;

	// CRC reciprocal
	bool reciprocal_match = CRC<uint32_t, 0x04C11DB7, 0x12345678, 0x87654321, true, false, false>().update((uint8_t *)check_value.data(), check_value.length()).final() ==
	                        CRC<uint32_t, 0x04C11DB7, 0x12345678, 0x87654321, true, false, true >().update((uint8_t *)check_value.data(), check_value.length()).final();
	std::cout << std::format("CRC normal/reciprocal test: {}", reciprocal_match ? "success" : "failure") << std::endl;

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
	success |= (int)!test_crc();
	std::cout << std::endl;

	return success;
}
