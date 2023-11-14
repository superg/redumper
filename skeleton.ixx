module;

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "throw_line.hh"

export module skeleton;

import cd.cd;
import cd.cdrom;
import dump;
import filesystem.iso9660;
import options;
import readers.sector_reader;
import readers.form1_reader;
import readers.image_bin_form1_reader;
import readers.image_iso_form1_reader;
import utils.logger;
import utils.misc;



namespace gpsxre
{

void skeleton(const std::string &image_prefix, const std::string &image_path, bool iso, Options &options)
{
	std::filesystem::path skeleton_path(image_prefix + ".skeleton");
	std::filesystem::path index_path(image_prefix + ".index");

	if(!options.overwrite && (std::filesystem::exists(skeleton_path) || std::filesystem::exists(index_path)))
		throw_line("skeleton/index file already exists");

	std::vector<std::pair<uint32_t, uint32_t>> files;

	files.emplace_back(0, iso9660::SYSTEM_AREA_SIZE);
	
	std::unique_ptr<SectorReader> sector_reader;
	if(iso)
		sector_reader = std::make_unique<Image_ISO_Form1Reader>(image_path);
	else
		sector_reader = std::make_unique<Image_BIN_Form1Reader>(image_path);

	iso9660::PrimaryVolumeDescriptor pvd;
	if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, sector_reader.get(), iso9660::VolumeDescriptorType::PRIMARY))
		return;

	auto root_directory = iso9660::Browser::rootDirectory(sector_reader.get(), pvd);
	iso9660::Browser::iterate(root_directory, [&](const std::string &path, std::shared_ptr<iso9660::Entry> d)
	{
		auto fp((path.empty() ? "" : path + "/") + d->name());

		files.emplace_back(d->sectorsOffset(), d->sectorsOffset() + d->sectorsSize());

		return false;
	});

	std::fstream image_fs(image_path, std::fstream::in | std::fstream::binary);
	if(!image_fs.is_open())
		throw_line("unable to open file ({})", image_path);

	std::fstream skeleton_fs(skeleton_path, std::fstream::out | std::fstream::binary);
	if(!skeleton_fs.is_open())
		throw_line("unable to create file ({})", skeleton_path.filename().string());

	std::vector<uint8_t> data(iso ? FORM1_DATA_SIZE : CD_DATA_SIZE);
	uint32_t sectors_count = std::filesystem::file_size(image_path) / data.size();
	for(uint32_t i = 0; i < sectors_count; ++i)
	{
		image_fs.read((char *)data.data(), data.size());
		if(image_fs.fail())
			throw_line("read failed");

		if(inside_range(i, files) != nullptr)
		{
			if(iso)
				std::fill(data.begin(), data.end(), 0);
			else
			{
				auto sector = (Sector *)data.data();

				if(sector->header.mode == 1)
				{
					memset(sector->mode1.user_data, 0x00, FORM1_DATA_SIZE);
					memset(&sector->mode1.ecc, 0x00, sizeof(Sector::ECC));
					sector->mode1.edc = 0;
				}
				else if(sector->header.mode == 2)
				{
					if(sector->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
					{
						memset(sector->mode2.xa.form2.user_data, 0x00, FORM2_DATA_SIZE);
						sector->mode2.xa.form2.edc = 0;
					}
					else
					{
						memset(sector->mode2.xa.form1.user_data, 0x00, FORM1_DATA_SIZE);
						memset(&sector->mode2.xa.form1.ecc, 0x00, sizeof(Sector::ECC));
						sector->mode2.xa.form1.edc = 0;
					}
				}
			}
		}

		skeleton_fs.write((char *)data.data(), data.size());
		if(skeleton_fs.fail())
			throw_line("write failed");
	}
}


export void redumper_skeleton(Context &ctx, Options &options)
{
	if(options.image_name.empty())
		throw_line("image name is not provided");

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	if(std::filesystem::exists(image_prefix + ".cue"))
	{
		for(auto const &t : cue_get_entries(image_prefix + ".cue"))
		{
			// skip audio tracks
			if(!t.second)
				continue;

			auto track_prefix = (std::filesystem::path(options.image_path) / std::filesystem::path(t.first).stem()).string();

			skeleton(track_prefix, (std::filesystem::path(options.image_path) / t.first).string(), false, options);
		}
	}
	else if(std::filesystem::exists(image_prefix + ".iso"))
	{
		skeleton(image_prefix, image_prefix + ".iso", true, options);
	}
	else
		throw_line("image file not found");
}

}
