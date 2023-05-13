module;
#include <utility>
#include <vector>

export module offset_manager;

import utils.common;



namespace gpsxre
{

export class OffsetManager
{
public:
	OffsetManager(const std::vector<std::pair<int32_t, int32_t>> &offsets)
		: _offsets(offsets)
	{
		if(_offsets.empty())
			throw_line("empty offsets provided");
	}

	bool isVariable() const
	{
		return _offsets.size() > 1;
	}

	int32_t getOffset(int32_t lba) const
	{
		int32_t offset = _offsets.front().second;

		for(auto const &o : _offsets)
		{
			if(o.first > lba)
				break;
			offset = o.second;
		}

		return offset;
	}

private:
	std::vector<std::pair<int32_t, int32_t>> _offsets;
};

}
