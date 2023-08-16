module;
#include <cstdint>

export module readers.sector_reader;



namespace gpsxre
{

export template<typename T>
class SectorReaderT
{
public:
	virtual bool read(uint8_t *sector, T index, T count) = 0;
	virtual T getSectorSize() const = 0;
	virtual T getSectorsCount() const = 0;
};

export using SectorReader = SectorReaderT<uint32_t>;

}
