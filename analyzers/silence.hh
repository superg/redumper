#pragma once



#include <utility>
#include <vector>
#include "analyzer.hh"



namespace gpsxre
{

class SilenceAnalyzer : public Analyzer
{
public:
	SilenceAnalyzer(uint16_t silence_threshold, uint32_t samples_min);

	std::vector<std::vector<std::pair<int32_t, int32_t>>> ranges() const;

	void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset) override;

private:
	int _limit;
	uint32_t _samplesMin;

	uint32_t _count;

	// don't use std::vector here because it's too slow
	std::unique_ptr<std::pair<int32_t, bool>[]> _state;

	std::vector<std::vector<std::pair<int32_t, int32_t>>> _ranges;
};

}
