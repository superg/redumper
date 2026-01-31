module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include "throw_line.hh"

export module dvd;

import cd.cdrom;
import common;
import utils.endian;
import utils.galois;



namespace gpsxre
{

export constexpr int32_t DVD_LBA_START = -0x30000;
export constexpr uint32_t ECC_FRAMES = 0x10;

// DVD uses primitive polynomial x^8 + x^4 + x^3 + x^2 + 1
GF256 gf(0x11D);


export struct DataFrame
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
        uint8_t sector_number[3];
    } id;
    uint16_t ied;
    uint8_t cpr_mai[6];
    uint8_t main_data[FORM1_DATA_SIZE];
    uint32_t edc;
};


export struct RecordingFrame
{
    struct
    {
        uint8_t main_data[172];
        uint8_t parity_inner[10];
    } row[12];
    uint8_t parity_outer[182];
};


void compute_parity_inner(std::span<uint8_t> parity_inner, std::span<const uint8_t> main_data)
{
    // PI: RS(n,k,d) where n=data_size+parity_size, k=data_size, d=parity_size
    // generator polynomial roots: alpha^0, alpha^1, ..., alpha^(parity_size-1)
    uint8_t parity[ECC_FRAMES] = {};
    for(uint32_t col = 0; col < main_data.size(); ++col)
    {
        uint8_t feedback = gf.add(main_data[col], parity[0]);
        for(uint32_t j = 0; j < parity_inner.size() - 1; ++j)
            parity[j] = gf.add(parity[j + 1], gf.mul(feedback, gf.exp[parity_inner.size() - 1 - j]));
        parity[parity_inner.size() - 1] = gf.mul(feedback, gf.exp[0]);
    }
    for(uint32_t j = 0; j < parity_inner.size(); ++j)
        parity_inner[j] = parity[parity_inner.size() - 1 - j];
}


void compute_parity_outer(std::span<uint8_t> parity_outer, std::span<const uint8_t> main_data, uint32_t stride)
{
    // PO: simplified single-frame encoding
    // treat each column as data_size bytes and compute 1 parity byte
    for(uint32_t col = 0; col < parity_outer.size(); ++col)
    {
        uint8_t parity = 0;
        for(uint32_t row = 0; row < main_data.size() / stride; ++row)
            parity = gf.add(parity, main_data[row * stride + col]);
        parity_outer[col] = parity;
    }
}


export RecordingFrame DataFrame_to_RecordingFrame(const DataFrame &data_frame)
{
    RecordingFrame recording_frame = {};

    auto src = (const uint8_t *)&data_frame;

    for(uint32_t i = 0; i < std::size(recording_frame.row); ++i)
        std::copy_n(src + i * std::size(recording_frame.row[i].main_data), std::size(recording_frame.row[i].main_data), recording_frame.row[i].main_data);

    for(uint32_t i = 0; i < std::size(recording_frame.row); ++i)
        compute_parity_inner(recording_frame.row[i].parity_inner, recording_frame.row[i].main_data);

    compute_parity_outer(recording_frame.parity_outer, std::span((uint8_t *)recording_frame.row, sizeof(recording_frame.row)), sizeof(recording_frame.row[0]));

    return recording_frame;
}


export DataFrame RecordingFrame_to_DataFrame(const RecordingFrame &recording_frame)
{
    DataFrame data_frame = {};

    auto dst = (uint8_t *)&data_frame;

    for(uint32_t i = 0; i < std::size(recording_frame.row); ++i)
        std::copy_n(recording_frame.row[i].main_data, std::size(recording_frame.row[i].main_data), dst + i * std::size(recording_frame.row[i].main_data));

    return data_frame;
}


export bool validate_id(const uint8_t *id)
{
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
