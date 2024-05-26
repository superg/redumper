module;
#include <cstdint>
#include <cstring>

export module cd.cdrom;

import cd.cd;



namespace gpsxre
{

export const uint32_t FORM1_DATA_SIZE = 2048;
export const uint32_t FORM2_DATA_SIZE = 2324;
export const uint32_t MODE0_DATA_SIZE = 2336;

export const uint8_t CD_DATA_SYNC[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };


export enum class CDXAMode : uint8_t
{
    EORECORD = 1 << 0,
    VIDEO = 1 << 1,
    AUDIO = 1 << 2,
    DATA = 1 << 3,
    TRIGGER = 1 << 4,
    FORM2 = 1 << 5,
    REALTIME = 1 << 6,
    EOFILE = 1 << 7
};


export struct Sector
{
    struct ECC
    {
        uint8_t p_parity[172];
        uint8_t q_parity[104];
    };

    uint8_t sync[12];

    struct Header
    {
        MSF address;
        uint8_t mode;
    } header;

    union
    {
        struct
        {
            uint8_t user_data[FORM1_DATA_SIZE];
            uint32_t edc;
            uint8_t intermediate[8];
            ECC ecc;
        } mode1;
        struct
        {
            union
            {
                uint8_t user_data[MODE0_DATA_SIZE];

                struct
                {
                    struct SubHeader
                    {
                        uint8_t file_number;
                        uint8_t channel;
                        uint8_t submode;
                        uint8_t coding_info;
                    } sub_header;
                    SubHeader sub_header_copy;

                    union
                    {
                        struct
                        {
                            uint8_t user_data[FORM1_DATA_SIZE];
                            uint32_t edc;
                            ECC ecc;
                        } form1;
                        struct
                        {
                            uint8_t user_data[FORM2_DATA_SIZE];
                            uint32_t edc; // reserved
                        } form2;
                    };
                } xa;
            };
        } mode2;
    };
};

}
