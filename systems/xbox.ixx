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
import cd.common;
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

export class SystemXBOX : public System
{
public:
    std::string getName() override
    {
        return "XBOX";
    }

    Type getType() override
    {
        return Type::ISO;
    }

    void printInfo(std::ostream &os, DataReader *data_reader, const std::filesystem::path &track_path, bool) const override
    {
        std::filesystem::path security_path = track_extract_basename(track_path.string()) + ".security";
        if(!std::filesystem::exists(security_path))
            return;

        auto security_sector = read_vector(security_path);
        if(security_sector.size() != FORM1_DATA_SIZE)
            return;

        auto const &sld = (xbox::SecurityLayerDescriptor &)security_sector[0];
        int32_t layer0_last = sign_extend<24>(endian_swap(sld.ld.layer0_end_sector));
        uint32_t xgd_type = xbox::xgd_version(layer0_last);
        if(xgd_type != 0)
            os << std::format("  system: {} (XGD{})", xgd_type == 1 ? "Xbox" : "Xbox 360", (char)(xgd_type + '0')) << std::endl;

        std::filesystem::path manufacturer_path = track_extract_basename(track_path.string()) + ".manufacturer";
        if(std::filesystem::exists(manufacturer_path))
        {
            auto manufacturer = read_vector(manufacturer_path);
            if(manufacturer.size() == FORM1_DATA_SIZE + 4)
            {
                auto const &dmi = (xbox::DMI &)manufacturer[4];
                if(dmi.version == 1)
                {
                    os << std::format("  serial: {:.2}-{:.3}", dmi.xgd1.xmid.publisher_id, dmi.xgd1.xmid.game_id) << std::endl;
                    os << std::format("  xmid: {:.8}", dmi.xgd1.xmid_string) << std::endl;
                }
                else if(dmi.version == 2)
                {
                    os << std::format("  serial: {:.2}-{:.4}", dmi.xgd23.xemid.publisher_id, dmi.xgd23.xemid.game_id) << std::endl;
                    os << std::format("  xemid: {:.16}", dmi.xgd23.xemid_string) << std::endl;

                    std::ostringstream ss;
                    ss << std::uppercase << std::hex << std::setfill('0');
                    for(uint32_t i = 12; i < 16; ++i)
                        ss << std::setw(2) << (uint32_t)dmi.xgd23.media_id[i];
                    os << std::format("  ringcode: {:.8}", ss.str()) << std::endl;
                }
            }
        }

        try
        {
            std::vector<Range<uint32_t>> protection;
            xbox::get_security_layer_descriptor_ranges(protection, security_sector);
            os << "  security sector ranges:" << std::endl;
            for(const auto &r : protection)
                os << std::format("    {}-{}", r.start, r.end - 1) << std::endl;
        }
        catch(const std::runtime_error &e)
        {
            // invalid SS ranges, don't print
            ;
        }
    }
};

}
