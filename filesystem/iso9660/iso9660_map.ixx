module;
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660:map;

import :defs;

import readers.data_reader;
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

    int32_t lba;
    Type type;
    uint32_t size;
    std::string name;
};


const std::map<Area::Type, std::string> AREA_TYPE_STRING = {
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


uint32_t directory_extent_get_length(DataReader *data_reader, int32_t lba)
{
    std::vector<uint8_t> sector(data_reader->sectorSize());
    if(data_reader->read(sector.data(), lba, 1) == 1)
    {
        auto dr = (DirectoryRecord &)sector[0];
        return dr.data_length.lsb;
    }

    return 0;
}


std::vector<Area> area_map(DataReader *data_reader, uint32_t sectors_count)
{
    std::vector<Area> area_vector;

    PrimaryVolumeDescriptor pvd;
    bool pvd_found = false;

    uint32_t descriptors_count = 0;
    for(;;)
    {
        VolumeDescriptor descriptor;
        if(data_reader->read((uint8_t *)&descriptor, data_reader->sectorsBase() + SYSTEM_AREA_SIZE + descriptors_count, 1) != 1)
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

    std::map<int32_t, Area> area_map;

    int32_t lba_end = data_reader->sectorsBase() + sectors_count;
    uint32_t sector_size = data_reader->sectorSize();
    if(pvd.logical_block_size.lsb && pvd.logical_block_size.lsb != sector_size)
        throw_line("unsupported logical block size (block size: {})", pvd.logical_block_size.lsb);

    int32_t lba;

    // system area
    lba = data_reader->sectorsBase() + 0;
    area_map.emplace(lba, Area{ lba, Area::Type::SYSTEM_AREA, SYSTEM_AREA_SIZE * sector_size, "" });

    // descriptors
    lba = data_reader->sectorsBase() + SYSTEM_AREA_SIZE;
    area_map.emplace(lba, Area{ lba, Area::Type::DESCRIPTORS, descriptors_count * sector_size, "" });

    auto path_table_sectors_count = scale_up(pvd.path_table_size.lsb, sector_size);

    // L-type path tables
    lba = pvd.path_table_l_offset;
    area_map.emplace(lba, Area{ lba, Area::Type::PATH_TABLE_L, pvd.path_table_size.lsb, "" });
    if(pvd.path_table_l_offset_opt)
    {
        lba = pvd.path_table_l_offset_opt;
        area_map.emplace(lba, Area{ lba, Area::Type::PATH_TABLE_L_OPT, pvd.path_table_size.lsb, "" });
    }

    // M-type path tables
    lba = endian_swap(pvd.path_table_m_offset);
    area_map.emplace(lba, Area{ lba, Area::Type::PATH_TABLE_M, pvd.path_table_size.lsb, "" });
    if(pvd.path_table_m_offset_opt)
    {
        lba = endian_swap(pvd.path_table_m_offset_opt);
        area_map.emplace(lba, Area{ lba, Area::Type::PATH_TABLE_M_OPT, pvd.path_table_size.lsb, "" });
    }

    // directories & files (path table)
    std::vector<uint8_t> path_table(path_table_sectors_count * sector_size);
    if(data_reader->read(path_table.data(), pvd.path_table_l_offset, path_table_sectors_count) == path_table_sectors_count)
    {
        path_table.resize(pvd.path_table_size.lsb);

        // some discs are mastered with path table size equal to logical block size and padded with zeroes
        auto zeroed_start = (uint32_t)(std::find_if(path_table.rbegin(), path_table.rend(), [](uint8_t i) { return i; }).base() - path_table.begin());

        std::vector<std::string> names;
        for(uint32_t i = 0; i < zeroed_start;)
        {
            auto pr = (PathRecord &)path_table[i];
            i += sizeof(PathRecord);

            std::string identifier((const char *)&path_table[i], pr.length);
            if(identifier == std::string(1, (char)iso9660::Characters::DIR_CURRENT))
                identifier.clear();
            std::string name((pr.parent_directory_number == 1 ? "" : names[pr.parent_directory_number - 1]) + "/" + identifier);
            names.push_back(name);

            i += round_up(pr.length, (uint8_t)2) + pr.xa_length;

            auto dr_extent_length = directory_extent_get_length(data_reader, pr.offset);
            auto dr_extent_sectors_count = scale_up(dr_extent_length, sector_size);

            lba = pr.offset;
            area_map.emplace(lba, Area{ lba, Area::Type::DIRECTORY_EXTENT, dr_extent_length, name });

            std::vector<uint8_t> directory_extent(dr_extent_sectors_count * sector_size);
            if(data_reader->read(directory_extent.data(), pr.offset, dr_extent_sectors_count) == dr_extent_sectors_count)
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

                    // skip dummy files
                    if(dr.second.offset.lsb < data_reader->sectorsBase() || dr.second.offset.lsb >= lba_end)
                        continue;

                    uint32_t dr_version;
                    std::string dr_name = split_identifier(dr_version, dr.first);

                    auto dr_data_sectors_count = scale_up(dr.second.data_length.lsb, sector_size);

                    lba = dr.second.offset.lsb;
                    auto area = Area{ lba, Area::Type::FILE_EXTENT, dr.second.data_length.lsb, (name == "/" ? "" : name) + "/" + dr_name };
                    if(auto it = area_map.find(lba); it == area_map.end())
                        area_map.emplace(lba, area);
                    else if(area.size > it->second.size)
                        it->second = area;
                }
            }
        }
    }

    // directories & files (root directory record traversal)
    // TODO: needed to overcome 16-bit path table parent limit
    ;

    // filesystem end marker
    // for multisession discs sometimes it's an absolute sector value
    lba = (pvd.volume_space_size.lsb > sectors_count ? 0 : data_reader->sectorsBase()) + pvd.volume_space_size.lsb;
    area_map.emplace(lba, Area{ lba, Area::Type::VOLUME_END_MARKER, 0, "" });

    // optional space after volume end
    if(lba_end > lba)
    {
        lba = lba_end;
        area_map.emplace(lba, iso9660::Area{ lba, iso9660::Area::Type::SPACE_END_MARKER, 0, "" });
    }

    area_vector.reserve(area_map.size());
    for(auto const &a : area_map)
        area_vector.push_back(a.second);

    return area_vector;
}


// this helper function is needed to avoid unresolved externals when linking with libstdc++
// if enum_to_string() is used outside of this module with iso9660::AREA_TYPE_STRING map,
// probably related to modules support and might be fixed eventually
std::string area_type_to_string(iso9660::Area::Type value)
{
    return enum_to_string(value, AREA_TYPE_STRING);
}

}
