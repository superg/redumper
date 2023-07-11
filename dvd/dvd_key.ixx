module;
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "throw_line.hh"

export module dvd.key;

import cd.cdrom;
import dump;
import dvd.css;
import filesystem.iso9660;
import options;
import readers.form1_reader;
import readers.cd_form1_reader;
import readers.iso_form1_reader;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

std::string region_string(uint8_t region_bits)
{
	std::string region;

	for(uint32_t i = 0; i < CHAR_BIT; ++i)
		if(!(region_bits & 1 << i))
			region += std::to_string(i + 1) + " ";

	if(!region.empty())
		region.pop_back();

	return region;
}


std::map<std::string, iso9660::DirectoryRecord> dr_list(Form1Reader &form1_reader, const iso9660::DirectoryRecord &dr)
{
	std::map<std::string, iso9660::DirectoryRecord> entries;

	if(!(dr.file_flags & (uint8_t)iso9660::DirectoryRecord::FileFlags::DIRECTORY))
		return entries;

	uint32_t sectors_count = scale_up(dr.data_length.lsb, FORM1_DATA_SIZE);
	std::vector<uint8_t> buffer(sectors_count * FORM1_DATA_SIZE);
	if(!form1_reader.read(buffer.data(), dr.offset.lsb, sectors_count))
		throw_line("failed to read directory record");
	buffer.resize(dr.data_length.lsb);

	for(uint32_t i = 0, n = (uint32_t)buffer.size(); i < n;)
	{
		iso9660::DirectoryRecord &dr = *(iso9660::DirectoryRecord *)&buffer[i];

		if(dr.length && dr.length <= FORM1_DATA_SIZE - i % FORM1_DATA_SIZE)
		{
			char b1 = (char)buffer[i + sizeof(dr)];
			if(b1 != (char)iso9660::Characters::DIR_CURRENT && b1 != (char)iso9660::Characters::DIR_PARENT)
			{
				std::string identifier((const char *)&buffer[i + sizeof(dr)], dr.file_identifier_length);
				auto s = identifier.find((char)iso9660::Characters::SEPARATOR2);
				std::string name(s == std::string::npos ? identifier : identifier.substr(0, s));

				entries[name] = dr;
			}

			i += dr.length;
		}
		// skip sector boundary
		else
			i = ((i / FORM1_DATA_SIZE) + 1) * FORM1_DATA_SIZE;
	}

	return entries;
}


//TODO: reimplement properly after ImageBrowser rewrite
std::map<std::string, std::pair<uint32_t, uint32_t>> extract_vob_list(Form1Reader &form1_reader)
{
	std::map<std::string, std::pair<uint32_t, uint32_t>> titles;

	// read PVD root directory record lba
	iso9660::DirectoryRecord root_dr;
	bool pvd_found = false;
	for(uint32_t lba = iso9660::SYSTEM_AREA_SIZE; ; ++lba)
	{
		std::vector<uint8_t> sector(FORM1_DATA_SIZE);
		if(!form1_reader.read(sector.data(), lba, 1))
			throw_line("failed to read PVD");

		auto vd = (iso9660::VolumeDescriptor *)sector.data();
		if(memcmp(vd->standard_identifier, iso9660::STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)))
			break;

		if(vd->type == iso9660::VolumeDescriptor::Type::PRIMARY)
		{
			auto pvd = (iso9660::VolumeDescriptor *)vd;
			root_dr = pvd->primary.root_directory_record;
			pvd_found = true;
			break;
		}
		else if(vd->type == iso9660::VolumeDescriptor::Type::SET_TERMINATOR)
			break;
	}

	if(!pvd_found)
		throw_line("PVD not found");

	auto root_entries = dr_list(form1_reader, root_dr);
	auto it = root_entries.find("VIDEO_TS");
	if(it != root_entries.end())
	{
		auto video_entries = dr_list(form1_reader, it->second);
		for(auto const &e : video_entries)
			if(ends_with(e.first, ".VOB"))
				titles[e.first] = std::pair(e.second.offset.lsb, e.second.offset.lsb + scale_up(e.second.data_length.lsb, FORM1_DATA_SIZE));
	}

	return titles;
}


