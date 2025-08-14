module;
#include <format>
#include <string>

export module version;



#define XSTRINGIFY(arg__) STRINGIFY(arg__)
#define STRINGIFY(arg__) #arg__



namespace gpsxre
{

export std::string redumper_version()
{
    return std::format("redumper (build: {})", XSTRINGIFY(REDUMPER_VERSION_BUILD));
}

}
