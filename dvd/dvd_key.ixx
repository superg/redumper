module;
#include <algorithm>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "throw_line.hh"

export module dvd.key;

import cd.cdrom;
import common;
import dvd.css;
import filesystem.iso9660;
import options;
import readers.disc_read_reader;
import readers.image_iso_reader;
import readers.data_reader;
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


std::map<std::string, std::pair<uint32_t, uint32_t>> extract_vob_list(DataReader *data_reader)
{
    std::map<std::string, std::pair<uint32_t, uint32_t>> titles;

    iso9660::PrimaryVolumeDescriptor pvd;
    if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, data_reader, iso9660::VolumeDescriptorType::PRIMARY))
        return titles;

    auto root_directory = iso9660::Browser::rootDirectory(data_reader, pvd);
    auto video_ts = root_directory->subEntry("VIDEO_TS");
    if(!video_ts)
        return titles;

    auto entries = video_ts->entries();
    for(auto e : entries)
    {
        if(e->isDirectory())
            continue;

        if(e->name().ends_with(".VOB"))
            titles[e->name()] = std::pair(e->sectorsLBA(), e->sectorsLBA() + e->sectorsSize());
    }

    return titles;
}


std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> create_vts_groups(const std::map<std::string, std::pair<uint32_t, uint32_t>> &vobs)
{
    std::vector<std::pair<uint32_t, uint32_t>> groups;

    for(auto const &v : vobs)
        groups.push_back(v.second);
    std::sort(groups.begin(), groups.end(), [](const std::pair<uint32_t, uint32_t> &v1, const std::pair<uint32_t, uint32_t> &v2) -> bool { return v1.first < v2.first; });
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


export int redumper_dvdkey(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::DVD)
        return exit_code;

    // protection
    std::vector<uint8_t> copyright;
    auto status = cmd_read_disc_structure(*ctx.sptd, copyright, 0, 0, 0, READ_DISC_STRUCTURE_Format::COPYRIGHT, 0);
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
            Disc_READ_Reader reader(*ctx.sptd, 0);
            auto vobs = extract_vob_list(&reader);

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

    return exit_code;
}


export int redumper_dvdisokey(Context &ctx, Options &options)
{
    int exit_code = 0;

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).generic_string();

    Image_ISO_Reader reader(image_prefix + ".iso");
    auto vobs = extract_vob_list(&reader);
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

    return exit_code;
}

}
