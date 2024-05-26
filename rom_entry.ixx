module;
#include <cstdint>
#include <format>
#include <ostream>
#include <string>

export module rom_entry;

import crc.crc32;
import hash.md5;
import hash.sha1;
import utils.strings;



namespace gpsxre
{

export class ROMEntry
{
public:
    ROMEntry(std::string name)
        : _name(replace_all(name, "&", "&amp;"))
        , _size(0)
    {
        ;
    }


    void update(const uint8_t *data, uint64_t size)
    {
        _size += size;

        _crc32.update(data, size);
        _md5.update(data, size);
        _sha1.update(data, size);
    }


    std::string xmlLine()
    {
        // do it only once because final() for MD5/SHA-1 modifies state
        if(_xmlLine.empty())
            _xmlLine = std::format("<rom name=\"{}\" size=\"{}\" crc=\"{:08x}\" md5=\"{}\" sha1=\"{}\" />", _name, _size, _crc32.final(), _md5.final(), _sha1.final());

        return _xmlLine;
    }

private:
    std::string _name;
    uint64_t _size;
    CRC32 _crc32;
    MD5 _md5;
    SHA1 _sha1;

    std::string _xmlLine;
};

}
