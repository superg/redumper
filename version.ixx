module;
#include <format>
#include <string>

export module version;



namespace gpsxre
{

export std::string redumper_version()
{
    return std::format("redumper (build: {})", REDUMPER_VERSION_BUILD);
}

}
