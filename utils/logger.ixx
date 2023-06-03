module;
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>

export module utils.logger;

import utils.misc;



namespace gpsxre
{

export class Logger
{
public:
	static Logger &get()
	{
		static Logger logger;

		return logger;
	}


	Logger &setFile(std::filesystem::path log_path)
	{
		auto pp = log_path.parent_path();
		if(!pp.empty())
			std::filesystem::create_directories(pp);

		bool nl = false;
		if(std::filesystem::exists(log_path))
			nl = true;

		_fs.open(log_path, std::fstream::out | std::fstream::app);
		if(_fs.fail())
			throw_line("unable to open file ({})", log_path.filename().string());

		if(nl)
			_fs << std::endl;

		auto dt = system_date_time(" %F %T ");
		_fs << std::format("{}{}{}", std::string(3, '='), dt, std::string(_LINE_WIDTH - 3 - dt.length(), '=')) << std::endl;

		return Logger::get();
	}


	template<typename... Args>
	constexpr Logger &log(bool file, std::format_string<Args...> fmt, Args &&... args)
	{
		auto message = std::format(fmt, std::forward<Args>(args)...);
		_currentLength = message.length();
		
		if(_eraseLength > _currentLength)
		{
			message += std::string(_eraseLength - _currentLength, ' ');
			_eraseLength = 0;
		}

		std::cout << message;

		if(file && _fs.is_open())
			_fs << message;

		return Logger::get();
	}


	Logger &NL(bool file)
	{
		_currentLength = 0;
		_eraseLength = 0;

		std::cout << std::endl;
		if(file && _fs.is_open())
			_fs << std::endl;

		return Logger::get();
	}


	Logger &RL()
	{
		_eraseLength = _currentLength;
		_currentLength = 0;

		std::cout << '\r';

		return Logger::get();
	}


	Logger &flush(bool file)
	{
		std::cout << std::flush;
		if(file && _fs.is_open())
			_fs << std::flush;

		return Logger::get();
	}


	Logger &returnLine(bool erase)
	{
		// default 80 terminal width - 1 is the largest value which doesn't wrap to a new line on Windows 7
		if(erase)
			std::cout << std::format("\r{:79}", "");
		std::cout << '\r';

		return Logger::get();
	}

private:
	static constexpr unsigned int _LINE_WIDTH = 80;

	std::fstream _fs;

	unsigned int _currentLength = 0;
	unsigned int _eraseLength = 0;
};


// log message followed by a new line (console & file)
export template<typename... Args>
constexpr void LOG(std::format_string<Args...> fmt, Args &&... args)
{
	Logger::get().log(true, fmt, std::forward<Args>(args)...).NL(true);
}


// log message and flush, no new line (console & file)
export template<typename... Args>
constexpr void LOG_F(std::format_string<Args...> fmt, Args &&... args)
{
	Logger::get().log(true, fmt, std::forward<Args>(args)...).flush(true);
}


// log message followed by a new line (console only)
export template<typename... Args>
constexpr void LOGC(std::format_string<Args...> fmt, Args &&... args)
{
	Logger::get().log(false, fmt, std::forward<Args>(args)...).NL(false);
}


// log message and flush, no new line (console only)
export template<typename... Args>
constexpr void LOGC_F(std::format_string<Args...> fmt, Args &&... args)
{
	Logger::get().log(false, fmt, std::forward<Args>(args)...).flush(false);
}


export template<typename... Args>
constexpr void LOGC_RF(std::format_string<Args...> fmt, Args &&... args)
{
	Logger::get().RL().log(false, fmt, std::forward<Args>(args)...).flush(false);
}


// return line (console only)
export void LOG_R()
{
	Logger::get().returnLine(false);
}


// erase line / return line (console only)
export void LOG_ER()
{
	Logger::get().returnLine(true);
}

}
