#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
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


std::vector<std::string> tokenize(const std::string &str, const char *delimiters, const char *quotes)
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
			if(quotes != nullptr && *s == quotes[0])
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
		r.first = stoll_strict(s);
		std::getline(range_ss, s, '-');
		r.second = stoll_strict(s) + 1;

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


//FIXME: just do regexp
std::string track_extract_basename(std::string str)
{
	std::string basename = str;

	// strip extension
	{
		auto pos = basename.find_last_of('.');
		if(pos != std::string::npos)
			basename = std::string(basename, 0, pos);
	}

	// strip (Track X)
	{
		auto pos = str.find(" (Track ");
		if(pos != std::string::npos)
			basename = std::string(basename, 0, pos);
	}

	return basename;
}


long long stoll_strict(const std::string &str)
{
	size_t idx = 0;
	long long number = std::stoll(str, &idx);

	// suboptimal but at least something
	if(idx != str.length())
		throw std::invalid_argument("invalid stol argument");

	return number;
}


bool stoll_try(long long &value, const std::string &str)
{
	bool success = true;
	
	try
	{
		value = stoll_strict(str);
	}
	catch(...)
	{
		success = false;
	}
	
	return success;
}


int32_t sample_offset_a2r(uint32_t absolute)
{
	return absolute + (LBA_START * CD_DATA_SIZE_SAMPLES);
}


uint32_t sample_offset_r2a(int32_t relative)
{
	return relative - (LBA_START * CD_DATA_SIZE_SAMPLES);
}

}
