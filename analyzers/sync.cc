#include "common.hh"
#include "sync.hh"



namespace gpsxre
{

SyncAnalyzer::SyncAnalyzer(bool scrap)
	: _scrap(scrap)
	, _syncSearch(0)
{
	;
}


std::vector<std::pair<int32_t, int32_t>> SyncAnalyzer::getOffsets() const
{
	std::vector<std::pair<int32_t, int32_t>> offsets;

	if(_records.empty())
		return offsets;

	//DEBUG
//	for(auto const &r : _records)
//		LOG("LBA: {:6}, MSF: {:02X}:{:02X}:{:02X}, offset: {:9}", BCDMSF_to_LBA(r.first), r.first.m, r.first.s, r.first.f, r.second);
//	LOG("LBA: {:6}, MSF: {:02X}:{:02X}:{:02X}, offset: {:9}", BCDMSF_to_LBA(_currentRecord.first), _currentRecord.first.m, _currentRecord.first.s, _currentRecord.first.f, _currentRecord.second);

	// copy initial data
	for(auto const &r : _records)
		offsets.emplace_back(BCDMSF_to_LBA(r.first), r.second);

	// correct lead-in lba
	for(uint32_t i = 0; i < offsets.size(); ++i)
	{
		if(offsets[i].first >= MSF_LBA_SHIFT && offsets[i].first <= 0)
		{
			for(uint32_t j = i; j; --j)
			{
				uint32_t count = scale_up(offsets[j].second - offsets[j - 1].second, CD_DATA_SIZE_SAMPLES);
				offsets[j - 1].first = offsets[j].first - count;
			}

			break;
		}
	}

	// erase false sync groups
	for(uint32_t i = 0; i < offsets.size(); ++i)
	{
		uint32_t count = 0;
		for(uint32_t j = i + 1; j < offsets.size(); ++j)
		{
			uint32_t o = offsets[j].second - offsets[i].second;
			uint32_t d = offsets[j].first - offsets[i].first;
			if(d * CD_DATA_SIZE_SAMPLES == o)
			{
				if(count < CD_DATA_SIZE_SAMPLES)
					offsets.erase(offsets.begin() + i + 1, offsets.begin() + j);

				break;
			}

			if(j + 1 < offsets.size())
				count += offsets[j + 1].second - offsets[j].second;
		}
	}

	// calculate lba relative offsets
	for(uint32_t i = 0; i < offsets.size(); ++i)
		offsets[i].second -= (offsets[i].first - LBA_START) * CD_DATA_SIZE_SAMPLES;

	// merge offset groups
	uint32_t c = 0;
	for(uint32_t i = 0; i < offsets.size(); ++i)
		if(offsets[c].second != offsets[i].second)
			offsets[++c] = offsets[i];
	offsets.resize(c + 1);

	// add last entry as a size marker
	offsets.emplace_back(BCDMSF_to_LBA(_currentRecord.first) + 1, offsets.back().second);

	return offsets;
}


void SyncAnalyzer::process(uint32_t *samples, State *state, uint32_t count, uint32_t offset, bool)
{
	for(uint32_t i = 0; i < count; ++i)
	{
		if(state[i] == State::ERROR_SKIP || state[i] == State::ERROR_C2)
		{
			_syncSearch = 0;
			continue;
		}
		
		if(_syncSearch < SYNC_SIZE_SAMPLES)
		{
			if(samples[i] == ((uint32_t *)CD_DATA_SYNC)[_syncSearch])
				++_syncSearch;
			else
				_syncSearch = 0;
		}
		else
		{
			_currentRecord.second = offset + i - SYNC_SIZE_SAMPLES;
			if(_scrap)
				_currentRecord.first = *(MSF *)&samples[i];
			else
				_scrambler.Process((uint8_t *)&_currentRecord.first, (uint8_t *)&samples[i], sizeof(CD_DATA_SYNC), sizeof(_currentRecord.first));

			if(_records.empty())
				_records.push_back(_currentRecord);
			else
			{
				auto &b = _records.back();


				uint32_t d = _currentRecord.second - b.second;
				int32_t l = BCDMSF_to_LBA(_currentRecord.first) - BCDMSF_to_LBA(b.first);
				if(d % CD_DATA_SIZE_SAMPLES || l != d / CD_DATA_SIZE_SAMPLES)
					_records.push_back(_currentRecord);
			}
			
			_syncSearch = 0;
		}
	}
}

}
