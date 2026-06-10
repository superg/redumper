module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string.h>
#include <utility>
#include <vector>
#include "throw_line.hh"

export module readers.emulated_sector_reader;

import readers.data_reader;



namespace gpsxre
{

export template<uint32_t S>
class Emulated_Sector_Reader : public DataReader
{
public:
    Emulated_Sector_Reader(std::shared_ptr<DataReader> source)
        : _source(std::move(source))
    {
        _source_sector_size = _source->sectorSize();
        if(S > _source_sector_size)
            throw_line("emulated sector size must be less than or equal to the source sector size");

        if(_source_sector_size % S != 0)
            throw_line("source sector size must be divisible by the emulated sector size");

        _emulated_sectors_per_source = _source_sector_size / S;
    }

    uint32_t read(uint8_t *sectors, int32_t lba, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        if(form_hint != nullptr)
            *form_hint = false;

        if(form2)
            return 0;

        const int32_t source_lba = lba / _emulated_sectors_per_source;
        const uint32_t source_sectors_to_read = count / _emulated_sectors_per_source + (count % _emulated_sectors_per_source != 0) + (lba % _emulated_sectors_per_source != 0);

        std::vector<uint8_t> source_sectors(source_sectors_to_read * _source_sector_size);
        const uint32_t source_sectors_read = _source->read(source_sectors.data(), source_lba, source_sectors_to_read, false, form_hint);
        if(source_sectors_read == 0)
            return 0;

        const uint32_t lba_offset = lba % _emulated_sectors_per_source;
        const uint32_t emulated_sectors_available = source_sectors_read * _emulated_sectors_per_source - lba_offset;
        const uint32_t byte_offset = lba_offset * S;
        const uint32_t emulated_sectors_to_copy = std::min(count, emulated_sectors_available);
        memcpy(sectors, &source_sectors[byte_offset], emulated_sectors_to_copy * S);

        return emulated_sectors_to_copy;
    }

    uint32_t sectorSize(bool form2 = false) override
    {
        return S;
    }

    int32_t sectorsBase() override
    {
        return _source->sectorsBase() * _emulated_sectors_per_source;
    }

    uint32_t sectorsCount() const override
    {
        return _source->sectorsCount() * _emulated_sectors_per_source;
    }

    int32_t sampleOffset(int32_t lba) override
    {
        return lba * S;
    }

private:
    std::shared_ptr<DataReader> _source;
    uint32_t _source_sector_size;
    uint32_t _emulated_sectors_per_source;
};
}
