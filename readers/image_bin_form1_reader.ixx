module;
#include <cstdint>
#include <filesystem>
#include <fstream>
#include "utils/throw_line.hh"

export module readers.image_bin_form1_reader;

import cd.cd;
import cd.cdrom;
import readers.form1_reader;



namespace gpsxre
{

export class Image_BIN_Form1Reader : public Form1Reader
{
public:
	Image_BIN_Form1Reader(const std::filesystem::path &image_path)
		: _fs(image_path, std::fstream::in | std::fstream::binary)
		, _count(std::filesystem::file_size(image_path) / CD_DATA_SIZE)
	{
		Sector sector;
		if(!read((uint8_t *)&sector, 0, 1))
			throw_line("unable to establish base LBA");

		_baseLBA = BCDMSF_to_LBA(sector.header.address);
	}


	bool read(uint8_t *sectors, uint32_t index, uint32_t count) override
	{
		bool success = true;

		_fs.seekg(index * CD_DATA_SIZE);
		if(_fs.fail())
		{
			_fs.clear();
			success = false;
		}
		else
		{
			for(uint32_t s = 0; s < count; ++s)
			{
				Sector sector;
				_fs.read((char *)&sector, sizeof(sector));

				if(_fs.fail() || memcmp(sector.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
				{
					_fs.clear();
					success = false;
					break;
				}

				uint8_t *user_data = nullptr;
				if(sector.header.mode == 1)
				{
					user_data = sector.mode1.user_data;
				}
				else if(sector.header.mode == 2)
				{
					if(!(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2))
					{
						user_data = sector.mode2.xa.form1.user_data;
					}
				}

				if(user_data == nullptr)
				{
					success = false;
					break;
				}

				memcpy(sectors + s * sectorSize(), user_data, sectorSize());
			}
		}

		return success;
	}


	bool readLBA(uint8_t *sectors, uint32_t lba, uint32_t count) override
	{
		return read(sectors, lba - _baseLBA, count);
	};
	

	uint32_t count() const override
	{
		return _count;
	}
	
private:
	std::fstream _fs;
	uint32_t _count;

	uint32_t _baseLBA;
};

}
