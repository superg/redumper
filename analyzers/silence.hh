#pragma once



#include <utility>
#include <vector>
#include "analyzer.hh"



namespace gpsxre
{

class SilenceAnalyzer : public Analyzer
{
public:
	SilenceAnalyzer(uint16_t silence_threshold, const std::vector<std::pair<int32_t, int32_t>> &index0_ranges);

	const std::vector<std::vector<std::pair<int32_t, int32_t>>> &ranges() const;

	void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset, bool last) override;

private:
	uint16_t _silenceLimit;
	uint32_t _silenceSamplesMin;

	// don't use std::vector here because it's too slow
	std::unique_ptr<int32_t[]> _silenceStart;

	std::vector<std::vector<std::pair<int32_t, int32_t>>> _silenceRanges;
};

}
