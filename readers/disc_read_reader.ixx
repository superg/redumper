module;
#include <cstdint>
#include <string>

export module readers.disc_read_form1_reader;

import cd.cdrom;
import readers.data_reader;
import scsi.cmd;
import scsi.sptd;



namespace gpsxre
{

export class Disc_READ_Reader : public DataReader
{
public:
    Disc_READ_Reader(SPTD &sptd, int32_t base_lba)
        : _sptd(sptd)
        , _baseLBA(base_lba)
    {
        ;
    }


    uint32_t read(uint8_t *sectors, int32_t lba, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        uint32_t sectors_read = 0;

        if(!form2)
        {
            auto status = cmd_read(_sptd, sectors, FORM1_DATA_SIZE, lba, count, false);
            if(!status.status_code)
                sectors_read = count;
        }

        if(form_hint != nullptr)
            *form_hint = false;

        return sectors_read;
    }


    uint32_t sectorSize(bool form2 = false) override
    {
        return FORM1_DATA_SIZE;
    }


    int32_t sectorsBase() override
    {
        return _baseLBA;
    }

private:
    SPTD &_sptd;
    int32_t _baseLBA;
};

}
