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


std::vector<SyncAnalyzer::Record> SyncAnalyzer::getOffsets() const
{
	std::vector<Record> offsets(_records);

	if(offsets.empty())
		return offsets;

	//DEBUG
//	for(auto const &o : offsets)
//		LOG("LBA: [{:6} .. {:6}], offset: {:9}, count: {}",
//			o.range.first, o.range.second, o.offset, o.count);
	
	// correct lead-in lba
	for(uint32_t i = 0; i < offsets.size(); ++i)
	{
		if(offsets[i].range.first >= MSF_LBA_SHIFT && offsets[i].range.first <= 0)
		{
			for(uint32_t j = i; j; --j)
			{
				auto &p = offsets[j - 1];

				uint32_t count = scale_up(offsets[j].offset - p.offset, CD_DATA_SIZE_SAMPLES);
				uint32_t length = p.range.second - p.range.first;
				p.range.first = offsets[j].range.first - count;
				p.range.second = p.range.first + length;
			}

			break;
		}
	}

	// erase false sync groups
	for(auto it = offsets.begin(); it != offsets.end();)
	{
		if(it->count == 1)
			it = offsets.erase(it);
		else
			++it;
	}

	// merge offset groups
	for(bool merge = true; merge;)
	{
		merge = false;
		for(uint32_t i = 0; i + 1 < offsets.size(); ++i)
		{
			uint32_t offset_diff = offsets[i + 1].offset - offsets[i].offset;
			int32_t range_diff = offsets[i + 1].range.first - offsets[i].range.first;
			if(range_diff * CD_DATA_SIZE_SAMPLES == offset_diff)
			{
				offsets[i].range.second = offsets[i + 1].range.second;
				offsets[i].count += offsets[i + 1].count;
				offsets.erase(offsets.begin() + i + 1);

				merge = true;
				break;
			}
		}
	}

	// calculate lba relative offsets
	for(uint32_t i = 0; i < offsets.size(); ++i)
		offsets[i].offset -= (offsets[i].range.first - LBA_START) * CD_DATA_SIZE_SAMPLES;

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
			MSF msf;
			if(_scrap)
				msf = *(MSF *)&samples[i];
			else
				_scrambler.Process((uint8_t *)&msf, (uint8_t *)&samples[i], sizeof(CD_DATA_SYNC), sizeof(msf));

			Record record{{BCDMSF_to_LBA(msf), BCDMSF_to_LBA(msf)}, (int32_t)(offset + i - SYNC_SIZE_SAMPLES), 1};

			if(_records.empty())
				_records.push_back(record);
			else
			{
				auto &b = _records.back();

				uint32_t offset_diff = record.offset - b.offset;
				int32_t range_diff = record.range.first - b.range.first;
				if(range_diff * CD_DATA_SIZE_SAMPLES == offset_diff)
				{
					++b.count;
					b.range.second = record.range.first;
				}
				else
					_records.push_back(record);
			}
			
			_syncSearch = 0;
		}
	}
}

}
