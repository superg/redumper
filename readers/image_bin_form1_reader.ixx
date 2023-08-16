module;
#include <cstdint>
#include <filesystem>
#include <fstream>

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
		, _sectorsCount(std::filesystem::file_size(image_path) / CD_DATA_SIZE)
	{
		;
	}


	bool read(uint8_t *block, uint32_t index, uint32_t count) override
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

				memcpy(block + s * getSectorSize(), user_data, getSectorSize());
			}
		}

		return success;
	}
	
	
	uint32_t getSectorsCount() const override
	{
		return _sectorsCount;
	}
	
private:
	std::fstream _fs;
	uint32_t _sectorsCount;
};

}
