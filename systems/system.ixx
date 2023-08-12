module;
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <ostream>

export module systems.system;



namespace gpsxre
{

export class System
{
public:
	virtual ~System() = default;

	virtual void printInfo(std::ostream &os) const = 0;
};


export class Systems
{
public:
	using Creator = std::function<std::unique_ptr<System>(const std::filesystem::path &track_path)>;

	Systems() = delete;

	static std::map<std::string, Creator> &get()
	{
		static std::map<std::string, Creator> systems;
		return systems;
	}

	static void registerCreator(std::string name, Creator creator)
	{
		get().emplace(name, creator);
	}
};


export template<typename T>
class SystemT : public System
{
public:
	SystemT()
	{
		reg;
	}
	
private:
	static bool reg;
	static bool init()
	{
		Systems::registerCreator(T::name(), [](const std::filesystem::path &track_path){ return std::make_unique<T>(track_path); });
		return true;
	}
};
template<class T>
bool SystemT<T>::reg = SystemT<T>::init();

}
