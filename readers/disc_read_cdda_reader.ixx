module;
#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>
#include "throw_line.hh"

export module readers.disc_read_cdda_reader;

import cd.cdrom;
import cd.common;
import cd.scrambler;
import drive;
import readers.data_reader;
import scsi.cmd;
import scsi.sptd;



namespace gpsxre
{

export class Disc_READ_CDDA_Reader : public DataReader
{
public:
    Disc_READ_CDDA_Reader(SPTD &sptd, const DriveConfig &drive_config, int32_t base_lba)
        : _sptd(sptd)
        , _driveConfig(drive_config)
        , _baseLBA(base_lba)
        , _indexShift(0)
    {
        ;
    }


    uint32_t read(uint8_t *sectors, int32_t lba, uint32_t count, bool form2 = false, bool *form_hint = nullptr) override
    {
        uint32_t sectors_read = 0;

        for(uint32_t s = 0; s < count; ++s)
        {
            Sector sector;
            read(sector, lba);

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
        Sector sector;
        return read(sector, lba);
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
    SPTD &_sptd;
    const DriveConfig &_driveConfig;
    int32_t _baseLBA;
    int32_t _indexShift;

    int32_t read(Sector &sector, int32_t lba)
    {
        int32_t sample_offset = 0;

        std::set<int32_t> history;
        history.insert(_indexShift);
        for(;;)
        {
            sample_offset = read((uint8_t *)&sector, lba + _indexShift);
            int32_t sector_lba = BCDMSF_to_LBA(sector.header.address);

            int32_t shift = lba - sector_lba;
            if(shift)
            {
                _indexShift += shift;
                if(!history.insert(_indexShift).second)
                    throw_line("infinite loop detected (LBA: {}, shift: {:+})", sector_lba, _indexShift);
            }
            else
                break;
        }

        return sample_offset;
    }


    int32_t read(uint8_t *sector, uint32_t lba)
    {
        std::vector<uint8_t> sector_buffer(CD_RAW_DATA_SIZE);
        std::span<const uint8_t> sector_data(sector_buffer.begin(), CD_DATA_SIZE);
        std::span<const uint8_t> sector_c2(sector_buffer.begin() + CD_DATA_SIZE, CD_C2_SIZE);

        constexpr uint32_t sectors_count = 2;
        std::vector<uint8_t> sectors(CD_DATA_SIZE * sectors_count);
        for(uint32_t i = 0; i < sectors_count; ++i)
        {
            int32_t lba_current = lba + i;
            bool unscrambled = false;
            SPTD::Status status = read_sector_new(_sptd, sector_buffer.data(), unscrambled, _driveConfig, lba_current);
            if(status.status_code)
                throw_line("SCSI error (LBA: {}, status: {})", lba_current, SPTD::StatusMessage(status));
            if(unscrambled)
                throw_line("unscrambled read (LBA: {})", lba_current);
            auto c2_bits = c2_bits_count(sector_c2);
            if(c2_bits)
                throw_line("C2 error (LBA: {}, bits: {})", lba_current, c2_bits);

            std::span<uint8_t> sectors_out(sectors.begin() + i * CD_DATA_SIZE, CD_DATA_SIZE);
            std::copy(sector_data.begin(), sector_data.end(), sectors_out.begin());
        }

        auto it = std::search(sectors.begin(), sectors.end(), std::begin(CD_DATA_SYNC), std::end(CD_DATA_SYNC));
        if(it == sectors.end())
            throw_line("sync not found (LBA: {})", lba);

        // there might be an incomplete sector followed by a complete sector (another sync)
        auto it2 = std::search(it + sizeof(CD_DATA_SYNC), sectors.end(), std::begin(CD_DATA_SYNC), std::end(CD_DATA_SYNC));
        if(it2 != sectors.end() && std::distance(it, it2) < CD_DATA_SIZE)
            it = it2;

        auto sync_index = (uint32_t)std::distance(sectors.begin(), it);
        if(sync_index + CD_DATA_SIZE > sectors.size())
            throw_line("not enough data (LBA: {})", lba);

        std::span<uint8_t> s(&sectors[sync_index], CD_DATA_SIZE);
        Scrambler::process(sector, &s[0], 0, s.size());

        return lba_to_sample(lba, -_driveConfig.read_offset + sync_index / CD_SAMPLE_SIZE);
    }
};

}
