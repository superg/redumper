#include <string>

namespace version
{

std::string build()
{
	return std::string(__DATE__) + ", " + __TIME__;
}

}
