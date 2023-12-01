module;
#include <cstdint>
#include <map>
#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660:map;

import :defs;

import readers.sector_reader;
import utils.endian;
import utils.logger;
import utils.misc;



export namespace gpsxre::iso9660
{

struct Area
{
	enum class Type
	{
		SYSTEM_AREA,
		DESCRIPTORS,
		PATH_TABLE_L,
		PATH_TABLE_L_OPT,
		PATH_TABLE_M,
		PATH_TABLE_M_OPT,
		DIRECTORY_EXTENT,
		FILE_EXTENT
	};

	uint32_t offset;
	Type type;
	uint32_t size;
	std::string name;
};


std::map<Area::Type, std::string> AREA_TYPE_STRING =
{
	{Area::Type::SYSTEM_AREA, "SYSTEM_AREA"},
	{Area::Type::DESCRIPTORS, "DESCRIPTORS"},
	{Area::Type::PATH_TABLE_L, "PATH_TABLE_L"},
	{Area::Type::PATH_TABLE_L_OPT, "PATH_TABLE_L_OPT"},
	{Area::Type::PATH_TABLE_M, "PATH_TABLE_M"},
	{Area::Type::PATH_TABLE_M_OPT, "PATH_TABLE_M_OPT"},
	{Area::Type::DIRECTORY_EXTENT, "DIRECTORY_EXTENT"},
	{Area::Type::FILE_EXTENT, "FILE_EXTENT"}
};


export std::vector<Area> area_map(SectorReader *sector_reader)
{
	std::vector<Area> area_vector;

	PrimaryVolumeDescriptor pvd;
	bool pvd_found = false;

	uint32_t descriptors_count = 0;
	for(;;)
	{
		VolumeDescriptor descriptor;
		if(sector_reader->read((uint8_t *)&descriptor, SYSTEM_AREA_SIZE + descriptors_count, 1) != 1)
			break;

		if(memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER, sizeof(descriptor.standard_identifier)) &&
		   memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER_CDI, sizeof(descriptor.standard_identifier)))
			break;

		++descriptors_count;

		if(descriptor.type == VolumeDescriptorType::PRIMARY)
		{
			pvd = *(PrimaryVolumeDescriptor *)&descriptor;
			pvd_found = true;
		}
		else if(descriptor.type == VolumeDescriptorType::SET_TERMINATOR)
			break;
	}

	if(!pvd_found)
		return area_vector;

	if(pvd.logical_block_size.lsb != sector_reader->sectorSize())
		throw_line("unsupported logical block size (block size: {})", pvd.logical_block_size.lsb);

	std::map<uint32_t, Area> area_map;

	// system area
	area_map.emplace(0, Area{ 0, Area::Type::SYSTEM_AREA, SYSTEM_AREA_SIZE * sector_reader->sectorSize(), "" });

	// descriptors
	area_map.emplace(SYSTEM_AREA_SIZE, Area{ SYSTEM_AREA_SIZE, Area::Type::DESCRIPTORS, descriptors_count * sector_reader->sectorSize(), "" });

	// L-type path tables
	auto path_table_size = scale_up(pvd.path_table_size.lsb, sector_reader->sectorSize());
	area_map.emplace(pvd.path_table_l_offset, Area{ pvd.path_table_l_offset, Area::Type::PATH_TABLE_L, pvd.path_table_size.lsb, "" });
	area_map.emplace(pvd.path_table_l_offset_opt, Area{ pvd.path_table_l_offset_opt, Area::Type::PATH_TABLE_L_OPT, pvd.path_table_size.lsb, "" });

	// M-type path tables
	auto path_table_m_offset = endian_swap(pvd.path_table_m_offset);
	auto path_table_m_offset_opt = endian_swap(pvd.path_table_m_offset_opt);
	area_map.emplace(path_table_m_offset, Area{ path_table_m_offset, Area::Type::PATH_TABLE_M, pvd.path_table_size.lsb, "" });
	area_map.emplace(path_table_m_offset_opt, Area{ path_table_m_offset_opt, Area::Type::PATH_TABLE_M_OPT, pvd.path_table_size.lsb, "" });

	// directories & files (path table)
	std::vector<uint8_t> path_table(sector_reader->sectorSize() * path_table_size);
	if(sector_reader->read(path_table.data(), pvd.path_table_l_offset, path_table_size) == path_table_size)
	{
		path_table.resize(pvd.path_table_size.lsb);

		std::vector<std::string> names;
		for(uint32_t i = 0; i < path_table.size();)
		{
			auto pr = (PathRecord &)path_table[i];
			i += sizeof(PathRecord);

			std::string identifier((const char *)&path_table[i], pr.length);
			if(identifier == std::string(1, (char)iso9660::Characters::DIR_CURRENT))
				identifier.clear();
			std::string name((pr.parent_directory_number == 1 ? "" : names[pr.parent_directory_number - 1]) + "/" + identifier);
			names.push_back(name);

			i += round_up(pr.length, (uint8_t)2) + pr.xa_length;

			auto dr_extent_length = directory_extent_get_length(sector_reader, pr.offset);
			area_map.emplace(pr.offset, Area{ pr.offset, Area::Type::DIRECTORY_EXTENT, dr_extent_length, name });

			auto dr_sectors_count = scale_up(dr_extent_length, sector_reader->sectorSize());
			std::vector<uint8_t> directory_extent(dr_sectors_count * sector_reader->sectorSize());
			if(sector_reader->read(directory_extent.data(), pr.offset, dr_sectors_count) == dr_sectors_count)
			{
				auto directory_records = directory_extent_get_records(directory_extent);
				for(auto const &dr : directory_records)
				{
					// skip current and parent records
					if(dr.first == std::string(1, (char)iso9660::Characters::DIR_CURRENT) || dr.first == std::string(1, (char)iso9660::Characters::DIR_PARENT))
						continue;

					// skip directories
					if(dr.second.file_flags & (uint8_t)iso9660::DirectoryRecord::FileFlags::DIRECTORY)
						continue;

					uint32_t offset = dr.second.offset.lsb - sector_reader->sectorsBase();
					area_map.emplace(offset, Area{ offset, Area::Type::FILE_EXTENT, dr.second.data_length.lsb, (name == "/" ? "" : name) + "/" + dr.first });
				}
			}
		}
	}

	// directories & files (root directory record traversal)
	//TODO: needed to overcome 16-bit path table parent limit
	;

	area_vector.reserve(area_map.size());
	for(auto const &a : area_map)
		area_vector.push_back(a.second);

	return area_vector;
}

}
