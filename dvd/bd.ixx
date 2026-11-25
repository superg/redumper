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


    bool valid(uint32_t psn, bool nintendo) const
    {
        bool valid = false;

        auto df = *this;
        df.descramble(psn, nintendo);

        if(endian_swap(df.edc) == DVD_EDC().update((uint8_t *)&df, offsetof(DataFrame, edc)).final())
            valid = true;

        return valid;
    }


    void descramble(uint32_t psn, bool nintendo)
    {
        Scrambler::get().descramble(std::span((uint8_t *)this, sizeof(DataFrame)), psn, nintendo);
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


// PSN of a further layer doesn't continue the previous layer (opposite track path), it starts where the DI says
export uint32_t LBA_to_PSN(int32_t lba, const std::vector<std::pair<uint32_t, uint32_t>> &layers)
{
    uint32_t psn = lba + 0x100000;

    int32_t layer_lba = 0;
    for(auto const &[first_psn, length] : layers)
    {
        if(lba >= layer_lba)
            psn = first_psn + (lba - layer_lba);
        layer_lba += (int32_t)length;
    }

    return psn;
}


#pragma pack(push, 1)
export struct OmniDriveDataFrame
{
    DataFrame data_frame;
    uint8_t unknown[18];
};
#pragma pack(pop)

}
