#pragma once



#include <utility>
#include <vector>
#include "scrambler.hh"
#include "analyzer.hh"



namespace gpsxre
{

class SyncAnalyzer : public Analyzer
{
public:
	struct Record
	{
		std::pair<int32_t, int32_t> range;
		uint32_t offset;
		uint32_t count;
	};

	SyncAnalyzer(bool scrap);

	std::vector<Record> getRecords() const;

	void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset) override;

private:
	static constexpr uint32_t SYNC_SIZE_SAMPLES = sizeof(CD_DATA_SYNC) / CD_SAMPLE_SIZE;

	bool _scrap;
	uint32_t _syncSearch;

	Scrambler _scrambler;

	std::vector<Record> _records;
};

}
