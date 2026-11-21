module;
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module bd;

import cd.cdrom;
import bd.scrambler;
import common;
import dvd.edc;
import scsi.mmc;
import utils.endian;



namespace gpsxre::bd
{

export constexpr int32_t LBA_START = -0x100000;
export constexpr int32_t LBA_IZ = -0x2000;
export constexpr uint32_t ECC_FRAMES = 0x20;


export struct DataFrame
{
    uint8_t main_data[FORM1_DATA_SIZE];
    uint32_t edc;


    bool valid(int32_t lba, bool nintendo) const
    {
        bool valid = false;

        auto df = *this;
        df.descramble(lba, nintendo);

        if(endian_swap(df.edc) == DVD_EDC().update((uint8_t *)&df, offsetof(DataFrame, edc)).final())
            valid = true;

        return valid;
    }


    void descramble(int32_t lba, bool nintendo)
    {
        Scrambler::get().descramble(std::span((uint8_t *)this, sizeof(DataFrame)), lba, nintendo);
    }
};


// (first PSN, length) of each layer from the disc information units (READ DISC STRUCTURE format 0 without header)
export std::vector<std::pair<uint32_t, uint32_t>> get_layers(std::span<const uint8_t> di_units, bool rom)
{
    std::vector<std::pair<uint32_t, uint32_t>> layers;

    uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + (rom ? 52 : 100);
    for(uint32_t j = 0; j < 32 && j < di_units.size() / unit_size; ++j)
    {
        auto &unit = (READ_DISC_STRUCTURE_DiscInformationUnit &)di_units[j * unit_size];
        std::string identifier((char *)unit.header.identifier, sizeof(unit.header.identifier));
        if(identifier != "DI")
            break;

        if(unit.header.format == 1)
        {
            auto body = (READ_DISC_STRUCTURE_DiscInformationBody1 *)unit.body;

            auto last_psn = endian_swap(body->last_psn);
            auto first_aun = endian_swap(body->first_aun);
            auto last_aun = endian_swap(body->last_aun);

            auto layers_count = unit.header.layer + 1;
            layers.resize(layers_count);
            layers[unit.header.layer] = std::pair(first_aun, (layers_count == body->layers_count ? last_psn - 1 : last_aun) - first_aun + 2);
        }
    }

    return layers;
}


// raw descrambling needs the PSN where each layer starts
export void scrambler_init_layers(std::span<const uint8_t> di_units, bool rom)
{
    std::vector<std::pair<int32_t, uint32_t>> layer_starts;
    int32_t lba = 0;
    for(auto const &[psn, length] : get_layers(di_units, rom))
    {
        layer_starts.emplace_back(lba, psn);
        lba += (int32_t)length;
    }

    Scrambler::get().set_layer_starts(layer_starts);
}


#pragma pack(push, 1)
export struct OmniDriveDataFrame
{
    DataFrame data_frame;
    uint8_t unknown[18];
};
#pragma pack(pop)

}
