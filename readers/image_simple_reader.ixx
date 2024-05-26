module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_simple_reader;



namespace gpsxre
{

export template<typename T, uint32_t S>
class Image_SimpleReader : public T
{
public:
    Image_SimpleReader(const std::filesystem::path &image_path)
        : _fs(image_path, std::fstream::in | std::fstream::binary)
        , _sectorsCount(std::filesystem::file_size(image_path) / S)
    {
        ;
    }


    uint32_t read(uint8_t *sectors, uint32_t index, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        uint32_t sectors_read = 0;

        if(!form2)
        {
            _fs.seekg((uint64_t)index * S);
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


    uint32_t sectorSize(bool form2 = false) override
    {
        return S;
    }


    uint32_t sectorsCount() const override
    {
        return _sectorsCount;
    }

private:
    std::fstream _fs;
    uint32_t _sectorsCount;
};

}
