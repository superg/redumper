#pragma once



#include <stdexcept>
#include <format>



#ifdef NDEBUG
#define throw_line(fmt, ...) throw std::runtime_error(std::format(fmt, __VA_ARGS__))
#else
#define throw_line(fmt, ...) throw std::runtime_error(std::format("{} {{{}:{}}}", std::format(fmt, __VA_ARGS__), __FILE__, __LINE__))
#endif
