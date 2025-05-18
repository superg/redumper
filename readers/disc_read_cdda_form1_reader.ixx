module;
#include <cstdint>
#include <span>
#include <string>
#include <vector>

export module readers.disc_read_cdda_form1_reader;

import cd.cdrom;
import cd.common;
import drive;
import readers.sector_reader;
import scsi.cmd;
import scsi.sptd;



namespace gpsxre
{

export class Disc_READ_CDDA_Reader : public SectorReader
{
public:
    Disc_READ_CDDA_Reader(SPTD &sptd, const DriveConfig &drive_config, uint32_t base_lba)
        : _sptd(sptd)
        , _driveConfig(drive_config)
        , _baseLBA(base_lba)
    {
        ;
    }


    uint32_t read(uint8_t *sectors, uint32_t index, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        uint32_t sectors_read = 0;

        for(uint32_t s = 0; s < count; ++s)
        {
            Sector sector;
            if(!read((uint8_t *)&sector, index))
                continue;

            uint8_t *user_data = nullptr;
            bool user_form2 = false;
            if(sector.header.mode == 1)
            {
                user_data = sector.mode1.user_data;
            }
            else if(sector.header.mode == 2)
            {
                if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
                {
                    user_data = sector.mode2.xa.form2.user_data;
                    user_form2 = true;
                }
                else
                {
                    user_data = sector.mode2.xa.form1.user_data;
                }
            }

            if(user_data != nullptr)
            {
                if(user_form2 == form2)
                {
                    uint32_t size = sectorSize(user_form2);
                    memcpy(sectors + sectors_read * size, user_data, size);
                    ++sectors_read;
                }
                else if(form_hint != nullptr)
                    *form_hint = true;
            }
        }

        return sectors_read;
    }


    uint32_t sectorSize(bool form2 = false) override
    {
        return form2 ? FORM2_DATA_SIZE : FORM1_DATA_SIZE;
    }


    uint32_t sectorsBase() override
    {
        return _baseLBA;
    }

private:
    SPTD &_sptd;
    const DriveConfig &_driveConfig;
    uint32_t _baseLBA;

    bool read(uint8_t *sector, uint32_t index)
    {
        bool success = false;

        std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
        std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
        std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);

        bool unscrambled = false;
        SPTD::Status status = read_sector_new(_sptd, sector_buffer.data(), unscrambled, _driveConfig, _baseLBA + index);

        return success;
    }
};

}
