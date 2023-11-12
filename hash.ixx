module;
#include <filesystem>
#include <fstream>
#include <vector>
#include "throw_line.hh"

export module hash;

import cd.cd;
import dump;
import options;
import rom_entry;
import utils.animation;
import utils.logger;
import utils.misc;



namespace gpsxre
{

void progress_output(uint64_t byte, uint64_t bytes_count)
{
	char animation = byte == bytes_count ? '*' : spinner_animation();

	LOGC_RF("{} [{:3}%] hashing", animation, byte * 100 / bytes_count);
}


export void redumper_hash(Context &ctx, Options &options)
{
	std::vector<std::string> dat(ctx.dat);

	if(dat.empty())
	{
		if(options.image_name.empty())
			throw_line("image name is not provided");

		auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

		std::vector<std::filesystem::path> files;
		if(std::filesystem::exists(image_prefix + ".cue"))
		{
			for(auto const &t : cue_get_entries(image_prefix + ".cue"))
				files.push_back(std::filesystem::path(options.image_path) / t.first);
		}
		else if(std::filesystem::exists(image_prefix + ".iso"))
		{
			files.push_back(image_prefix + ".iso");
		}

		if(!files.empty())
		{
			uint64_t byte = 0;
			uint64_t bytes_count = 0;
			for(auto f : files)
				bytes_count += std::filesystem::file_size(f);

			std::vector<uint8_t> sector(CD_DATA_SIZE);
			for(auto f : files)
			{
				std::fstream fs(f, std::fstream::in | std::fstream::binary);
				if(!fs.is_open())
					throw_line("unable to open file ({})", f.filename().string());

				ROMEntry rom_entry(f.filename().string());

				std::vector<uint8_t> data(1024 * 1024); // 1Mb chunk
				batch_process_range<uint64_t>(std::pair(0, std::filesystem::file_size(f)), data.size(), [&](uint64_t offset, uint64_t size) -> bool
				{
					progress_output(byte, bytes_count);

					fs.read((char *)data.data(), size);
					if(fs.fail())
						throw_line("read failed ({})", f.filename().string());

					rom_entry.update(data.data(), size);

					byte += size;

					return false;
				});

				dat.push_back(rom_entry.xmlLine());
			}

			progress_output(bytes_count, bytes_count);
			LOG("");
			LOG("");
		}
	}

	if(!dat.empty())
	{
		LOG("dat:");
		for(auto l : dat)
			LOG("{}", l);
	}
}

}
