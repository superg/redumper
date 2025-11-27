#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

import cd.cd;
import cd.edc;
import cd.scrambler;
import crc.crc;
import crc.crc16_gsm;
import crc.crc32;
import range;
import utils.file_io;
import utils.misc;
import utils.strings;



using namespace gpsxre;



bool test_scale()
{
    bool success = true;

    std::vector<std::pair<std::pair<int32_t, uint32_t>, int32_t>> cases = {
        { { 0, 16 },   0  },
        { { 1, 16 },   1  },
        { { 15, 16 },  1  },
        { { 16, 16 },  1  },
        { { 17, 16 },  2  },
        { { 20, 16 },  2  },
        { { 32, 16 },  2  },
        { { 33, 16 },  3  },

        { { -1, 16 },  -1 },
        { { -15, 16 }, -1 },
        { { -16, 16 }, -1 },
        { { -17, 16 }, -2 },
        { { -20, 16 }, -2 },
        { { -32, 16 }, -2 },
        { { -33, 16 }, -3 }
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


bool test_lbamsf()
{
    bool success = true;

    std::vector<std::pair<MSF, int32_t>> cases = {
        { { 0, 0, 0 },     -150   },
        { { 0, 0, 1 },     -149   },
        { { 0, 0, 73 },    -77    },
        { { 0, 0, 74 },    -76    },
        { { 0, 1, 0 },     -75    },
        { { 0, 2, 0 },     0      },
        { { 79, 59, 74 },  359849 },
        { { 80, 0, 0 },    359850 },
        { { 89, 59, 74 },  404849 },
        { { 90, 0, 0 },    404850 },
        { { 90, 0, 1 },    404851 },
        { { 90, 1, 0 },    404925 },
        { { 99, 59, 74 },  449849 },
        { { 100, 0, 0 },   449850 },
        { { 160, 0, 0 },   -45150 },
        { { 160, 0, 1 },   -45149 },
        { { 160, 1, 0 },   -45075 },
        { { 169, 59, 74 }, -151   },
        // LEGACY:
        //        {{90,  0,  0}, -45150},
        //        {{90,  0,  1}, -45149},
        //        {{90,  1,  0}, -45075},
        //        {{99, 59, 74},   -151}
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


bool test_bcd()
{
    bool success = true;

    std::vector<std::pair<uint8_t, uint8_t>> cases = {
        { 0x00, 0   },
        { 0x01, 1   },
        { 0x09, 9   },
        { 0x10, 10  },
        { 0x11, 11  },
        { 0x15, 15  },
        { 0x19, 19  },
        { 0x55, 55  },
        { 0x99, 99  },
        { 0xA0, 100 },
        { 0xA1, 101 },
        { 0xA6, 106 },
        { 0xA9, 109 },
        { 0xB0, 110 },
        { 0xF0, 150 },
        { 0xF9, 159 }
    };

    for(size_t i = 0; i < cases.size(); ++i)
    {
        std::cout << std::format("bcd_decode: {:02X} -> {:3}... ", cases[i].first, cases[i].second) << std::flush;
        auto value = bcd_decode(cases[i].first);
        if(value == cases[i].second)
            std::cout << "success";
        else
        {
            std::cout << std::format("failure, value: {:3}", value);
            success = false;
        }

        std::cout << std::endl;
    }

    for(size_t i = 0; i < cases.size(); ++i)
    {
        std::cout << std::format("bcd_encode: {:3} -> {:02X}... ", cases[i].second, cases[i].first) << std::flush;
        auto value = bcd_encode(cases[i].second);
        if(value == cases[i].first)
            std::cout << "success";
        else
        {
            std::cout << std::format("failure, value: {:02X}", value);
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

    // DEBUG
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
                *lba_ptr = str_to_int(tokens[1]);
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
    bool reciprocal_match = CRC<uint32_t, 0x04C11DB7, 0x12345678, 0x87654321, true, false, false>().update((uint8_t *)check_value.data(), check_value.length()).final()
                         == CRC<uint32_t, 0x04C11DB7, 0x12345678, 0x87654321, true, false, true>().update((uint8_t *)check_value.data(), check_value.length()).final();
    std::cout << std::format("CRC normal/reciprocal test: {}", reciprocal_match ? "success" : "failure") << std::endl;

    return success;
}


bool test_range()
{
    bool success = true;

    std::vector<Range<int32_t>> ranges;

    // Test 1: Insert non-overlapping ranges
    std::cout << "Test 1: Insert non-overlapping ranges... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 10, 20 });
    insert_range(ranges, Range<int32_t>{ 30, 40 });
    insert_range(ranges, Range<int32_t>{ 50, 60 });
    if(ranges.size() == 3 && ranges[0].start == 10 && ranges[0].end == 20 && ranges[1].start == 30 && ranges[1].end == 40 && ranges[2].start == 50 && ranges[2].end == 60)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << "failure" << std::endl;
        success = false;
    }

    // Test 2: Find values in ranges
    std::cout << "Test 2: Find values in ranges... " << std::flush;
    auto r1 = find_range(ranges, 15);
    auto r2 = find_range(ranges, 35);
    auto r3 = find_range(ranges, 55);
    auto r4 = find_range(ranges, 25);
    if(r1 && r1->start == 10 && r1->end == 20 && r2 && r2->start == 30 && r2->end == 40 && r3 && r3->start == 50 && r3->end == 60 && !r4)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << "failure" << std::endl;
        success = false;
    }

    // Test 3: Insert overlapping range (should merge)
    std::cout << "Test 3: Insert overlapping range (merge 10-20 and 30-40)... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 15, 35 });
    if(ranges.size() == 2 && ranges[0].start == 10 && ranges[0].end == 40 && ranges[1].start == 50 && ranges[1].end == 60)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << std::format("failure (size: {}, ranges[0]: [{}, {}])", ranges.size(), ranges.empty() ? 0 : ranges[0].start, ranges.empty() ? 0 : ranges[0].end) << std::endl;
        success = false;
    }

    // Test 4: Insert range that covers multiple ranges
    std::cout << "Test 4: Insert range covering all existing ranges... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 5, 65 });
    if(ranges.size() == 1 && ranges[0].start == 5 && ranges[0].end == 65)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << "failure" << std::endl;
        success = false;
    }

    // Test 5: Insert adjacent ranges (should merge)
    ranges.clear();
    std::cout << "Test 5: Insert adjacent ranges (should merge)... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 10, 20 });
    insert_range(ranges, Range<int32_t>{ 20, 30 });
    if(ranges.size() == 1 && ranges[0].start == 10 && ranges[0].end == 30)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << std::format("failure (size: {}, range: [{}, {}])", ranges.size(), ranges.empty() ? 0 : ranges[0].start, ranges.empty() ? 0 : ranges[0].end) << std::endl;
        success = false;
    }

    // Test 6: Insert range within existing range
    ranges.clear();
    std::cout << "Test 6: Insert range within existing range... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 10, 50 });
    insert_range(ranges, Range<int32_t>{ 20, 30 });
    if(ranges.size() == 1 && ranges[0].start == 10 && ranges[0].end == 50)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << "failure" << std::endl;
        success = false;
    }

    // Test 7: Insert at start without overlap
    ranges.clear();
    std::cout << "Test 7: Insert at start without overlap... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 20, 30 });
    insert_range(ranges, Range<int32_t>{ 40, 50 });
    insert_range(ranges, Range<int32_t>{ 5, 10 });
    if(ranges.size() == 3 && ranges[0].start == 5 && ranges[0].end == 10 && ranges[1].start == 20 && ranges[1].end == 30 && ranges[2].start == 40 && ranges[2].end == 50)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << "failure" << std::endl;
        success = false;
    }

    // Test 8: Insert at start with overlap (should merge with next)
    ranges.clear();
    std::cout << "Test 8: Insert at start with overlap (should merge with next)... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 20, 30 });
    insert_range(ranges, Range<int32_t>{ 40, 50 });
    insert_range(ranges, Range<int32_t>{ 5, 25 });
    if(ranges.size() == 2 && ranges[0].start == 5 && ranges[0].end == 30 && ranges[1].start == 40 && ranges[1].end == 50)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << std::format("failure (size: {}, ranges[0]: [{}, {}])", ranges.size(), ranges.empty() ? 0 : ranges[0].start, ranges.empty() ? 0 : ranges[0].end) << std::endl;
        success = false;
    }

    // Test 9: Insert at end without overlap
    ranges.clear();
    std::cout << "Test 9: Insert at end without overlap... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 10, 20 });
    insert_range(ranges, Range<int32_t>{ 30, 40 });
    insert_range(ranges, Range<int32_t>{ 50, 60 });
    if(ranges.size() == 3 && ranges[0].start == 10 && ranges[0].end == 20 && ranges[1].start == 30 && ranges[1].end == 40 && ranges[2].start == 50 && ranges[2].end == 60)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << "failure" << std::endl;
        success = false;
    }

    // Test 10: Insert at end with overlap (should merge with previous)
    ranges.clear();
    std::cout << "Test 10: Insert at end with overlap (should merge with previous)... " << std::flush;
    insert_range(ranges, Range<int32_t>{ 10, 20 });
    insert_range(ranges, Range<int32_t>{ 30, 40 });
    insert_range(ranges, Range<int32_t>{ 35, 60 });
    if(ranges.size() == 2 && ranges[0].start == 10 && ranges[0].end == 20 && ranges[1].start == 30 && ranges[1].end == 60)
    {
        std::cout << "success" << std::endl;
    }
    else
    {
        std::cout << std::format("failure (size: {}, ranges[1]: [{}, {}])", ranges.size(), ranges.size() < 2 ? 0 : ranges[1].start, ranges.size() < 2 ? 0 : ranges[1].end) << std::endl;
        success = false;
    }

    return success;
}


int main(int argc, char *argv[])
{
    int success = 0;

    success |= (int)!test_scale();
    std::cout << std::endl;
    success |= (int)!test_bcd();
    std::cout << std::endl;
    success |= (int)!test_lbamsf();
    std::cout << std::endl;
    success |= (int)!test_unscramble();
    std::cout << std::endl;
    success |= (int)!test_crc();
    std::cout << std::endl;
    success |= (int)!test_range();
    std::cout << std::endl;

    return success;
}
