module;
#include <cassert>
#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <iomanip>
#include <set>
#include <sstream>

export module common;

import cd.cd;



namespace gpsxre
{

export constexpr uint32_t SLOW_SECTOR_TIMEOUT = 5;
#if 1
export int32_t LBA_START = MSF_to_LBA(MSF_LEADIN_START); // -45150
#else
// easier debugging, LBA starts with 0, plextor lead-in and asus cache are disabled
export constexpr int32_t LBA_START = 0;
// GS2v3   13922 .. 17080-17090
// GS2_1.1 12762 .. 17075
// GS2_5.5 12859 .. 17130-17140
// GS2_1.2 12739 .. 16930-16940
// SC DISC  8546 .. 17100-17125
// SC BOX  10547 .. 16940-16950
// CB4 6407-7114 ..  9200- 9220
// GS GCD   9162 .. 17000-17010  // F05 0004
// XPLO FM  7770 .. 10700-10704
//static constexpr int32_t LBA_START = MSF_to_LBA(MSF_LEADIN_START);
#endif


export enum class State : uint8_t
{
	ERROR_SKIP, // must be first to support random offset file writes
	ERROR_C2,
	SUCCESS_C2_OFF,
	SUCCESS_SCSI_OFF,
	SUCCESS
};

export template<typename... Args>
void throw_line(const std::string fmt, const Args &... args)
{
	auto message = std::vformat(fmt, std::make_format_args(args...));

#ifdef NDEBUG
	throw std::runtime_error(message);
#else
	throw std::runtime_error(std::format("{} {{{}:{}}}", message, __FILE__, __LINE__));
#endif
}

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
	//GGG
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


export std::string str_uppercase(const std::string &str)
{
	std::string str_uc;
	std::transform(str.begin(), str.end(), std::back_inserter(str_uc), [](unsigned char c) { return std::toupper(c); });

	return str_uc;
}


export void replace_all_occurences(std::string &str, const std::string &from, const std::string &to)
{
	for(size_t pos = 0; (pos = str.find(from, pos)) != std::string::npos; pos += to.length())
		str.replace(pos, from.length(), to);
}


export long long stoll_strict(const std::string &str)
{
	size_t idx = 0;
	long long number = std::stoll(str, &idx);

	// suboptimal but at least something
	if(idx != str.length())
		throw std::invalid_argument("invalid stol argument");

	return number;
}


export std::vector<std::pair<int32_t, int32_t>> string_to_ranges(const std::string &str)
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


export bool stoll_try(long long &value, const std::string &str)
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


export int32_t sample_offset_a2r(uint32_t absolute)
{
	return absolute + (LBA_START * CD_DATA_SIZE_SAMPLES);
}


export uint32_t sample_offset_r2a(int32_t relative)
{
	return relative - (LBA_START * CD_DATA_SIZE_SAMPLES);
}

}