std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> create_vts_groups(const std::map<std::string, std::pair<uint32_t, uint32_t>> &vobs)
{
	std::vector<std::pair<uint32_t, uint32_t>> groups;

	for(auto const &v : vobs)
		groups.push_back(v.second);
	std::sort(groups.begin(), groups.end(), [](const std::pair<uint32_t, uint32_t> &v1, const std::pair<uint32_t, uint32_t> &v2)
			-> bool { return v1.first < v2.first; });
	for(bool merge = true; merge;)
	{
		merge = false;
		for(uint32_t i = 0; i + 1 < groups.size(); ++i)
		{
			if(groups[i].second == groups[i + 1].first)
			{
				groups[i].second = groups[i + 1].second;
				groups.erase(groups.begin() + i + 1);

				merge = true;
				break;
			}
		}
	}

	std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> vts;
	for(auto const &g : groups)
		vts[g] = std::vector<uint8_t>();

	return vts;
}


export void dvd_key(Context &ctx, const Options &options)
{
	// protection
	std::vector<uint8_t> copyright;
	auto status = cmd_read_dvd_structure(*ctx.sptd, copyright, 0, 0, READ_DVD_STRUCTURE_Format::COPYRIGHT, 0);
	if(!status.status_code)
	{
		strip_response_header(copyright);

		auto ci = (READ_DVD_STRUCTURE_CopyrightInformation *)copyright.data();
		auto cpst = (READ_DVD_STRUCTURE_CopyrightInformation_CPST)ci->copyright_protection_system_type;

		LOG("copyright: ");

		std::string protection("unknown");
		if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::NONE)
			protection = "<none>";
		else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
			protection = "CSS/CPPM";
		else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CPRM)
			protection = "CPRM";
		LOG("  protection system type: {}", protection);
		LOG("  region management information: {}", region_string(ci->region_management_information));

		if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
		{
			CDForm1Reader reader(*ctx.sptd, 0);
			auto vobs = extract_vob_list(reader);
			
			bool cppm = false;

			CSS css(*ctx.sptd);

			auto disc_key = css.getDiscKey(cppm);
			if(!disc_key.empty())
				LOG("  disc key: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}", disc_key[0], disc_key[1], disc_key[2], disc_key[3], disc_key[4]);

			if(!vobs.empty())
			{
				// determine continuous VTS groups
				auto vts = create_vts_groups(vobs);

				// attempt to get title keys from the disc
				for(auto &v : vts)
					v.second = css.getTitleKey(disc_key, v.first.first, cppm);

				// authenticate for reading
				css.getDiscKey(cppm);

				// crack remaining title keys (region lock)
				for(auto &v : vts)
					if(v.second.empty())
						v.second = CSS::crackTitleKey(v.first.first, v.first.second, reader);

				// assign keys from VTS groups to individual files
				std::map<std::string, std::vector<uint8_t>> title_keys;
				for(auto const &v : vobs)
				{
					for(auto const &vv : vts)
						if(v.second.first >= vv.first.first && v.second.second <= vv.first.second)
						{
							title_keys[v.first] = vv.second;
							break;
						}
				}

				LOG("  title keys:");
				for(auto const &t : title_keys)
				{
					std::string title_key;
					if(t.second.empty())
						title_key = "<error>";
					else if(is_zeroed(t.second.data(), t.second.size()))
						title_key = "<none>";
					else
						title_key = std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", t.second[0], t.second[1], t.second[2], t.second[3], t.second[4]);

					LOG("    {}: {}", t.first, title_key);
				}
			}
		}
		else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CPRM)
		{
			LOG("warning: CPRM protection is unsupported");
		}
	}
}


export void dvd_isokey(Context &ctx, const Options &options)
{
	if(options.image_name.empty())
		throw_line("image name is not provided");

	std::filesystem::path scm_path((std::filesystem::path(options.image_path) / options.image_name).string() + ".iso");

	ISOForm1Reader reader(scm_path);
	auto vobs = extract_vob_list(reader);
	if(!vobs.empty())
	{
		// determine continuous VTS groups
		auto vts = create_vts_groups(vobs);

		// crack title keys
		for(auto &v : vts)
			v.second = CSS::crackTitleKey(v.first.first, v.first.second, reader);

		// assign keys from VTS groups to individual files
		std::map<std::string, std::vector<uint8_t>> title_keys;
		for(auto const &v : vobs)
		{
			for(auto const &vv : vts)
				if(v.second.first >= vv.first.first && v.second.second <= vv.first.second)
				{
					title_keys[v.first] = vv.second;
					break;
				}
		}

		LOG("title keys:");
		for(auto const &t : title_keys)
		{
			std::string title_key;
			if(t.second.empty())
				title_key = "<error>";
			else if(is_zeroed(t.second.data(), t.second.size()))
				title_key = "<none>";
			else
				title_key = std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", t.second[0], t.second[1], t.second[2], t.second[3], t.second[4]);

			LOG("  {}: {}", t.first, title_key);
		}
	}
}

}
