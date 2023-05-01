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
	void Log(bool file, std::string fmt, const Args &... args)
	{
		auto message = std::vformat(fmt, std::make_format_args(args...));

		std::cout << message;

		if(file && _fs.is_open())
			_fs << message;
	}

	bool Reset(std::filesystem::path log_path);

	void NL(bool file = true);
	void Flush(bool file);
	void ReturnLine(bool erase);

private:
	static Logger _logger;

	std::filesystem::path _log_path;
	std::fstream _fs;
};


// log message followed by a new line (console & file)
template<typename... Args>
void LOG(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(true, fmt, std::forward<const Args>(args)...);
	logger.NL(true);
}


// log message and flush, no new line (console & file)
template<typename... Args>
void LOG_F(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(true, fmt, std::forward<const Args>(args)...);
	logger.Flush(true);
}


// log message followed by a new line (console only)
template<typename... Args>
void LOGC(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(false, fmt, std::forward<const Args>(args)...);
	logger.NL(false);
}


// log message and flush, no new line (console only)
template<typename... Args>
void LOGC_F(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(false, fmt, std::forward<const Args>(args)...);
	logger.Flush(false);
}


// return line (console only)
void LOG_R();


// erase line / return line (console only)
void LOG_ER();

}
