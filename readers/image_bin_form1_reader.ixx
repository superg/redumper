module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_bin_form1_reader;

import cd.cd;
import readers.form1_reader;



namespace gpsxre
{

export class Image_BIN_Form1Reader : public Form1Reader
{
public:
	Image_BIN_Form1Reader(const std::filesystem::path &image_path)
		: _fs(image_path, std::fstream::in | std::fstream::binary)
		, _blocksCount(std::filesystem::file_size(image_path) / blockSize())
	{
		;
	}


	bool read(uint8_t *block, uint32_t index, uint32_t count) override
	{
		bool success = false;

		_fs.seekg(index * blockSize());
		if(_fs.fail())
			_fs.clear();
		else
		{
			_fs.read((char *)block, count * blockSize());
			if(_fs.fail())
				_fs.clear();
			else
				success = true;
		}

		return success;
	}
	
	
	uint32_t blocksCount() const override
	{
		return _blocksCount;
	}
	
private:
	std::fstream _fs;
	uint32_t _blocksCount;
};

}
