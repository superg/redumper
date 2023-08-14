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
		, _blocksCount(std::filesystem::file_size(image_path) / T::blockSize())
	{
		;
	}


	bool read(uint8_t *block, uint32_t index, uint32_t count) override
	{
		bool success = false;

		_fs.seekg(index * T::blockSize());
		if(_fs.fail())
			_fs.clear();
		else
		{
			_fs.read((char *)block, count * T::blockSize());
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
