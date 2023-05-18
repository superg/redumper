module;

#include <algorithm>
#include <cctype>
#include <string>

export module utils.strings;



namespace gpsxre
{

void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c) { return !std::isspace(c); }));
}


void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](char c) { return !std::isspace(c); }).base(), s.end());
}


export void trim_inplace(std::string &s)
{
    rtrim(s);
    ltrim(s);
}


export std::string trim(std::string s)
{
    trim_inplace(s);
    return s;
}

}
