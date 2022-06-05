#pragma once



#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>



namespace gpsxre
{

class Logger
{
public:
	static Logger &Get();

	template<typename... Args>
	void Log(std::string fmt, const Args &... args)
	{
		auto message = std::format(fmt, args...);
		std::cout << message << std::endl;
		if(_fs.is_open())
			_fs << message << std::endl;
	}

	bool Reset(std::filesystem::path log_path);

private:
	static Logger _logger;

	std::filesystem::path _log_path;
	std::fstream _fs;
};

template<typename... Args>
void LOG(std::string fmt, const Args &... args)
{
	Logger::Get().Log(fmt, std::forward<const Args>(args)...);
}

}
