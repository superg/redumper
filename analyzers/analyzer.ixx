module;
#include <cstdint>

export module analyzers.analyzer;

import utils.misc;



namespace gpsxre
{

export class Analyzer
{
public:
	virtual ~Analyzer() {}

	virtual void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset) = 0;
};

}
