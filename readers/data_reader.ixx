module;
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

export module readers.data_reader;

import cd.common;
import hash.sha1;



namespace gpsxre
{

export class DataReader
{
public:
    virtual ~DataReader() {}

    virtual uint32_t read(uint8_t *sectors, int32_t lba, uint32_t count, bool form2 = false, bool *form_hint = nullptr) = 0;
    virtual uint32_t sectorSize(bool form2 = false) = 0;
    virtual int32_t sectorsBase() = 0;
    virtual int32_t sampleOffset(int32_t lba) = 0;

    virtual std::string calculateSHA1(int32_t lba, uint32_t count, uint32_t form1_size, bool form2 = false, bool *form_hint = nullptr)
    {
        SHA1 bh_sha1;

        std::vector<uint8_t> sector(sectorSize(form2));

        for(uint32_t i = 0; i < count; ++i)
        {
            bool hint = false;
            if(read(sector.data(), lba + i, 1, form2, &hint) == 1)
            {
                uint32_t size = sector.size();
                if(!form2)
                {
                    uint32_t form1_read = i * size;
                    if(form1_read < form1_size)
                    {
                        uint32_t tail = form1_size - form1_read;
                        if(tail < size)
                            size = tail;
                    }
                    else
                        size = 0;
                }

                bh_sha1.update(sector.data(), size);
            }

            if(form_hint != nullptr && hint)
                *form_hint = true;
        }

        return bh_sha1.final();
    }
};

}
