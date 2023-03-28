#include <limits>
#include "silence.hh"



namespace gpsxre
{

SilenceAnalyzer::SilenceAnalyzer(uint16_t silence_threshold, const std::vector<std::pair<int32_t, int32_t>> &index0_ranges)
	: _limit(silence_threshold + 1)
	, _samplesMin(std::numeric_limits<uint32_t>::max())
	, _count(0)
	, _state(std::make_unique<std::pair<int32_t, bool>[]>(_limit))
	, _ranges(_limit)
{
	for(auto const &r : index0_ranges)
	{
		uint32_t length = r.second - r.first;
		if(_samplesMin > length)
			_samplesMin = length;
	}

	std::fill_n(_state.get(), _limit, std::pair(-_samplesMin, true));
}


std::vector<std::vector<std::pair<int32_t, int32_t>>> SilenceAnalyzer::ranges() const
{
	std::vector<std::vector<std::pair<int32_t, int32_t>>> silence_ranges;

	for(int i = 0; i < _limit; ++i)
	{
		std::vector<std::pair<int32_t, int32_t>> silence_range(_ranges[i]);
		silence_range.emplace_back(_state[i].second ? _state[i].first : _count, 0);

		int32_t base = LBA_START * CD_DATA_SIZE_SAMPLES;
		for(auto &r : silence_range)
		{
			r.first += base;
			r.second += base;
		}
		silence_range.front().first = std::numeric_limits<int32_t>::min();
		silence_range.back().second = std::numeric_limits<int32_t>::max();

		silence_ranges.push_back(silence_range);
	}

	return silence_ranges;
}


void SilenceAnalyzer::process(uint32_t *samples, State *state, uint32_t count, uint32_t offset)
{
	_count = offset;
	for(uint32_t n = offset + count; _count < n; ++_count)
	{
		auto sample = (int16_t *)&samples[_count - offset];
		int sample_l = std::abs(sample[0]);
		int sample_r = std::abs(sample[1]);

// bound optimization
#if 1
		int bound = std::min(std::max(sample_l, sample_r), _limit);

		for(int k = 0; k < bound; ++k)
		{
			int32_t &start = _state[k].first;
			bool &silence = _state[k].second;

			if(silence)
			{
				if(_count - start >= (int32_t)_samplesMin)
					_ranges[k].emplace_back(start, _count);
				silence = false;
			}
		}

		for(int k = bound; k < _limit; ++k)
		{
			int32_t &start = _state[k].first;
			bool &silence = _state[k].second;

			if(!silence)
			{
				start = _count;
				silence = true;
			}
		}
#else
		for(int k = 0; k < _limit; ++k)
		{
			int32_t &start = _state[k].first;
			bool &silence = _state[k].second;

			// silence
			if(sample_l <= k && sample_r <= k)
			{
				if(!silence)
				{
					start = _count;
					silence = true;
				}
			}
			// not silence
			else
			{
				if(silence)
				{
					if(_count - start >= (int32_t)_samplesMin)
						_ranges[k].emplace_back(start, _count);
					silence = false;
				}
			}
		}
#endif
	}
}

}
