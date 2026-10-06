#include <algorithm>
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

import hash.aes128;
import hash.cmac;

using namespace gpsxre;


uint8_t hexDigit(char c)
{
    return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10);
}

std::vector<uint8_t> hexBytes(std::string_view hex)
{
    std::vector<uint8_t> bytes;
    for(uint32_t i = 0; i < hex.size(); i += 2)
        bytes.push_back((uint8_t)((hexDigit(hex[i]) << 4) | hexDigit(hex[i + 1])));
    return bytes;
}

std::array<uint8_t, AES128::BLOCK_SIZE> hexBlock(std::string_view hex)
{
    auto bytes = hexBytes(hex);
    std::array<uint8_t, AES128::BLOCK_SIZE> block;
    std::copy(bytes.begin(), bytes.end(), block.begin());
    return block;
}

const auto CMAC_KEY = hexBlock("2b7e151628aed2a6abf7158809cf4f3c");
const auto CMAC_MESSAGE = hexBytes("6bc1bee22e409f96e93d7e117393172a" "ae2d8a571e03ac9c9eb76fac45af8e51" "30c81c46a35ce411e5fbc1191a0a52ef" "f69f2445df4f9b17ad2b417be66c3710");


TEST(Aes128, EncryptBlock)
{
    AES128 aes(hexBlock("000102030405060708090a0b0c0d0e0f"));
    EXPECT_EQ(aes.encryptBlock(hexBlock("00112233445566778899aabbccddeeff")), hexBlock("69c4e0d86a7b0430d8cdb78070b4c55a"));
}


TEST(Cmac, NistKnownAnswers)
{
    EXPECT_EQ(CMAC(CMAC_KEY).final(), hexBlock("bb1d6929e95937287fa37d129b756746"));
    EXPECT_EQ(CMAC(CMAC_KEY).update(CMAC_MESSAGE.data(), 16).final(), hexBlock("070a16b46b4d4144f79bdd9dd04a287c"));
    EXPECT_EQ(CMAC(CMAC_KEY).update(CMAC_MESSAGE.data(), 40).final(), hexBlock("dfa66747de9ae63030ca32611497c827"));
    EXPECT_EQ(CMAC(CMAC_KEY).update(CMAC_MESSAGE.data(), 64).final(), hexBlock("51f0bebf7e3b9d92fc49741779363cfe"));
}


TEST(Cmac, SplitUpdatesAndReset)
{
    auto expected = hexBlock("dfa66747de9ae63030ca32611497c827");
    CMAC cmac(CMAC_KEY);
    cmac.update(CMAC_MESSAGE.data(), 1).update(CMAC_MESSAGE.data() + 1, 15).update(CMAC_MESSAGE.data() + 16, 16).update(CMAC_MESSAGE.data() + 32, 8);
    EXPECT_EQ(cmac.final(), expected);

    // final() resets the message state while retaining the key.
    EXPECT_EQ(cmac.update(CMAC_MESSAGE.data(), 40).final(), expected);

    cmac.update(CMAC_MESSAGE.data(), 7).reset();
    EXPECT_EQ(cmac.update(CMAC_MESSAGE.data(), 40).final(), expected);
    EXPECT_EQ(cmac.update(nullptr, 0).final(), hexBlock("bb1d6929e95937287fa37d129b756746"));
}
