#pragma once



#include <format>
#include <stdexcept>



#ifdef NDEBUG
#define throw_line(...) throw std::runtime_error(std::format(__VA_ARGS__))
#else
#define throw_line(...) throw std::runtime_error(std::format("{} {{{}:{}}}", std::format(__VA_ARGS__), __FILE__, __LINE__))
#endif
