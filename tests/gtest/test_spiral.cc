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
    return { SpiralDrive_Operation::SEEK, 0, {}, 0, status };
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


TEST_F(SpiralDump, StartsAtCurrentPositionAnchorsAndHonorsExclusiveEnd)
{
    auto opts = options();
    opts.lba_end = std::make_unique<int>(-147);
    auto ctx = context();

    std::vector<uint8_t> sectors;
    for(uint32_t i = 0; i < 16; ++i)
    {
        auto sector = i == 0 ? frame(0x11, 0, -3000) : i == 1 ? frame(0x12, 0, -3000) : i == 2 ? frame(0x13, 1, -149, true) : frame(0x14, 1, 100 + i);
        sectors.insert(sectors.end(), sector.begin(), sector.end());
    }

    FakeTransport transport{
        {
         seek_reply(),
         read_reply(16, sectors),
         }
    };

    EXPECT_EQ(run(transport, ctx, opts), 1);
    EXPECT_EQ(transport.next, 2);
    EXPECT_EQ(byte_at(".scram", -151, CD_DATA_SIZE), 0x11);
    EXPECT_EQ(byte_at(".scram", -150, CD_DATA_SIZE), 0x12);
    EXPECT_EQ(byte_at(".scram", -149, CD_DATA_SIZE), 0x13);
    EXPECT_EQ(byte_at(".scram", -148, CD_DATA_SIZE), 0x14);
    EXPECT_EQ(byte_at(".subcode", -149, CD_SUBCODE_SIZE), sectors[2 * CD_RAW_DATA_SIZE + CD_DATA_SIZE + CD_C2_SIZE]);
    EXPECT_EQ(byte_at(".state", -149, CD_DATA_SIZE_SAMPLES), (uint8_t)State::ERROR_C2);
    EXPECT_EQ(byte_at(".state", -149, CD_DATA_SIZE_SAMPLES, 1), (uint8_t)State::SUCCESS);
    EXPECT_EQ(std::filesystem::file_size(path(".scram")), (uint64_t)(-147 - LBA_START) * CD_DATA_SIZE);
    EXPECT_EQ(std::filesystem::file_size(path(".subcode")), (uint64_t)(-147 - LBA_START) * CD_SUBCODE_SIZE);
    EXPECT_EQ(std::filesystem::file_size(path(".state")), (uint64_t)(-147 - LBA_START) * CD_DATA_SIZE_SAMPLES);
}


TEST_F(SpiralDump, CanAnchorAtCurrentLeadoutPosition)
{
    auto opts = options();
    opts.lba_end = std::make_unique<int>(216);
    auto ctx = context();
    FakeTransport transport{
        {
         seek_reply(),
         read_reply(16, batch(0x61, CD_LEADOUT_TRACK_NUMBER, 200)),
         }
    };

    EXPECT_EQ(run(transport, ctx, opts), 0);
    EXPECT_EQ(transport.next, 2);
    EXPECT_EQ(byte_at(".scram", 200, CD_DATA_SIZE), 0x61);
    EXPECT_EQ(byte_at(".scram", 215, CD_DATA_SIZE), 0x61);
}


TEST_F(SpiralDump, DiscardsScsiErrorResponseAndContinuesWithNextBatch)
{
    auto opts = options();
    opts.lba_end = std::make_unique<int>(-101);
    opts.verbose = true;
    auto ctx = context();
    auto rejected = batch(0xee, 1, -133);
    FakeTransport transport{
        {
         seek_reply(),
         read_reply(16, batch(0x22, 1, -149)),
         read_reply(16, rejected, 2, 5 * CD_RAW_DATA_SIZE),
         read_reply(16, batch(0x23, 1, -117)),
         read_reply(16, batch(0x24, CD_LEADOUT_TRACK_NUMBER, -100)),
         }
    };

    ::testing::internal::CaptureStdout();
    EXPECT_EQ(run(transport, ctx, opts), 0);
    auto output = ::testing::internal::GetCapturedStdout();
    auto first_shift = output.find("subcode desync");
    ASSERT_NE(first_shift, std::string::npos);
    auto second_shift = output.find("subcode desync", first_shift + 1);
    ASSERT_NE(second_shift, std::string::npos);
    EXPECT_EQ(output.find("subcode desync", second_shift + 1), std::string::npos);
    EXPECT_NE(output.find("shift: +16"), std::string::npos);
    EXPECT_NE(output.find("shift: +17"), std::string::npos);
    EXPECT_NE(output.find("Q LBA: -85"), std::string::npos);
    EXPECT_EQ(transport.next, 5);
    EXPECT_EQ(byte_at(".scram", -149, CD_DATA_SIZE), 0x22);
    EXPECT_EQ(byte_at(".scram", -134, CD_DATA_SIZE), 0x22);
    EXPECT_EQ(byte_at(".scram", -133, CD_DATA_SIZE), 0x23);
    EXPECT_EQ(byte_at(".scram", -117, CD_DATA_SIZE), 0x24);
    EXPECT_EQ(byte_at(".scram", -102, CD_DATA_SIZE), 0x24);
    EXPECT_EQ(std::filesystem::file_size(path(".scram")), (uint64_t)(-101 - LBA_START) * CD_DATA_SIZE);
}


TEST_F(SpiralDump, ShowsUnknownQLbaWhenCurrentFrameHasInvalidQ)
{
    auto opts = options();
    opts.lba_end = std::make_unique<int>(-133);
    auto ctx = context();
    auto sectors = batch(0x52, 1, -149);
    sectors[15 * CD_RAW_DATA_SIZE + CD_DATA_SIZE + CD_C2_SIZE] ^= 0x40;
    FakeTransport transport{
        {
         seek_reply(),
         read_reply(16, std::move(sectors)),
         }
    };

    ::testing::internal::CaptureStdout();
    EXPECT_EQ(run(transport, ctx, opts), 0);
    auto output = ::testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Q LBA: ?"), std::string::npos);
}


TEST_F(SpiralDump, RejectsGoodShortTransferWithoutWritingItsBatch)
{
    auto opts = options();
    auto ctx = context();
    auto short_data = batch(0xee, 1, -133);
    FakeTransport transport{
        {
         seek_reply(),
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
    FakeTransport transport;
    transport.replies.push_back(seek_reply());
    transport.replies.push_back(read_reply(16, batch(0x32, 0, -3000)));
    transport.replies.push_back(read_reply(16, batch(0xee, 1, -149), 2));
    for(uint32_t i = 1; i < 16; ++i)
        transport.replies.push_back(read_reply(16, batch(0xee, 1, -149), 2));

    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(transport.next, 18);
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
         seek_reply(2),
         }
    };

    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(transport.next, 1);
    ctx.drive_config.vendor_specific = "stock firmware";
    EXPECT_THROW(run(transport, ctx, opts), std::runtime_error);
    EXPECT_EQ(transport.next, 1);
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
