#include <cstdint>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <set>
#include <vector>

import cd.cd;
import cd.cdrom;
import cd.scrambler;
import utils.file_io;
import utils.strings;

using namespace gpsxre;


TEST(Unscramble, AllSamples)
{
    const std::filesystem::path data_dir = "unscramble";
    ASSERT_TRUE(std::filesystem::exists(data_dir) && std::filesystem::is_directory(data_dir))
        << "unscramble/ data directory not found relative to working directory " << std::filesystem::current_path();

    Scrambler scrambler;

    std::set<std::filesystem::path> test_files;
    for(auto const &f : std::filesystem::directory_iterator(data_dir))
        if(f.is_regular_file())
            test_files.insert(f.path());

    for(auto const &f : test_files)
    {
        SCOPED_TRACE(f.filename().string());

        std::vector<uint8_t> sector = read_vector(f);

        auto tokens = tokenize(f.filename().string(), ".", nullptr);
        ASSERT_EQ(tokens.size(), 3u);

        int32_t lba = 0;
        int32_t *lba_ptr = &lba;
        if(tokens[1] == "null")
            lba_ptr = nullptr;
        else
            *lba_ptr = str_to_int(tokens[1]);

        bool expected_unscrambled = tokens[2] == "pass";
        bool unscrambled = scrambler.descramble(sector.data(), lba_ptr, sector.size());

        EXPECT_EQ(unscrambled, expected_unscrambled);
    }

    EXPECT_GT(test_files.size(), 0u);
}


TEST(ScramblerProcess, Invertibility)
{
    std::vector<uint8_t> original(CD_DATA_SIZE);
    for(uint32_t i = 0; i < CD_DATA_SIZE; ++i)
        original[i] = (uint8_t)(i * 31 + 7);

    std::vector<uint8_t> buffer = original;

    Scrambler::process(buffer.data(), buffer.data(), 0, CD_DATA_SIZE);
    EXPECT_NE(buffer, original);

    Scrambler::process(buffer.data(), buffer.data(), 0, CD_DATA_SIZE);
    EXPECT_EQ(buffer, original);
}


TEST(ScramblerDescramble, ZeroSector)
{
    Scrambler scrambler;

    std::vector<uint8_t> sector(CD_DATA_SIZE, 0);
    int32_t lba = 0;

    EXPECT_FALSE(scrambler.descramble(sector.data(), &lba, sector.size()));

    for(auto b : sector)
        EXPECT_EQ(b, 0);
}


TEST(ScramblerDescramble, NullLbaSyncOnly)
{
    Scrambler scrambler;

    std::vector<uint8_t> sector(CD_DATA_SIZE, 0);
    std::memcpy(sector.data(), CD_DATA_SYNC, sizeof(CD_DATA_SYNC));
    sector[12] = 0x00;
    sector[13] = 0x02;
    sector[14] = 0x00;
    sector[15] = 0x01;

    std::vector<uint8_t> reference = sector;

    Scrambler::process(sector.data(), sector.data(), 0, CD_DATA_SIZE);

    EXPECT_TRUE(scrambler.descramble(sector.data(), nullptr, CD_DATA_SIZE));
    EXPECT_EQ(sector, reference);
}
