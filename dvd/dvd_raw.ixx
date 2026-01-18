module;
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include "throw_line.hh"

export module dvd.raw;

import cd.cdrom;
import common;
import drive.mediatek;
import options;
import scsi.sptd;
import utils.file_io;
import utils.galois;



namespace gpsxre
{


export constexpr uint32_t DVD_LBA_START = -0x30000;
export constexpr uint32_t DATA_FRAME_SIZE = 2064;
export constexpr uint32_t RECORDING_FRAME_SIZE = 2366;
export constexpr uint32_t ECC_FRAMES = 0x10;



export struct IdentificationData
{
    struct
    {
        uint8_t layer_number       :1;
        uint8_t data_type          :1;
        uint8_t zone_type          :2;
        uint8_t reserved           :1;
        uint8_t reflectivity       :1;
        uint8_t tracking_method    :1;
        uint8_t sector_format_type :1;
    } sector_info;

    uint8_t sector_number[3];

    int32_t psn() const
    {
        return (int32_t)((uint32_t(sector_number[0]) << 16) | (uint32_t(sector_number[1]) << 8) | uint32_t(sector_number[2]));
    }
};

export struct DataFrame
{
    IdentificationData id;
    uint16_t ied;
    uint8_t cpr_mai[6];
    uint8_t main_data[FORM1_DATA_SIZE];
    uint32_t edc;
};


export struct RecordingFrame
{
    struct Row
    {
        uint8_t main_data[172];
        uint8_t parity_inner[10];
    };

    Row row[12];
    uint8_t parity_outer[182];
};


export bool validate_id(const uint8_t id[6])
{
    // primitive polynomial x^8 + x^4 + x^3 + x^2 + 1
    static GF256 gf(0x11D); // 100011101

    // generator G(x) = x^2 + g1*x + g2
    uint8_t g1 = gf.add(1, gf.exp[1]); // alpha0 + alpha1
    uint8_t g2 = gf.exp[1];            // alpha0 * alpha1

    // initialize coefficients
    uint8_t poly[6] = { 0 };
    for(uint8_t i = 0; i < 4; ++i)
        poly[i] = id[i];

    // polynomial long division
    for(uint8_t i = 0; i <= 3; ++i)
    {
        uint8_t coef = poly[i];
        if(coef != 0)
        {
            poly[i + 0] = 0;
            poly[i + 1] = gf.add(poly[i + 1], gf.mul(coef, g1));
            poly[i + 2] = gf.add(poly[i + 2], gf.mul(coef, g2));
        }
    }

    return (poly[4] == id[4]) && (poly[5] == id[5]);
}

}
