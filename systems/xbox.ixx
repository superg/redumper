module;
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include <span>
#include <vector>
#include "system.hh"
#include "throw_line.hh"

export module systems.xbox;

import cd.cdrom;
import dvd.xbox;
import filesystem.iso9660;
import range;
import readers.data_reader;
import utils.endian;
import utils.file_io;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemXbox : public System
{
public:
    std::string getName() override
    {
        return "Xbox";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &image_path, bool) const override
    {
        std::string basename = image_path.string();
        auto pos = basename.find_last_of('.');
        if(pos != std::string::npos)
            basename = std::string(basename, 0, pos);

        std::filesystem::path security_path = basename + ".security";
        if(!std::filesystem::exists(security_path))
            return;

        auto security_sector = read_vector(security_path);
        if(security_sector.empty() || security_sector.size() != FORM1_DATA_SIZE)
            return;

        auto const &sld = (xbox::SecurityLayerDescriptor &)security_sector[0];
        int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));
        uint32_t xgd_type = xbox::xgd_version(layer0_last);
        os << std::format("  system: {} (XGD{})", xgd_type == 1 ? "Xbox" : "Xbox 360", (char)(xgd_type + '0')) << std::endl;

        std::filesystem::path manufacturer_path = basename + ".manufacturer";
        if(std::filesystem::exists(manufacturer_path))
        {
            auto manufacturer = read_vector(manufacturer_path);
            if(!manufacturer.empty() && manufacturer.size() == FORM1_DATA_SIZE + 4)
            {
                auto const &dmi = (DMI &)manufacturer[4];
                if(dmi.version == 1)
                {
                    os << std::format("  serial: {}-{}", dmi.xgd1.xmid.publisher_id, dmi.xgd1.xmid.game_id) << std::endl;
                    os << std::format("  xmid: {}", dmi.xgd1.xmid_string) << std::endl;
                }
                else if(dmi.version == 2)
                {
                    os << std::format("  serial: {}-{}", dmi.xgd23.xemid.publisher_id, dmi.xgd23.xemid.game_id) << std::endl;
                    os << std::format("  xemid: {}", dmi.xgd23.xemid_string) << std::endl;

                    std::ostringstream ss;
                    ss << std::uppercase << std::hex << std::setfill('0');
                    for(uint32_t i = 12; i < 16; ++i)
                        ss << std::setw(2) << (uint32_t)dmi.xgd23.media_id[i];
                    os << std::format("  ringcode: {}", ss.str()) << std::endl;
                }
                else
                {
                    os << "  warning: unexpected DMI" << std::endl;
                }
            }
            else
            {
                os << "  warning: unexpected DMI" << std::endl;
            }
        }

        bool valid_ss = true;
        for(uint32_t i = 0; i < (uint32_t)sld.range_count; ++i)
        {
            if(xgd_type == 1 && i >= 16 || xgd_type != 1 && i != 0 && i != 3)
                continue;

            auto psn_start = sign_extend<24>(endian_swap_from_array<int32_t>(sld.ranges[i].psn_start));
            auto psn_end = sign_extend<24>(endian_swap_from_array<int32_t>(sld.ranges[i].psn_end));

            if(psn_start < 0 || psn_end - psn_start != 4095)
            {
                valid_ss = false;
                os << "  warning: unexpected security sector" << std::endl;
                break;
            }
        }

        if(valid_ss)
        {
            std::vector<Range<uint32_t>> protection;
            xbox::get_security_layer_descriptor_ranges(protection, security_sector);
            os << "  security sector ranges:" << std::endl;
            for(const auto &r : protection)
                os << std::format("    {}-{}", r.start, r.end - 1) << std::endl;
        }
    }

private:
    struct DMI
    {
        uint8_t version;

        union
        {
            struct
            {
                uint8_t reserved_001[7];
                union
                {
                    struct
                    {
                        char publisher_id[2];
                        char game_id[3];
                        char sku[2];
                        char region;
                    } xmid;
                    char xmid_string[8];
                };
                uint8_t timestamp[8];
                uint8_t xor_key_id;
                uint8_t reserved_019[55];
            } xgd1;

            struct
            {
                uint8_t reserved_001[15];
                uint8_t timestamp[8];
                uint8_t xor_key_id;
                uint8_t reserved_019[7];
                uint8_t media_id[16];
                uint8_t reserved_030[16];
                union
                {
                    struct
                    {
                        char publisher_id[2];
                        char game_id[4];
                        char sku[2];
                        char region;
                        union
                        {
                            struct
                            {
                                char version_id;
                                char media_id;
                                char disc_number;
                                char disc_total;
                                char reserved_04E[3];
                            } short_version;

                            struct
                            {
                                char version_id[2];
                                char media_id;
                                char disc_number;
                                char disc_total;
                                char reserved_04E[2];
                            } long_version;
                        };
                    } xemid;
                    char xemid_string[16];
                };
            } xgd23;
        };

        uint8_t reserved_050[1508];
        uint8_t dmi_trailer[460];
    };
};

}
