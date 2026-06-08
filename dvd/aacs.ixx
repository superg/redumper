// based on aacskeys

module;

#include <cstdint>
#include <cstring>

export module bd.aacs;

import filesystem.udf;
import readers.data_reader;
import utils.endian;



namespace gpsxre
{

export class AACS
{
public:
    static int getMKBVersion(DataReader *reader)
    {
        auto root_directory = udf::Browser::rootDirectory(reader);
        if(!root_directory)
        {
            return -1;
        }

        auto media_key_block = root_directory->subEntry("AACS/MKB_RO.inf");
        if(!media_key_block)
        {
            return -1;
        }

        auto data = media_key_block->read();

        // The media key block version is stored as a big endian integer from 0x00000008 to 0x0000000B
        uint32_t mkbv;
        memcpy(&mkbv, data.data() + 8, sizeof(uint32_t));
        mkbv = endian_swap(mkbv);

        return mkbv;
    }
};

}
