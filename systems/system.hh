#pragma once



#include <filesystem>
#include <list>
#include <memory>
#include <ostream>
#include <string>



namespace gpsxre
{

class System
{
public:
	virtual ~System() = default;

	virtual std::string getName() const = 0;
	virtual bool isValid() const = 0;
	virtual void print(std::ostream &os) const = 0;

	static std::list<std::shared_ptr<System>> getSystems(const std::filesystem::path &file_path);

private:
	static struct Static
	{
		typedef std::shared_ptr<System> (*Creator)(const std::filesystem::path &);
		static std::list<Creator> _creators;

		Static();
	} _static;
};


}
