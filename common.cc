#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <set>
#include <sstream>
#include "common.hh"



namespace gpsxre
{

std::string normalize_string(const std::string &s)
{
	std::string ns;

	std::istringstream iss(s);
	for(std::string token; std::getline(iss, token, ' '); )
	{
		if(!token.empty())
			ns += token + ' ';
	}
	if(!ns.empty())
		ns.pop_back();

	return ns;
}


std::vector<std::string> tokenize_quoted(const std::string &str, const char *delimiters, const char *quotes)
{
    std::vector<std::string> tokens;

    std::set<char> delimiter;
    for(auto d = delimiters; *d != '\0'; ++d)
        delimiter.insert(*d);

    bool in = false;
    std::string::const_iterator s;
    for(auto it = str.begin(); it < str.end(); ++it)
    {
        if(in)
        {
            // quoted
            if(*s == quotes[0])
            {
                if(*it == quotes[1])
                {
                    ++s;
                    tokens.emplace_back(s, it);
                    in = false;
                }
            }
            // unquoted
            else
            {
                if(delimiter.find(*it) != delimiter.end())
                {
                    tokens.emplace_back(s, it);
                    in = false;
                }
            }
        }
        else
        {
            if(delimiter.find(*it) == delimiter.end())
            {
                s = it;
                in = true;
            }
        }
    }

    // remaining entry
    if(in)
        tokens.emplace_back(s, str.end());

    return tokens;
}


std::string str_uppercase(const std::string &str)
{
    std::string str_uc;
    std::transform(str.begin(), str.end(), std::back_inserter(str_uc), [](unsigned char c) { return std::toupper(c); });

    return str_uc;
}


void replace_all_occurences(std::string &str, const std::string &from, const std::string &to)
{
    for(size_t pos = 0; (pos = str.find(from, pos)) != std::string::npos; pos += to.length())
        str.replace(pos, from.length(), to);
}


std::vector<std::pair<int32_t, int32_t>> string_to_ranges(const std::string &str)
{
    std::vector<std::pair<int32_t, int32_t>> ranges;

    std::istringstream iss(str);
    for(std::string range; std::getline(iss, range, ':'); )
    {
        std::istringstream range_ss(range);

        std::pair<int32_t, int32_t> r;

        std::string s;
        std::getline(range_ss, s, '-');
        r.first = stoi(s);
        std::getline(range_ss, s, '-');
        r.second = stoi(s) + 1;

        ranges.push_back(r);
    }

    return ranges;
}


std::string ranges_to_string(const std::vector<std::pair<int32_t, int32_t>> &ranges)
{
    std::string str;

    for(auto const &r : ranges)
        str += std::format("{}-{}:", r.first, r.second - 1);

    if(!str.empty())
        str.pop_back();

    return str;
}


const std::pair<int32_t, int32_t> *inside_range(int32_t lba, const std::vector<std::pair<int32_t, int32_t>> &ranges)
{
    for(auto const &r : ranges)
        if(lba >= r.first && lba < r.second)
            return &r;

    return nullptr;
}


std::string system_date_time(std::string fmt)
{
    auto time_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(localtime(&time_now), fmt.c_str());
    return ss.str();
}

}
