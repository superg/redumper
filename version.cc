export module version;

import <string>;



namespace version
{

export std::string build()
{
	return std::string(__DATE__) + ", " + __TIME__;
}

}
