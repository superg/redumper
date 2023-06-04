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


export std::string erase_all(std::string s, char c)
{
    erase_all_inplace(s, c);
    return s;
}


export void extend_left_inplace(std::string &s, char c, size_t width)
{
    s = std::string(width - std::min(width, s.length()), c) + s;
}


export std::string extend_left(std::string s, char c, size_t width)
{
    extend_left_inplace(s, c, width);
    return s;
}


export void replace_all_inplace(std::string &s, std::string from, std::string to)
{
    for(size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.length())
        s.replace(pos, from.length(), to);
}


export std::string replace_all(std::string s, const std::string &from, const std::string &to)
{
    replace_all_inplace(s, from, to);
    return s;
}

}
