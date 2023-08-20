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
		, _count(std::filesystem::file_size(image_path) / T::sectorSize())
	{
		;
	}


	bool read(uint8_t *sectors, uint32_t index, uint32_t count) override
	{
		bool success = false;

		_fs.seekg(index * T::sectorSize());
		if(_fs.fail())
			_fs.clear();
		else
		{
			_fs.read((char *)sectors, count * T::sectorSize());
			if(_fs.fail())
				_fs.clear();
			else
				success = true;
		}

		return success;
	}


	uint32_t count() const override
	{
		return _count;
	}

private:
	std::fstream _fs;
	uint32_t _count;
};

}
