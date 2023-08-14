module;
#include <filesystem>
#include <ostream>
#include <string>

export module systems.system;



namespace gpsxre
{

export class System
{
public:
	enum class Type
	{
		RAW_DATA,
		RAW_AUDIO,
		ISO
	};

	virtual ~System() = default;

	virtual std::string getName() = 0;
	virtual Type getType() = 0;
	virtual void printInfo(std::ostream &os, const std::filesystem::path &track_path) const = 0;
};

}
