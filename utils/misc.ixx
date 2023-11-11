module;
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include "throw_line.hh"

export module utils.misc;

import cd.cd;



namespace gpsxre
{

export template <typename T, size_t N>
constexpr size_t countof(T(&)[N])
{
	return N;
}


export template <typename T, class = typename std::enable_if_t<std::is_unsigned_v<T>>>
constexpr T round_up_pow2(T value, T multiple)
{
	multiple -= 1;
	return (value + multiple) & ~multiple;
}


export template <typename T, typename U, class = typename std::enable_if_t<std::is_unsigned_v<U>>>
constexpr T scale_up(T value, U multiple)
{
	assert(multiple);
	T sign = value > 0 ? +1 : (value < 0 ? -1 : 0);
	return (value - sign) / (T)multiple + sign;
}


export template <typename T, typename U, class = typename std::enable_if_t<std::is_unsigned_v<U>>>
constexpr T scale_down(T value, U multiple)
{
	assert(multiple);
	return value / (T)multiple;
}


export template <typename T, typename U, class = typename std::enable_if_t<std::is_unsigned_v<U>>>
constexpr T scale_left(T value, U multiple)
{
	return value < 0 ? scale_up(value, multiple) : scale_down(value, multiple);
}


export template <typename T, typename U, class = typename std::enable_if_t<std::is_unsigned_v<U>>>
constexpr T scale_right(T value, U multiple)
{
	return value < 0 ? scale_down(value, multiple) : scale_up(value, multiple);
}


export template <typename T, typename U, class = typename std::enable_if_t<std::is_unsigned_v<U>>>
constexpr T round_up(T value, U multiple)
{
	return scale_up(value, multiple) * (T)multiple;
}


export template <typename T, typename U, class = typename std::enable_if_t<std::is_unsigned_v<U>>>
constexpr T round_down(T value, U multiple)
{
	return scale_down(value, multiple) * (T)multiple;
}


export template<typename T, class = typename std::enable_if_t<std::is_unsigned_v<T>>>
void clean_write(T *dst, size_t dst_offset, size_t size, T data)
{
	T mask = (T)(~(T)0 << (sizeof(T) * CHAR_BIT - size)) >> dst_offset;
	*dst = (*dst & ~mask) | (data & mask);
};


export template<typename T>
bool is_zeroed(const T *data, uint64_t count)
{
	for(uint64_t i = 0; i < count; ++i)
		if(data[i])
			return false;

	return true;
}


export template<typename T, class = typename std::enable_if_t<std::is_unsigned_v<T>>>
void bit_copy(T *dst, size_t dst_offset, const T *src, size_t src_offset, size_t size)
{
	constexpr size_t BLOCK_SIZE = sizeof(T) * CHAR_BIT;

	// skip to the area of interest
	src += src_offset / BLOCK_SIZE;
	dst += dst_offset / BLOCK_SIZE;
	src_offset %= BLOCK_SIZE;
	dst_offset %= BLOCK_SIZE;

	// aligned copy
	if(src_offset == dst_offset)
	{
		// head
		if(dst_offset)
		{
			size_t size_to_write = std::min(size, BLOCK_SIZE - dst_offset);
			clean_write(dst, dst_offset, size_to_write, *src);
			++src;
			++dst;

			size -= size_to_write;
		}

		// body
		size_t body_size = size / BLOCK_SIZE;
		size %= BLOCK_SIZE;
		memcpy(dst, src, body_size);
		src += body_size;
		dst += body_size;

		// tail
		if(size)
			clean_write(dst, 0, size, *src);
	}
	// unaligned copy
	else
	{
		size_t size_to_write = std::min(size, BLOCK_SIZE - dst_offset);

		// head
		size_t lshift, rshift;
		T c;
		if(src_offset < dst_offset)
		{
			rshift = dst_offset - src_offset;
			lshift = BLOCK_SIZE - rshift;

			c = *src >> rshift;
		}
		else
		{
			lshift = src_offset - dst_offset;
			rshift = BLOCK_SIZE - lshift;

			c = *src++ << lshift;
			if(BLOCK_SIZE - src_offset < size)
				c |= *src >> rshift;
		}
		clean_write(dst, dst_offset, size_to_write, c);
		++dst;

		size -= size_to_write;

		// body
		size_t body_size = size / BLOCK_SIZE;
		size %= BLOCK_SIZE;
		for(size_t i = 0; i < body_size; ++i)
		{
			c = *src++ << lshift;
			*dst++ = c | *src >> rshift;
		}

		// tail
		if(size)
		{
			c = *src << lshift;
			if(BLOCK_SIZE - lshift < size)
				c |= *++src >> rshift;

			clean_write(dst, 0, size, c);
		}
	}
}


export template<typename T>
uint32_t bits_count(T value)
{
	uint32_t count = 0;

	for(; value; ++count)
		value &= value - 1;

	return count;
}


export template<typename T>
uint64_t bit_diff(const T *data1, const T *data2, uint64_t count)
{
	uint64_t diff = 0;

	for(uint64_t i = 0; i < count; ++i)
		diff += bits_count(data1[i] ^ data2[i]);

	return diff;
}


export template<typename T>
constexpr T bits_reflect(T word)
{
	T r = word;

	int s = sizeof(word) * CHAR_BIT - 1;

	for(word >>= 1; word; word >>= 1)
	{
		r <<= 1;
		r |= word & 1;
		--s;
	}
	r <<= s;

	return r;
}


export template<typename T>
std::string dictionary_values(const std::map<T, std::string> &dictionary)
{
	std::string values;

	std::string delimiter;
	for(auto &d : dictionary)
	{
		values += delimiter + d.second;
		if(delimiter.empty())
			delimiter = ", ";
	}

	return values;
}


export template<typename T>
std::string enum_to_string(T value, const std::map<T, std::string> &dictionary)
{
	auto it = dictionary.find(value);
	if(it == dictionary.end())
		throw_line("enum_to_string failed, no such value in dictionary (possible values: {})", dictionary_values(dictionary));

	return it->second;

}


export template<typename T>
T string_to_enum(std::string value, const std::map<T, std::string> &dictionary)
{
	for(auto &d : dictionary)
		if(d.second == value)
			return d.first;

	throw_line("string_to_enum failed, no such value in dictionary (possible values: {})", dictionary_values(dictionary));

	//FIXME: find good way to suppress "-Wreturn-type" warning
	return (T)0;
}


export template<typename T>
T diff_bytes_count(const uint8_t *data1, const uint8_t *data2, T size)
{
	T diff = 0;

	for(T i = 0; i < size; ++i)
		if(data1[i] != data2[i])
			++diff;

	return diff;
}


export template<typename T>
T digits_count(T value)
{
	return (value ? log10(value) : 0) + 1;
}


export template<typename T>
bool batch_process_range(const std::pair<T, T> &range, T batch_size, const std::function<bool(T, T)> &func)
{
	bool interrupted = false;

	for(T offset = range.first; offset != range.second;)
	{
		T size = std::min(range.second - offset, batch_size);

		T offset_next = offset + size;

		if(func(offset, size))
		{
			interrupted = true;
			break;
		}

		offset = offset_next;
	}

	return interrupted;
}


export std::string normalize_string(const std::string &s)
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


export std::vector<std::string> tokenize(const std::string &str, const char *delimiters, const char *quotes)
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


export std::optional<uint64_t> str_to_uint64(std::string::const_iterator str_begin, std::string::const_iterator str_end)
{
	uint64_t value = 0;

	bool valid = false;
	for(auto it = str_begin; it != str_end; ++it)
	{
		if(std::isdigit(*it))
		{
			value = (value * 10) + (*it - '0');
			valid = true;
		}
		else
		{
			valid = false;
			break;
		}
	}

	return valid ? std::make_optional(value) : std::nullopt;
}


export std::optional<uint64_t> str_to_uint64(const std::string &str)
{
	return str_to_uint64(str.cbegin(), str.cend());
}


export std::optional<int64_t> str_to_int64(std::string::const_iterator str_begin, std::string::const_iterator str_end)
{
	// empty string
	auto it = str_begin;
	if(it == str_end)
		return std::nullopt;

	// preserve sign
	int64_t negative = 1;
	if(*it == '+')
		++it;
	else if(*it == '-')
	{
		negative = -1;
		++it;
	}

	if(auto value = str_to_uint64(it, str_end))
		return std::make_optional(negative * *value);

	return std::nullopt;
}


export std::optional<uint64_t> str_to_int64(const std::string &str)
{
	return str_to_int64(str.cbegin(), str.cend());
}


export int64_t str_to_int(const std::string &str)
{
	auto value = str_to_int64(str);
	if(!value)
		throw_line("string is not an integer number ({})", str);

	return *value;
}


export std::optional<double> str_to_double(std::string::const_iterator str_begin, std::string::const_iterator str_end)
{
	// empty string
	auto it = str_begin;
	if(it == str_end)
		return std::nullopt;

	// preserve sign
	double negative = 1;
	if(*it == '+')
		++it;
	else if(*it == '-')
	{
		negative = -1;
		++it;
	}

	auto dot = std::find(it, str_end, '.');

	if(auto whole = str_to_uint64(it, dot))
	{
		if(dot == str_end)
			return std::make_optional(negative * *whole);
		else
		{
			if(auto decimal = str_to_uint64(dot + 1, str_end))
			{
				auto fraction = *decimal / pow(10, digits_count(*decimal));

				return std::make_optional(negative * (*whole + fraction));
			}
		}
	}

	return std::nullopt;
}

export double str_to_double(const std::string &str)
{
	auto value = str_to_double(str.cbegin(), str.cend());
	if(!value)
		throw_line("string is not a double number ({})", str);

	return *value;
}


export std::vector<std::pair<int32_t, int32_t>> string_to_ranges(const std::string &str)
{
	std::vector<std::pair<int32_t, int32_t>> ranges;

	std::istringstream iss(str);
	for(std::string range; std::getline(iss, range, ':'); )
	{
		std::istringstream range_ss(range);
		std::string s;

		std::getline(range_ss, s, '-');
		uint32_t lba_start = str_to_int(s);

		std::getline(range_ss, s, '-');
		uint32_t lba_end = str_to_int(s) + 1;

		ranges.emplace_back(lba_start, lba_end);
	}

	return ranges;
}


export std::string ranges_to_string(const std::vector<std::pair<int32_t, int32_t>> &ranges)
{
	std::string str;

	for(auto const &r : ranges)
		str += std::format("{}-{}:", r.first, r.second - 1);

	if(!str.empty())
		str.pop_back();

	return str;
}


export const std::pair<int32_t, int32_t> *inside_range(int32_t lba, const std::vector<std::pair<int32_t, int32_t>> &ranges)
{
	for(auto const &r : ranges)
		if(lba >= r.first && lba < r.second)
			return &r;

	return nullptr;
}


export std::string system_date_time(std::string fmt)
{
	auto time_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::stringstream ss;
	ss << std::put_time(localtime(&time_now), fmt.c_str());
	return ss.str();
}


//FIXME: just do regexp
export std::string track_extract_basename(std::string str)
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


export template<unsigned int bits, class T>
T sign_extend(T value)
{
	// clear extra bits
	T v = value & (1 << bits) - 1;

	T m = (T)1 << bits - 1;
	return (v ^ m) - m;
}


export bool number_is_year(uint32_t year)
{
	// reasonable unixtime range
	return year >= 1970 && year < 2038;
}


export bool number_is_month(uint32_t month)
{
	return month >= 1 && month <= 12;
}


export std::list<std::pair<std::string, bool>> cue_get_entries(const std::filesystem::path &cue_path)
{
	std::list<std::pair<std::string, bool>> entries;

	std::fstream fs(cue_path, std::fstream::in);
	if(!fs.is_open())
		throw_line("unable to open file ({})", cue_path.filename().string());

	std::pair<std::string, bool> entry;
	std::string line;
	while(std::getline(fs, line))
	{
		auto tokens(tokenize(line, " \t", "\"\""));
		if(tokens.size() == 3)
		{
			if(tokens[0] == "FILE")
				entry.first = tokens[1];
			else if(tokens[0] == "TRACK" && !entry.first.empty())
			{
				entry.second = tokens[2] != "AUDIO";
				entries.push_back(entry);
				entry.first.clear();
			}
		}
	}

	return entries;
}

}
