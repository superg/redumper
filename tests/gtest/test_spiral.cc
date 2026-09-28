#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import cd.cd;
import cd.common;
import cd.spiral;
import cd.subcode;
import common;
import crc.crc16_gsm;
import options;
import scsi.mmc;
import scsi.sptd;
import utils.endian;

using namespace gpsxre;


namespace
{

std::vector<uint8_t> frame(uint8_t marker, uint8_t track, int32_t q_lba, bool c2 = false)
{
    std::vector<uint8_t> data(CD_RAW_DATA_SIZE);
    std::fill(data.begin(), data.begin() + CD_DATA_SIZE, marker);
    if(c2)
        data[CD_DATA_SIZE] = 0x80;

    ChannelQ q = {};
    q.adr = 1;
    q.mode1.tno = track;
    q.mode1.a_msf = LBA_to_BCDMSF(q_lba);
    q.crc = endian_swap(CRC16_GSM().update(q.raw, sizeof(q.raw)).final());

    auto bytes = (const uint8_t *)&q;
    for(uint32_t i = 0; i < CD_SUBCODE_SIZE; ++i)
        if(bytes[i / 8] & (1 << (7 - i % 8)))
            data[CD_DATA_SIZE + CD_C2_SIZE + i] |= 0x40;

    return data;
}


std::vector<uint8_t> batch(uint8_t marker, uint8_t track, int32_t first_q_lba)
{
    std::vector<uint8_t> data;
    for(uint32_t i = 0; i < 16; ++i)
    {
        auto sector = frame(marker, track, first_q_lba + i);
        data.insert(data.end(), sector.begin(), sector.end());
    }
    return data;
}


struct Reply
{
    SpiralDrive_Operation operation;
    int32_t value;
    std::vector<uint8_t> data;
    uint32_t transferred;
    uint8_t status;
};


struct FakeTransport
{
    std::vector<Reply> replies;
    size_t next = 0;

    std::pair<SPTD::Status, uint32_t> operator()(uint8_t *buffer, uint32_t size, SpiralDrive_Operation operation, uint16_t timeout, int32_t value)
    {
        if(next >= replies.size())
            throw std::runtime_error("unexpected SpiralDrive command");
        const auto &reply = replies[next++];
        if(operation != reply.operation || value != reply.value || size != reply.data.size() || timeout != 5000)
            throw std::runtime_error("unexpected SpiralDrive CDB or transfer size");
        if(size)
            std::memcpy(buffer, reply.data.data(), size);
        return {
            SPTD::Status{ reply.status, 0, 0, 0 },
            reply.transferred
        };
    }
};


Reply read_reply(int32_t count, std::vector<uint8_t> data, uint8_t status = 0, uint32_t transferred = UINT32_MAX)
{
    uint32_t size = (uint32_t)data.size();
    return { SpiralDrive_Operation::READ, count, std::move(data), transferred == UINT32_MAX ? size : transferred, status };
}


Reply seek_reply(uint8_t status = 0)
{
    return { SpiralDrive_Operation::SEEK, -200, {}, 0, status };
}


class SpiralDump : public ::testing::Test
{
protected:
    std::filesystem::path directory;

    void SetUp() override
    {
        directory = std::filesystem::current_path() / "spiral_test_files" / ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(directory);
    }

    Options options()
    {
        const char *argv[] = { "redumper" };
        Options value(1, argv);
        value.image_path = directory.string();
        value.image_name = "image";
        return value;
    }

    Context context()
    {
        Context value = {};
        value.disc_type = DiscType::CD;
        value.drive_config.vendor_specific = "SpiralDrive v1.0.0";
        return value;
    }

    int run(FakeTransport &transport, Context &ctx, Options &opts)
    {
        return redumper_dump_spiral_with_transport(ctx, opts,
            [&](uint8_t *buffer, uint32_t size, SpiralDrive_Operation operation, uint16_t timeout, int32_t value) { return transport(buffer, size, operation, timeout, value); });
    }

    std::filesystem::path path(const char *extension)
    {
        return directory / (std::string("image") + extension);
    }

