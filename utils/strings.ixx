module;

#include <algorithm>
#include <cctype>
#include <string>

export module utils.strings;



namespace gpsxre
{

export void trim_left_inplace(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c) { return !std::isspace(c); }));
}


export void trim_right_inplace(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](char c) { return !std::isspace(c); }).base(), s.end());
}


export void trim_inplace(std::string &s)
{
    trim_right_inplace(s);
    trim_left_inplace(s);
}


export std::string trim(std::string s)
{
    trim_inplace(s);
    return s;
}


export void erase_all_inplace(std::string &s, char c)
{
    s.erase(std::remove(s.begin(), s.end(), c), s.end());
}

}
