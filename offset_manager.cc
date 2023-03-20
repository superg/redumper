#include "common.hh"
#include "offset_manager.hh"



namespace gpsxre
{

OffsetManager::OffsetManager(const std::vector<std::pair<int32_t, int32_t>> &offsets)
	: _offsets(offsets)
{
	if(_offsets.empty())
		throw_line("empty offsets provided");
}


bool OffsetManager::isVariable() const
{
	return _offsets.size() > 1;
}


int32_t OffsetManager::getOffset(int32_t lba) const
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

}
