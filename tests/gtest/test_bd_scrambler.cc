#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

import bd.scrambler;

using namespace gpsxre;



// descrambling zeroes of a data frame (user data + EDC) yields the key stream of a sector
static std::vector<uint8_t> key_stream(int32_t lba)
{
    std::vector<uint8_t> data(2048 + 4, 0);
    bd::Scrambler::get().descramble(data, lba, false);

    return data;
}


TEST(BDScrambler, LayerStartPSN)
{
    // BD-50, opposite track path: layer 1 PSN starts at the complement of the layer 0 end PSN
    const int32_t layer_break = 11220192;
    const uint32_t layer1_psn = 0x144CB20;
    const std::vector<std::pair<int32_t, uint32_t>> no_layers;

    auto &scrambler = bd::Scrambler::get();

    // without layer starts PSN = LBA + 0x100000
    scrambler.set_layer_starts(no_layers);
    auto l0_key = key_stream(1000);
    auto l1_key = key_stream(layer1_psn + 4321 - 0x100000);
    auto l1_key_linear = key_stream(layer_break + 4321);
    ASSERT_NE(l1_key, l1_key_linear);

    scrambler.set_layer_starts({
        { 0,           0x100000   },
        { layer_break, layer1_psn }
    });
    EXPECT_EQ(key_stream(1000), l0_key);
    EXPECT_EQ(key_stream(layer_break + 4321), l1_key);

    scrambler.set_layer_starts(no_layers);
}