    uint8_t byte_at(const char *extension, int32_t lba, uint32_t entry_size, uint32_t byte_offset = 0)
    {
        std::ifstream fs(path(extension), std::ios::binary);
        EXPECT_TRUE(fs.is_open());
        fs.seekg((uint64_t)(lba - LBA_START) * entry_size + byte_offset);
        char byte = 0;
        fs.get(byte);
        EXPECT_TRUE(fs.good());
        return (uint8_t)byte;
    }
};


TEST_F(SpiralDump, SearchesAnchorsAndHonorsExclusiveEnd)
{
    auto opts = options();
    opts.lba_end = std::make_unique<int>(-147);
    auto ctx = context();

    std::vector<uint8_t> sectors;
    for(uint32_t i = 0; i < 16; ++i)
    {
        auto sector = i == 0 ? frame(0x12, 0, -3000) : i == 1 ? frame(0x13, 1, -149, true) : frame(0x14, 1, 100 + i);
        sectors.insert(sectors.end(), sector.begin(), sector.end());
    }

    FakeTransport transport{
        {
         read_reply(1, frame(0x10, 1, 100)),
         seek_reply(),
         read_reply(1, frame(0x11, 0, -3000)),
         read_reply(16, sectors),
         }
    };

    EXPECT_EQ(run(transport, ctx, opts), 1);
    EXPECT_EQ(transport.next, 4);
    EXPECT_EQ(byte_at(".scram", -151, CD_DATA_SIZE), 0x11);
    EXPECT_EQ(byte_at(".scram", -150, CD_DATA_SIZE), 0x12);
    EXPECT_EQ(byte_at(".scram", -149, CD_DATA_SIZE), 0x13);
    EXPECT_EQ(byte_at(".scram", -148, CD_DATA_SIZE), 0x14);
    EXPECT_EQ(byte_at(".subcode", -149, CD_SUBCODE_SIZE), sectors[CD_RAW_DATA_SIZE + CD_DATA_SIZE + CD_C2_SIZE]);
    EXPECT_EQ(byte_at(".state", -149, CD_DATA_SIZE_SAMPLES), (uint8_t)State::ERROR_C2);
    EXPECT_EQ(byte_at(".state", -149, CD_DATA_SIZE_SAMPLES, 1), (uint8_t)State::SUCCESS);
    EXPECT_EQ(std::filesystem::file_size(path(".scram")), (uint64_t)(-147 - LBA_START) * CD_DATA_SIZE);
    EXPECT_EQ(std::filesystem::file_size(path(".subcode")), (uint64_t)(-147 - LBA_START) * CD_SUBCODE_SIZE);
    EXPECT_EQ(std::filesystem::file_size(path(".state")), (uint64_t)(-147 - LBA_START) * CD_DATA_SIZE_SAMPLES);
}


TEST_F(SpiralDump, DiscardsEntireScsiErrorResponse)
{
    auto opts = options();
    auto ctx = context();
    auto rejected = batch(0xee, 1, -133);
    FakeTransport transport{
        {
         read_reply(1, frame(0x21, 0, -3000)),
         read_reply(16, batch(0x22, 1, -149)),
         read_reply(16, rejected, 2, 5 * CD_RAW_DATA_SIZE),
         }
    };

    EXPECT_EQ(run(transport, ctx, opts), 0);
    EXPECT_EQ(transport.next, 3);
    EXPECT_EQ(byte_at(".scram", -150, CD_DATA_SIZE), 0x21);
    EXPECT_EQ(byte_at(".scram", -134, CD_DATA_SIZE), 0x22);
    EXPECT_EQ(std::filesystem::file_size(path(".scram")), (uint64_t)(-133 - LBA_START) * CD_DATA_SIZE);
}


TEST_F(SpiralDump, RejectsGoodShortTransferWithoutWritingItsBatch)
{
    auto opts = options();
    auto ctx = context();
    auto short_data = batch(0xee, 1, -133);
    FakeTransport transport{
        {
         read_reply(1, frame(0x21, 0, -3000)),
         read_reply(16, batch(0x22, 1, -149)),
         read_reply(16, short_data, 0, (uint32_t)short_data.size() - 1),
         }
    };

    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(std::filesystem::file_size(path(".scram")), (uint64_t)(-133 - LBA_START) * CD_DATA_SIZE);
}


TEST_F(SpiralDump, ThrowsWithoutAnchorAndCreatesNoDumpFiles)
{
    auto opts = options();
    auto ctx = context();
    FakeTransport transport{
        {
         read_reply(1, frame(0x31, 0, -3000)),
         read_reply(16, batch(0x32, 0, -3000)),
         read_reply(16, batch(0xee, 1, -149), 2),
         }
    };

    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(path(".scram")));
    EXPECT_FALSE(std::filesystem::exists(path(".state")));
    EXPECT_FALSE(std::filesystem::exists(path(".subcode")));
}


TEST_F(SpiralDump, FailsOnSeekErrorAndRejectsWrongFirmware)
{
    auto opts = options();
    auto ctx = context();
    FakeTransport transport{
        {
         read_reply(1, frame(0x41, 1, 100)),
         seek_reply(2),
         }
    };

    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(transport.next, 2);
    ctx.drive_config.vendor_specific = "stock firmware";
    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(transport.next, 2);
}


TEST_F(SpiralDump, RejectsUnsupportedOptionsBeforeReading)
{
    auto opts = options();
    auto ctx = context();
    FakeTransport transport;

    opts.lba_start = std::make_unique<int>(0);
    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    opts.lba_start.reset();
    opts.force_omnidrive = true;
    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(transport.next, 0);
}

}
