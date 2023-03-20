#pragma once



#include <utility>
#include <vector>



namespace gpsxre
{

class OffsetManager
{
public:
	OffsetManager(const std::vector<std::pair<int32_t, int32_t>> &offsets);

	bool isVariable() const;
	int32_t getOffset(int32_t lba) const;

private:
	std::vector<std::pair<int32_t, int32_t>> _offsets;
};

}
