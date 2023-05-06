#pragma once



#include <cstdint>

import common;



namespace gpsxre
{

class Analyzer
{
public:
	virtual ~Analyzer() {}

	virtual void process(uint32_t *samples, State *state, uint32_t count, uint32_t offset) = 0;
};

}
