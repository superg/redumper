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
        FILE_EXTENT,
        VOLUME_END_MARKER,
        SPACE_END_MARKER
    };

    uint32_t offset;
    Type type;
    uint32_t size;
    std::string name;
};


std::map<Area::Type, std::string> AREA_TYPE_STRING = {
    { Area::Type::SYSTEM_AREA,       "SYSTEM_AREA"       },
    { Area::Type::DESCRIPTORS,       "DESCRIPTORS"       },
    { Area::Type::PATH_TABLE_L,      "PATH_TABLE_L"      },
    { Area::Type::PATH_TABLE_L_OPT,  "PATH_TABLE_L_OPT"  },
    { Area::Type::PATH_TABLE_M,      "PATH_TABLE_M"      },
    { Area::Type::PATH_TABLE_M_OPT,  "PATH_TABLE_M_OPT"  },
    { Area::Type::DIRECTORY_EXTENT,  "DIRECTORY_EXTENT"  },
    { Area::Type::FILE_EXTENT,       "FILE_EXTENT"       },
    { Area::Type::VOLUME_END_MARKER, "VOLUME_END_MARKER" },
    { Area::Type::SPACE_END_MARKER,  "SPACE_END_MARKER"  }
};


std::vector<Area> area_map(SectorReader *sector_reader, uint32_t base_offset, uint32_t sectors_count)
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

        if(memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER, sizeof(descriptor.standard_identifier))
            && memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER_CDI, sizeof(descriptor.standard_identifier)))
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

    uint32_t sector_size = sector_reader->sectorSize();
    if(pvd.logical_block_size.lsb != sector_size)
        throw_line("unsupported logical block size (block size: {})", pvd.logical_block_size.lsb);

    std::map<uint32_t, Area> area_map;
    uint32_t o;

    // system area
    o = base_offset + 0;
    area_map.emplace(o, Area{ o, Area::Type::SYSTEM_AREA, SYSTEM_AREA_SIZE * sector_size, "" });

    // descriptors
    o = base_offset + SYSTEM_AREA_SIZE;
    area_map.emplace(o, Area{ o, Area::Type::DESCRIPTORS, descriptors_count * sector_size, "" });

    // L-type path tables
    uint32_t path_table_offset = pvd.path_table_l_offset - sector_reader->sectorsBase();
    o = base_offset + path_table_offset;
    area_map.emplace(o, Area{ o, Area::Type::PATH_TABLE_L, pvd.path_table_size.lsb, "" });
    if(pvd.path_table_l_offset_opt)
    {
        o = base_offset + pvd.path_table_l_offset_opt - sector_reader->sectorsBase();
        area_map.emplace(o, Area{ o, Area::Type::PATH_TABLE_L_OPT, pvd.path_table_size.lsb, "" });
    }

    // M-type path tables
    o = base_offset + endian_swap(pvd.path_table_m_offset) - sector_reader->sectorsBase();
    area_map.emplace(o, Area{ o, Area::Type::PATH_TABLE_M, pvd.path_table_size.lsb, "" });
    if(pvd.path_table_m_offset_opt)
    {
        o = base_offset + endian_swap(pvd.path_table_m_offset_opt) - sector_reader->sectorsBase();
        area_map.emplace(o, Area{ o, Area::Type::PATH_TABLE_M_OPT, pvd.path_table_size.lsb, "" });
    }

    // directories & files (path table)
    auto path_table_size = scale_up(pvd.path_table_size.lsb, sector_size);
    std::vector<uint8_t> path_table(sector_size * path_table_size);
    if(sector_reader->read(path_table.data(), path_table_offset, path_table_size) == path_table_size)
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

            auto pr_offset = pr.offset - sector_reader->sectorsBase();
            auto dr_extent_length = directory_extent_get_length(sector_reader, pr_offset);
            o = base_offset + pr_offset;
            area_map.emplace(o, Area{ o, Area::Type::DIRECTORY_EXTENT, dr_extent_length, name });

            auto dr_sectors_count = scale_up(dr_extent_length, sector_size);
            std::vector<uint8_t> directory_extent(dr_sectors_count * sector_size);
            if(sector_reader->read(directory_extent.data(), pr_offset, dr_sectors_count) == dr_sectors_count)
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

                    uint32_t dr_version;
                    std::string dr_name = split_identifier(dr_version, dr.first);

                    o = base_offset + dr.second.offset.lsb - sector_reader->sectorsBase();
                    area_map.emplace(o, Area{ o, Area::Type::FILE_EXTENT, dr.second.data_length.lsb, (name == "/" ? "" : name) + "/" + dr_name });
                }
            }
        }
    }

    // directories & files (root directory record traversal)
    // TODO: needed to overcome 16-bit path table parent limit
    ;

    // filesystem end marker
    // for multisession discs sometimes it's an absolute sector value
    uint32_t volume_end_offset = pvd.volume_space_size.lsb - (pvd.volume_space_size.lsb > sectors_count ? sector_reader->sectorsBase() : 0);
    o = base_offset + volume_end_offset;
    area_map.emplace(o, Area{ o, Area::Type::VOLUME_END_MARKER, 0, "" });

    // optional space after volume
    if(sectors_count > volume_end_offset)
        area_map.emplace(sectors_count, iso9660::Area{ sectors_count, iso9660::Area::Type::SPACE_END_MARKER, 0, "" });

    area_vector.reserve(area_map.size());
    for(auto const &a : area_map)
        area_vector.push_back(a.second);

    return area_vector;
}

}
