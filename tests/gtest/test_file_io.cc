#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>



import utils.file_io;

using namespace gpsxre;


struct TestFile
{
    std::filesystem::path directory = std::filesystem::temp_directory_path() / ("redumper-file-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::fstream fs;

    TestFile()
    {
        if(!std::filesystem::create_directory(directory))
            throw std::runtime_error("unable to create test directory");
    }


    ~TestFile()
    {
        fs.exceptions(std::ios::goodbit);
        fs.close();
        std::error_code error;
        std::filesystem::remove(directory / "data", error);
        std::filesystem::remove(directory, error);
    }


    void reset(const std::vector<uint8_t> &data, bool exceptions = true)
    {
        fs.exceptions(std::ios::goodbit);
        if(fs.is_open())
            fs.close();
        fs.clear();
        fs.open(directory / "data", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        fs.exceptions(std::ios::badbit | std::ios::failbit);
        if(!data.empty())
            fs.write((const char *)data.data(), data.size());
        fs.flush();
        if(!exceptions)
            fs.exceptions(std::ios::goodbit);
    }


    std::vector<uint8_t> contents()
    {
        fs.seekg(0, std::ios::end);
        std::vector<uint8_t> data((size_t)fs.tellg());
        fs.seekg(0);
        if(!data.empty())
            fs.read((char *)data.data(), data.size());
        return data;
    }
};


static std::vector<uint8_t> reference_read(const std::vector<uint8_t> &file, size_t size, int64_t start, bool fill_bit)
{
    std::vector<uint8_t> result(size, fill_bit ? 0xFF : 0);
    for(size_t bit = 0; bit < size * 8; ++bit)
    {
        int64_t position = start + (int64_t)bit;
        if(position < 0 || position >= (int64_t)file.size() * 8)
            continue;
        uint8_t mask = 0x80 >> (bit % 8);
        bool value = file[position / 8] & (0x80 >> (position % 8));
        result[bit / 8] = (result[bit / 8] & ~mask) | (value ? mask : 0);
    }
    return result;
}


static std::vector<uint8_t> reference_write(std::vector<uint8_t> file, const std::vector<uint8_t> &data, int64_t start, bool fill_bit)
{
    for(size_t bit = 0; bit < data.size() * 8; ++bit)
    {
        int64_t position = start + (int64_t)bit;
        if(position < 0)
            continue;
        if((uint64_t)position / 8 >= file.size())
            file.resize(position / 8 + 1, fill_bit ? 0xFF : 0);
        uint8_t mask = 0x80 >> (position % 8);
        bool value = data[bit / 8] & (0x80 >> (bit % 8));
        file[position / 8] = (file[position / 8] & ~mask) | (value ? mask : 0);
    }
    return file;
}


TEST(FileIOBit, matches_bit_reference)
{
    TestFile file;
    for(const auto &initial : {
            std::vector<uint8_t>{},
            std::vector<uint8_t>{ 0xA5 },
            std::vector<uint8_t>{ 0x12, 0x34, 0x56, 0x78, 0x9A }
    })
        for(uint64_t entry_size : { 1, 3 })
            for(uint64_t count : { 1, 2 })
                for(uint64_t index : { 0, 1, 10 })
                    for(int64_t offset = -52; offset <= 100; offset += 4)
                        for(bool fill_bit : { false, true })
                            for(bool exceptions : { false, true })
                            {
                                SCOPED_TRACE(::testing::Message() << "file_size=" << initial.size() << " entry_size=" << entry_size << " count=" << count << " index=" << index << " offset=" << offset
                                                                  << " fill=" << fill_bit << " exceptions=" << exceptions);
                                std::vector<uint8_t> data(entry_size * count);
                                for(size_t i = 0; i < data.size(); ++i)
                                    data[i] = (uint8_t)(0xAB + i * 37);
                                int64_t start = (int64_t)(index * entry_size * 8) - offset;
                                file.reset(initial, exceptions);
                                std::vector<uint8_t> actual(data.size());
                                read_entry_bit(file.fs, actual.data(), entry_size, index, count, offset, fill_bit);
                                ASSERT_EQ(actual, reference_read(initial, data.size(), start, fill_bit));
                                write_entry_bit(file.fs, data.data(), entry_size, index, count, offset, fill_bit);
                                auto expected = reference_write(initial, data, start, fill_bit);
                                ASSERT_EQ(file.contents(), expected);
                                read_entry_bit(file.fs, actual.data(), entry_size, index, count, offset, fill_bit);
                                ASSERT_EQ(actual, reference_read(expected, data.size(), start, fill_bit));
                            }
}


TEST(FileIOBit, adjacent_and_overlapping_writes)
{
    TestFile file;
    const std::vector<uint8_t> first{ 0xAB, 0xCD };
    const std::vector<uint8_t> second{ 0x12, 0x34 };
    for(bool fill_bit : { false, true })
        for(int64_t offset : { -12, -4, 4, 12 })
            for(bool reverse : { false, true })
            {
                file.reset({});
                std::vector<uint8_t> expected;
                for(uint64_t i = 0; i < 2; ++i)
                {
                    uint64_t index = reverse ? 1 - i : i;
                    const auto &data = index ? second : first;
                    write_entry_bit(file.fs, data.data(), 2, index, 1, offset, fill_bit);
                    expected = reference_write(expected, data, (int64_t)(index * 16) - offset, fill_bit);
                    ASSERT_EQ(file.contents(), expected);
                }
                write_entry_bit(file.fs, second.data(), 1, 1, 2, offset, !fill_bit);
                expected = reference_write(expected, second, 8 - offset, !fill_bit);
                EXPECT_EQ(file.contents(), expected);
            }
}


TEST(FileIOBit, empty_invalid_and_extreme_ranges)
{
    std::fstream closed;
    for(int64_t offset : { -4, 0, 4 })
    {
        EXPECT_NO_THROW(write_entry_bit(closed, nullptr, 1, UINT64_MAX, 0, offset, false));
        EXPECT_NO_THROW(read_entry_bit(closed, nullptr, 1, UINT64_MAX, 0, offset, false));
        EXPECT_NO_THROW(write_entry_bit(closed, nullptr, 0, UINT64_MAX, UINT64_MAX, offset, false));
        EXPECT_NO_THROW(read_entry_bit(closed, nullptr, 0, UINT64_MAX, UINT64_MAX, offset, false));
    }
    TestFile file;
    file.reset({ 0xA5 });
    uint8_t data = 0xAB;
    for(int64_t offset : { -7, -1, 1, 6 })
    {
        EXPECT_THROW(write_entry_bit(file.fs, &data, 1, 0, 1, offset, false), std::runtime_error);
        EXPECT_THROW(read_entry_bit(file.fs, &data, 1, 0, 1, offset, false), std::runtime_error);
    }
    for(auto range : {
            std::vector<uint64_t>{ UINT64_MAX, 0,          2          },
            std::vector<uint64_t>{ 2,          UINT64_MAX, 1          },
            std::vector<uint64_t>{ 1,          INT64_MAX,  1          },
            std::vector<uint64_t>{ 1,          0,          UINT64_MAX }
    })
    {
        EXPECT_THROW(write_entry_bit(file.fs, &data, range[0], range[1], range[2], -4, false), std::runtime_error);
        EXPECT_THROW(read_entry_bit(file.fs, &data, range[0], range[1], range[2], -4, false), std::runtime_error);
    }
    EXPECT_THROW(write_entry_bit(file.fs, &data, 1, INT64_MAX, 1, INT64_MIN, false), std::runtime_error);
    EXPECT_THROW(read_entry_bit(file.fs, &data, 1, INT64_MAX, 1, INT64_MIN, false), std::runtime_error);
    EXPECT_EQ(file.contents(), (std::vector<uint8_t>{ 0xA5 }));
    EXPECT_NO_THROW(write_entry_bit(file.fs, &data, 1, 0, 1, INT64_MAX - 3, false));
    read_entry_bit(file.fs, &data, 1, 0, 1, INT64_MAX - 3, true);
    EXPECT_EQ(data, 0xFF);
    read_entry_bit(file.fs, &data, 1, 0, 1, INT64_MIN, false);
    EXPECT_EQ(data, 0);
    EXPECT_EQ(file.contents(), (std::vector<uint8_t>{ 0xA5 }));
    EXPECT_THROW(write_entry_bit(closed, &data, 1, 0, 1, 0, false), std::runtime_error);
    closed.clear();
    EXPECT_THROW(read_entry_bit(closed, &data, 1, 0, 1, 0, false), std::runtime_error);
}
