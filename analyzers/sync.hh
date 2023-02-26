#pragma once



#include <tuple>
#include <vector>
#include "scrambler.hh"
#include "analyzer.hh"



namespace gpsxre
{

class SyncAnalyzer : public Analyzer
{
public:
	SyncAnalyzer(bool scrap);

	std::vector<std::pair<int32_t, int32_t>> getOffsets() const;

	void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset, bool last) override;

private:
	static constexpr uint32_t SYNC_SIZE_SAMPLES = sizeof(CD_DATA_SYNC) / CD_SAMPLE_SIZE;

	bool _scrap;
	uint32_t _syncSearch;

	Scrambler _scrambler;

	typedef std::pair<MSF, uint32_t> Record;
	Record _currentRecord;
	std::vector<Record> _records;
};

}
