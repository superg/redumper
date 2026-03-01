module;
#include <format>
#include <sstream>
#include <string>
#include <vector>
#include "system.hh"

export module systems.gc;

import cd.cdrom;
import cd.common;
import readers.data_reader;
import utils.strings;
import utils.file_io;
import utils.hex_bin;


namespace gpsxre
{

export class SystemGC : public System
{
public:
    std::string getName() override
    {
        return "GC";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &track_path, bool) const override
    {
        std::vector<uint8_t> header_data(FORM1_DATA_SIZE);
        data_reader->read((uint8_t *)header_data.data(), 0, 1);
        auto header = (Header *)header_data.data();
        if(header->magic != _GC_MAGIC)
            return;

        os << std::format("  version: {}", header->disc_version) << std::endl;

        std::string serial = normalize_string(std::string(&header->disc_id, 6));
        os << std::format("  serial: {}", serial) << std::endl;

        std::string title = normalize_string(erase_all(std::string(header->title, sizeof(Header::title)), '\0'));
        os << std::format("  title: {}", title) << std::endl;

        if(header->disc_number)
            os << std::format("  disc number: {}", header->disc_number) << std::endl;

        std::filesystem::path bca_path = track_extract_basename(track_path.string()) + ".bca";
        if(!std::filesystem::exists(bca_path))
            return;

        auto bca = read_vector(bca_path);
        if(bca.size() != _GC_BCA_STRUCTURE_SIZE)
            return;

        os << "  BCA:" << std::endl;
        os << std::format("{}", rawhexdump(&bca[_GC_REPORTED_BCA_OFFSET], _GC_REPORTED_BCA_SIZE));
    }

private:
    static constexpr uint32_t _GC_MAGIC = 0x3D9F33C2;
    static constexpr uint32_t _GC_BCA_STRUCTURE_SIZE = 192;
    static constexpr uint32_t _GC_REPORTED_BCA_OFFSET = 0x80;
    static constexpr uint32_t _GC_REPORTED_BCA_SIZE = 0x40;

    struct Header
    {
        char disc_id;
        char game_code[2];
        char region_code;
        char maker_code[2];
        uint8_t disc_number;
        uint8_t disc_version;
        uint8_t audio_streaming;
        uint8_t streaming_buffer_size;
        uint8_t unknown[18];
        uint32_t magic;
        char title[64];
        uint8_t disable_hash_verification;
        uint8_t disable_disc_encryption;
        uint8_t padding[380];
    };
};

}
