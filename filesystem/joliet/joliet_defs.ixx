module;
#include <cstdint>
#include <set>
#include <string>

export module filesystem.joliet:defs;

import filesystem.iso9660;
import utils.endian;
import utils.strings;



export namespace gpsxre::joliet
{

const std::set<std::string_view> ESCAPE_SEQUENCES = {
    "%/@", // UCS-2 Level 1
    "%/C", // UCS-2 Level 2
    "%/E"  // UCS-2 Level 3
};

using VolumeIdentifier = uint16_t[sizeof(iso9660::SupplementaryVolumeDescriptor::volume_identifier) / sizeof(uint16_t)];

// Converts an UCS-2BE identifier to a trimmed UTF-8 string.
template<std::size_t N>
std::string identifier_to_string(const uint16_t (&identifier)[N])
{
    std::string result;

    for(auto c : identifier)
    {
        uint16_t codepoint = endian_swap(c);

        if(codepoint <= 0x7F)
        {
            result += (char)(codepoint);
        }
        else if(codepoint <= 0x7FF)
        {
            result += (char)(0xC0 | (codepoint >> 6));
            result += (char)(0x80 | ((codepoint >> 0) & 0x3F));
        }
        else
        {
            result += (char)(0xE0 | (codepoint >> 12));
            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
            result += (char)(0x80 | ((codepoint >> 0) & 0x3F));
        }
    }

    return trim(result).c_str();
}

}
