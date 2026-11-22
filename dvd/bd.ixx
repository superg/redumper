module;
#include <cstddef>
#include <cstdint>
#include <span>
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


static std::vector<uint32_t> current_layer_lengths;
static std::vector<uint32_t> current_layer_psns;


export std::vector<uint32_t> get_layer_lengths(const std::vector<uint8_t> &structure, bool rom, std::vector<uint32_t> *layer_psns = nullptr)
{
    std::vector<uint32_t> layer_lengths;

    uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + (rom ? 52 : 100);
    for(uint32_t j = 0; j < 32; ++j)
    {
        uint32_t offset = sizeof(CMD_ParameterListHeader) + j * unit_size;
        if(offset + unit_size > structure.size())
            break;

        auto &unit = (READ_DISC_STRUCTURE_DiscInformationUnit &)structure[offset];
        if(unit.header.identifier[0] != 'D' || unit.header.identifier[1] != 'I')
            break;

        if(unit.header.format == 1)
        {
            auto body = (READ_DISC_STRUCTURE_DiscInformationBody1 *)unit.body;

            auto last_psn = endian_swap(body->last_psn);
            auto first_aun = endian_swap(body->first_aun);
            auto last_aun = endian_swap(body->last_aun);
            auto layers_count = unit.header.layer + 1;

            layer_lengths.resize(layers_count);
            if(layer_psns)
            {
                layer_psns->resize(layers_count);
                (*layer_psns)[unit.header.layer] = first_aun;
            }

            layer_lengths[unit.header.layer] = ((layers_count == body->layers_count) ? last_psn - 1 : last_aun) - first_aun + 2;
        }
    }

    if(layer_psns && !layer_psns->empty())
        layer_psns->front() = 0x100000;

    return layer_lengths;
}


export std::vector<uint32_t> set_layer_addresses(const std::vector<uint8_t> &structure, bool rom)
{
    std::vector<uint32_t> layer_psns;
    auto layer_lengths = get_layer_lengths(structure, rom, rom ? &layer_psns : nullptr);

    if(rom)
    {
        current_layer_lengths = layer_lengths;
        current_layer_psns = layer_psns;
    }
    else
    {
        current_layer_lengths.clear();
        current_layer_psns.clear();
    }

    return layer_lengths;
}


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
        uint32_t psn = lba + 0x100000;
        int32_t lba_start = 0;

        for(uint32_t i = 0; i < current_layer_lengths.size(); ++i)
        {
            psn = current_layer_psns[i] + (lba - lba_start);
            if(lba < lba_start + (int32_t)current_layer_lengths[i])
                break;

            lba_start += (int32_t)current_layer_lengths[i];
        }

        Scrambler::get().descramble(std::span((uint8_t *)this, sizeof(DataFrame)), psn, nintendo);
    }
};


#pragma pack(push, 1)
export struct OmniDriveDataFrame
{
    DataFrame data_frame;
    uint8_t unknown[18];
};
#pragma pack(pop)

}
