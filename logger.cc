module;
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include "common.hh"

export module logger;



namespace gpsxre
{

export class Logger
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
export template<typename... Args>
void LOG(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(true, fmt, std::forward<const Args>(args)...);
	logger.NL(true);
}


// log message and flush, no new line (console & file)
export template<typename... Args>
void LOG_F(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(true, fmt, std::forward<const Args>(args)...);
	logger.Flush(true);
}


// log message followed by a new line (console only)
export template<typename... Args>
void LOGC(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(false, fmt, std::forward<const Args>(args)...);
	logger.NL(false);
}


// log message and flush, no new line (console only)
export template<typename... Args>
void LOGC_F(std::string fmt, const Args &... args)
{
	auto &logger = Logger::Get();
	logger.Log(false, fmt, std::forward<const Args>(args)...);
	logger.Flush(false);
}


// return line (console only)
export void LOG_R();


// erase line / return line (console only)
export void LOG_ER();

Logger Logger::_logger;


bool Logger::Reset(std::filesystem::path log_path)
{
	bool reset = false;
	if(_log_path != log_path)
	{
		_log_path = log_path;
		if(_fs.is_open())
			_fs.close();

		if(!_log_path.empty())
		{
			auto pp = log_path.parent_path();
			if(!pp.empty())
				std::filesystem::create_directories(pp);

			bool nl = false;
			if(std::filesystem::exists(log_path))
				nl = true;

			_fs.open(log_path, std::fstream::out | std::fstream::app);
			if(_fs.fail())
				throw_line(std::format("unable to open file ({})", log_path.filename().string()));

			if(nl)
				_fs << std::endl;

			auto dt = system_date_time(" %F %T ");
			_fs << std::format("{}{}{}", std::string(3, '='), dt, std::string(80 - 3 - dt.length(), '=')) << std::endl;
		}

		reset = true;
	}

	return reset;
}


void Logger::NL(bool file)
{
	std::cout << std::endl;
	if(file && _fs.is_open())
		_fs << std::endl;
}


void Logger::Flush(bool file)
{
	std::cout << std::flush;
	if(file && _fs.is_open())
		_fs << std::flush;
}


void Logger::ReturnLine(bool erase)
{
	// default 80 terminal width - 1 is the largest value which doesn't wrap to a new line on Windows 7
	if(erase)
		std::cout << std::format("\r{:79}", "");
	std::cout << '\r';
}


Logger &Logger::Get()
{
	return _logger;
}


void LOG_R()
{
	Logger::Get().ReturnLine(false);
}


void LOG_ER()
{
	Logger::Get().ReturnLine(true);
}

}
