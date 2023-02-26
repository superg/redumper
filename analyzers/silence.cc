#include "silence.hh"



namespace gpsxre
{

SilenceAnalyzer::SilenceAnalyzer(uint16_t silence_threshold, const std::vector<std::pair<int32_t, int32_t>> &index0_ranges)
	: _silenceLimit(silence_threshold + 1)
	, _silenceSamplesMin(std::numeric_limits<uint32_t>::max())
	, _silenceStart(std::make_unique<int32_t[]>(_silenceLimit))
	, _silenceRanges(_silenceLimit)
{
	for(auto const &r : index0_ranges)
	{
		uint32_t length = r.second - r.first;
		if(_silenceSamplesMin > length)
			_silenceSamplesMin = length;
	}

	std::fill_n(_silenceStart.get(), _silenceLimit, std::numeric_limits<int32_t>::min());
}


const std::vector<std::vector<std::pair<int32_t, int32_t>>> &SilenceAnalyzer::ranges() const
{
	return _silenceRanges;
}


void SilenceAnalyzer::process(uint32_t *samples, State *state, uint32_t count, uint32_t offset, bool last)
{
	for(uint32_t j = 0; j < count; ++j)
	{
		int32_t position = LBA_START * CD_DATA_SIZE_SAMPLES + offset + j;

		auto sample = (int16_t *)&samples[j];

		for(uint16_t k = 0; k < _silenceLimit; ++k)
		{
			// silence
			if(std::abs(sample[0]) <= (int)k && std::abs(sample[1]) <= (int)k)
			{
				if(_silenceStart[k] == std::numeric_limits<int32_t>::max())
					_silenceStart[k] = position;
			}
			// not silence
			else if(_silenceStart[k] != std::numeric_limits<int32_t>::max())
			{
				if(_silenceStart[k] == std::numeric_limits<int32_t>::min() || position - _silenceStart[k] >= (int32_t)_silenceSamplesMin)
					_silenceRanges[k].emplace_back(_silenceStart[k], position);

				_silenceStart[k] = std::numeric_limits<int32_t>::max();
			}
		}
	}

	// tail
	if(last)
		for(uint16_t k = 0; k < _silenceLimit; ++k)
			_silenceRanges[k].emplace_back(_silenceStart[k] == std::numeric_limits<int32_t>::max() ? LBA_START * CD_DATA_SIZE_SAMPLES + offset + count : _silenceStart[k], std::numeric_limits<int32_t>::max());
}

}
