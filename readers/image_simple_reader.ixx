module;
#include <cstdint>
#include <filesystem>
#include <fstream>

export module readers.image_simple_reader;



namespace gpsxre
{

export template<typename T>
class Image_SimpleReader : public T
{
public:
	Image_SimpleReader(const std::filesystem::path &image_path)
		: _fs(image_path, std::fstream::in | std::fstream::binary)
	{
		;
	}


	uint32_t read(uint8_t *sectors, uint32_t index, uint32_t count) override
	{
		uint32_t sectors_read = 0;

		_fs.seekg(index * T::sectorSize());
		if(_fs.fail())
			_fs.clear();
		else
		{
			_fs.read((char *)sectors, count * T::sectorSize());
			if(_fs.fail())
				_fs.clear();
			else
				sectors_read = count;
		}

		return sectors_read;
	}

private:
	std::fstream _fs;
};

}
