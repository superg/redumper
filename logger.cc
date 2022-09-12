#include "common.hh"
#include "logger.hh"



namespace gpsxre
{

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


void Logger::ClearLine()
{
    // default 80 terminal width - 1 is the largest value which doesn't wrap to a new line on Windows 7
    std::cout << std::format("\r{:79}\r", "");
}


Logger &Logger::Get()
{
    return _logger;
}


void LOG_R()
{
    Logger::Get().ClearLine();
}

}
