module;
#include <chrono>
#include <string_view>

export module utils.animation;



namespace gpsxre
{

export char spinner_animation()
{
	static const std::string_view spinner("-\\|/");

	static std::string_view::size_type index;
	static auto t_prev(std::chrono::high_resolution_clock::now());

	auto t = std::chrono::high_resolution_clock::now();
	if(std::chrono::duration_cast<std::chrono::milliseconds>(t - t_prev) > std::chrono::milliseconds(100))
	{
		++index;
		index %= spinner.size();
		t_prev = t;
	}

	return spinner[index];
}

}
