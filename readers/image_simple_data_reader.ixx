module;
#include <cstdint>
#include <fstream>
#include <string>
#include "throw_line.hh"

export module readers.image_simple_data_reader;

import cd.common;



namespace gpsxre
{

export template<typename T, uint32_t S>
class Image_SimpleDataReader : public T
{
public:
    Image_SimpleDataReader(const std::string &image_path)
    {
        _fs.open(image_path, std::fstream::in | std::fstream::binary);
        if(!_fs.is_open())
            throw_line("unable to open image file (path: {})", image_path);
    }


    uint32_t read(uint8_t *sectors, int32_t lba, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        uint32_t sectors_read = 0;

        if(!form2)
        {
            _fs.seekg((uint64_t)lba * S);
            if(_fs.fail())
                _fs.clear();
            else
            {
                _fs.read((char *)sectors, (uint64_t)count * S);
                if(_fs.fail())
                    _fs.clear();
                else
                    sectors_read = count;
            }
        }

        if(form_hint != nullptr)
            *form_hint = false;

        return sectors_read;
    }

    
    int32_t sampleOffset(int32_t lba) override
    {
        return lba_to_sample(lba, 0);
    }


    uint32_t sectorSize(bool form2 = false) override
    {
        return S;
    }


    int32_t sectorsBase() override
    {
        return 0;
    }

private:
    std::fstream _fs;
};

}
