#pragma once



#include <cstddef>
#include <format>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>



// stringify
#define XSTRINGIFY(arg__) STRINGIFY(arg__)
#define STRINGIFY(arg__) #arg__

// meaningful throw
#ifdef NDEBUG
#define throw_line(arg__) throw std::runtime_error(std::format("{}", arg__))
#else
#define throw_line(arg__) throw std::runtime_error(std::format("{} {{{}:{}}}", arg__, __FILE__, __LINE__))
#endif



namespace gpsxre
{

// count of static array elements
template <typename T, int N>
constexpr size_t dim(T(&)[N])
{
    return N;
}

template <typename T, typename U>
T round_up(T value, U base)
{
	base -= 1;
	return (value + (T)base) & ~base;
}

template<typename T, class = typename std::enable_if_t<std::is_unsigned_v<T>>>
void clean_write(T *dst, size_t dst_offset, size_t size, T data)
{
	T mask = (T)(~(T)0 << (sizeof(T) * CHAR_BIT - size)) >> dst_offset;
	*dst = (*dst & ~mask) | (data & mask);
};

template<typename T, class = typename std::enable_if_t<std::is_unsigned_v<T>>>
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

template<typename T>
uint64_t bit_diff(const T *data1, const T *data2, uint64_t count)
{
	uint64_t bits_count = 0;

	for(uint64_t i = 0; i < count; ++i)
		bits_count += std::popcount<T>(data1[i] ^ data2[i]);

	return bits_count;
}

std::string normalize_string(const std::string &s);
std::vector<std::string> tokenize_quoted(const std::string &str, const char *delimiters, const char *quotes);
std::string str_uppercase(const std::string &str);
void replace_all_occurences(std::string &str, const std::string &from, const std::string &to);
std::vector<std::pair<int32_t, int32_t>> string_to_ranges(const std::string &str);
std::string ranges_to_string(const std::vector<std::pair<int32_t, int32_t>> &ranges);
const std::pair<int32_t, int32_t> *inside_range(int32_t lba, const std::vector<std::pair<int32_t, int32_t>> &ranges);
std::string system_date_time(std::string fmt);

}
