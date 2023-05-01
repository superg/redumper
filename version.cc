module;
#include <string>

export module version;

namespace version
{

export std::string build()
{
	return std::string(__DATE__) + ", " + __TIME__;
}

}
