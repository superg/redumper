module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include "throw_line.hh"

export module dvd;

import cd.cdrom;
import common;
import dvd.edc;
import dvd.scrambler;
import utils.endian;
import utils.galois;



namespace gpsxre::dvd
{

export constexpr int32_t LBA_START = -0x30000;
export constexpr int32_t LBA_RCZ = -0x1000;
export using dvd::ECC_FRAMES;

// DVD uses primitive polynomial x^8 + x^4 + x^3 + x^2 + 1
GF256 gf(0x11D);


export enum class ZoneType : uint8_t
{
    DATA_ZONE,
    LEADIN_ZONE,
    LEADOUT_ZONE,
    MIDDLE_ZONE
};


export struct DataFrame
{
    struct ID
    {
        struct
        {
            uint8_t layer_number       :1;
            uint8_t data_type          :1;
            ZoneType zone_type         :2;
            uint8_t reserved           :1;
            uint8_t reflectivity       :1;
            uint8_t tracking_method    :1;
            uint8_t sector_format_type :1;
            uint8_t sector_number[3];
        } id;
        uint16_t ied;


        bool valid() const
        {
            // generator G(x) = x^2 + g1*x + g2
            uint8_t g1 = gf.add(1, gf.exp[1]); // alpha0 + alpha1
            uint8_t g2 = gf.exp[1];            // alpha0 * alpha1

            // initialize coefficients
            DataFrame::ID p{ id, 0 };
            uint8_t *poly = (uint8_t *)&p;

            // polynomial long division
            for(uint8_t i = 0; i < sizeof(p.id); ++i)
            {
                if(uint8_t coef = poly[i]; coef)
                {
                    poly[i + 0] = 0;
                    poly[i + 1] = gf.add(poly[i + 1], gf.mul(coef, g1));
                    poly[i + 2] = gf.add(poly[i + 2], gf.mul(coef, g2));
                }
            }

            return p.ied == ied;
        }
    } id;

    uint8_t cpr_mai[6];
    uint8_t main_data[FORM1_DATA_SIZE];
    uint32_t edc;


    bool valid(std::optional<uint8_t> nintendo_key = std::nullopt) const
    {
        bool valid = false;

        if(id.valid())
        {
            auto df = *this;
            df.descramble(nintendo_key);

            if(endian_swap(df.edc) == DVD_EDC().update((uint8_t *)&df, offsetof(DataFrame, edc)).final())
                valid = true;
        }

        return valid;
    }


    void descramble(std::optional<uint8_t> nintendo_key = std::nullopt)
    {
        uint32_t psn = endian_swap_from_array<int32_t>(id.id.sector_number);
        Scrambler::get().descramble(std::span(main_data, FORM1_DATA_SIZE), psn, nintendo_key);
    }
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

}
