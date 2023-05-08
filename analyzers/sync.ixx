module;
#include <utility>
#include <vector>

export module analyzers.sync_analyzer;

import analyzers.analyzer;
import cd.cd;
import cd.cdrom;
import cd.scrambler;
import common;



namespace gpsxre
{

export class SyncAnalyzer : public Analyzer
{
public:
	struct Record
	{
		std::pair<int32_t, int32_t> range;
		int32_t sample_offset;
		uint32_t count;
	};


	SyncAnalyzer(bool scrap)
		: _scrap(scrap)
		, _syncSearch(0)
	{
		;
	}


	std::vector<Record> getRecords() const
	{
		return _records;
	}


	void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset) override
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
					_scrambler.process((uint8_t *)&msf, (uint8_t *)&samples[i], sizeof(CD_DATA_SYNC), sizeof(msf));

				Record record{{BCDMSF_to_LBA(msf), BCDMSF_to_LBA(msf)}, sample_offset_a2r(offset + i - SYNC_SIZE_SAMPLES), 1};

				if(_records.empty())
					_records.push_back(record);
				else
				{
					auto &b = _records.back();

					uint32_t offset_diff = record.sample_offset - b.sample_offset;
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

private:
	static constexpr uint32_t SYNC_SIZE_SAMPLES = sizeof(CD_DATA_SYNC) / CD_SAMPLE_SIZE;

	bool _scrap;
	uint32_t _syncSearch;

	Scrambler _scrambler;

	std::vector<Record> _records;
};

}
