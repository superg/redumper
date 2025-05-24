module;
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include "utils/throw_line.hh"

export module readers.image_bin_reader;

import cd.cd;
import cd.cdrom;
import cd.common;
import readers.data_reader;



namespace gpsxre
{

export class Image_BIN_Reader : public DataReader
{
public:
    Image_BIN_Reader(const std::string &image_path)
    {
        _fs.open(image_path, std::fstream::in | std::fstream::binary);
        if(!_fs.is_open())
            throw_line("unable to open image file (path: {})", image_path);

        setBaseLBA();
    }


    uint32_t read(uint8_t *sectors, int32_t lba, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        uint32_t sectors_read = 0;

        uint32_t index = lba - _baseLBA;
        if(!seek(index))
            return sectors_read;

        for(uint32_t s = 0; s < count; ++s)
        {
            Sector sector;
            if(!read((uint8_t *)&sector) || memcmp(sector.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
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


    int32_t sampleOffset(int32_t lba) override
    {
        return lba_to_sample(lba, 0);
    }


    uint32_t sectorSize(bool form2 = false) override
    {
        return form2 ? FORM2_DATA_SIZE : FORM1_DATA_SIZE;
    }


    int32_t sectorsBase() override
    {
        return _baseLBA;
    }

private:
    std::fstream _fs;
    uint32_t _baseLBA;


    bool seek(uint32_t index)
    {
        bool success = false;

        _fs.seekg(index * CD_DATA_SIZE);
        if(_fs.fail())
            _fs.clear();
        else
            success = true;

        return success;
    }


    bool read(uint8_t *sector)
    {
        bool success = false;

        _fs.read((char *)sector, CD_DATA_SIZE);
        if(_fs.fail())
            _fs.clear();
        else
            success = true;

        return success;
    }


    void setBaseLBA()
    {
        Sector sector;
        if(!seek(0) || !read((uint8_t *)&sector))
            throw_line("unable to establish base LBA");

        _baseLBA = BCDMSF_to_LBA(sector.header.address);
    }
};

}
