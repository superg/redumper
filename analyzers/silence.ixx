module;
#include <limits>
#include <memory>
#include <utility>
#include <vector>

export module analyzers.silence_analyzer;

import analyzers.analyzer;
import cd.cd;
import cd.common;
import utils.misc;



namespace gpsxre
{

export class SilenceAnalyzer : public Analyzer
{
public:
    SilenceAnalyzer(uint16_t silence_threshold, uint32_t samples_min)
        : _limit(silence_threshold + 1)
        , _samplesMin(samples_min)
        , _count(0)
        , _state(std::make_unique<std::pair<int32_t, bool>[]>(_limit))
        , _ranges(_limit)
    {
        std::fill_n(_state.get(), _limit, std::pair(-_samplesMin, true));
    }


    std::vector<std::vector<std::pair<int32_t, int32_t>>> ranges() const
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


    void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset) override
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

private:
    int _limit;
    uint32_t _samplesMin;

    uint32_t _count;

    // don't use std::vector here because it's too slow
    std::unique_ptr<std::pair<int32_t, bool>[]> _state;

    std::vector<std::vector<std::pair<int32_t, int32_t>>> _ranges;
};

}
