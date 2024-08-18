module;
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include "throw_line.hh"

export module dvd.dump;

import cd.cdrom;
import drive;
import dump;
import dvd.css;
import filesystem.iso9660;
import options;
import rom_entry;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.signal;
import utils.strings;
import utils.xbox;



namespace gpsxre
{

enum class LayerType : uint8_t
{
    EMBOSSED = 1 << 0,
    RECORDABLE = 1 << 1,
    REWRITABLE = 1 << 2,
    RESERVED = 1 << 3
};


// clang-format off
static const std::string BOOK_TYPE[] =
{
    "DVD-ROM",
    "DVD-RAM",
    "DVD-R",
    "DVD-RW",
    "HD DVD-ROM",
    "HD DVD-RAM",
    "HD DVD-R",
    "RESERVED1",
    "RESERVED2",
    "DVD+RW",
    "DVD+R",
    "RESERVED3",
    "RESERVED4",
    "DVD+RW DL",
    "DVD+R DL",
    "RESERVED5"
};


static const std::string MAXIMUM_RATE[] =
{
    "2.52 mbps",
    "5.04 mbps",
    "10.08 mbps",
    "20.16 mbps",
    "30.24 mpbs",
    "reserved1",
    "reserved2",
    "reserved3",
    "reserved4",
    "reserved5",
    "reserved6",
    "reserved7",
    "reserved8",
    "reserved9",
    "reserved10",
    "not specified"
};


static const std::string LINEAR_DENSITY[] =
{
    "0.267 um/bit",
    "0.293 um/bit",
    "0.409 to 0.435 um/bit",
    "reserved1",
    "0.280 to 0.291 um/bit",
    "0.153 um/bit",
    "0.130 to 0.140 um/bit",
    "reserved2",
    "0.353 um/bit",
    "reserved3",
    "reserved4",
    "reserved5",
    "reserved6",
    "reserved7",
    "reserved8",
    "reserved9"
};


static const std::string TRACK_DENSITY[] =
{
    "0.74 um/track",
    "0.80 um/track",
    "0.615 um/track",
    "0.40 um/track",
    "0.34 um/track",
    "reserved1",
    "reserved2",
    "reserved3",
    "reserved4",
    "reserved5",
    "reserved6",
    "reserved7",
    "reserved8",
    "reserved9",
    "reserved10",
    "reserved11"
};


static const std::string BLURAY_HYBRID_LAYER[] =
{
    "none",
    "-ROM",
    "-R",
    "-RW"
};


static const std::string BLURAY_CHANNEL_LENGTH[] =
{
    "none",
    "74.5nm",
    "69.0nm",
    "reserved1",
    "reserved2",
    "reserved3",
    "reserved4",
    "reserved5",
    "reserved6",
    "reserved7",
    "reserved8",
    "reserved9",
    "reserved10",
    "reserved11",
    "reserved12",
    "reserved13"
};
// clang-format on


uint32_t get_layer_length(const READ_DVD_STRUCTURE_LayerDescriptor &layer_descriptor)
{
    int32_t lba_first = sign_extend<24>(endian_swap(layer_descriptor.data_start_sector));
    int32_t lba_last = sign_extend<24>(endian_swap(layer_descriptor.data_end_sector));
    int32_t layer0_last = sign_extend<24>(endian_swap(layer_descriptor.layer0_end_sector));

    // for opposite layout the initial length is a difference between two layers (negative value)
    int32_t length = lba_last + 1 - lba_first;
    if(layer_descriptor.track_path)
        length += 2 * (layer0_last + 1);

    return length;
}


void print_physical_structure(const READ_DVD_STRUCTURE_LayerDescriptor &layer_descriptor, uint32_t layer)
{
    std::string types;
    if(layer_descriptor.layer_type & (uint8_t)LayerType::EMBOSSED)
        types += std::string(types.empty() ? "" : ", ") + "embossed";
    if(layer_descriptor.layer_type & (uint8_t)LayerType::RECORDABLE)
        types += std::string(types.empty() ? "" : ", ") + "recordable";
    if(layer_descriptor.layer_type & (uint8_t)LayerType::REWRITABLE)
        types += std::string(types.empty() ? "" : ", ") + "rewritable";
    if(layer_descriptor.layer_type & (uint8_t)LayerType::RESERVED)
        types += std::string(types.empty() ? "" : ", ") + "reserved";

    LOG("{}layer {} {{ {} }}", std::string(2, ' '), layer, types);

    std::string indent(4, ' ');

    uint32_t lba_first_raw = endian_swap(layer_descriptor.data_start_sector);
    uint32_t lba_last_raw = endian_swap(layer_descriptor.data_end_sector);
    uint32_t layer0_last_raw = endian_swap(layer_descriptor.layer0_end_sector);

    int32_t lba_first = sign_extend<24>(lba_first_raw);
    int32_t lba_last = sign_extend<24>(lba_last_raw);
    int32_t layer0_last = sign_extend<24>(layer0_last_raw);

    uint32_t length = get_layer_length(layer_descriptor);

    LOG("{}data {{ LBA: [{} .. {}], length: {}, hLBA: [0x{:06X} .. 0x{:06X}] }}", indent, lba_first, lba_last, length, lba_first_raw, lba_last_raw);
    if(layer0_last)
        LOG("{}data layer 0 last {{ LBA: {}, hLBA: 0x{:06X} }}", indent, layer0_last, layer0_last_raw);
    LOG("{}book type: {}", indent, BOOK_TYPE[layer_descriptor.book_type]);
    LOG("{}part version: {}", indent, layer_descriptor.part_version);
    if(layer_descriptor.disc_size < 2)
        LOG("{}disc size: {}", indent, layer_descriptor.disc_size ? "80mm" : "120mm");
    LOG("{}maximum rate: {}", indent, MAXIMUM_RATE[layer_descriptor.maximum_rate]);
    LOG("{}layers count: {}", indent, layer_descriptor.layers_number + 1);
    LOG("{}track path: {}", indent, layer_descriptor.track_path ? "opposite" : "parallel");
    LOG("{}linear density: {}", indent, LINEAR_DENSITY[layer_descriptor.linear_density]);
    LOG("{}track density: {}", indent, TRACK_DENSITY[layer_descriptor.track_density]);
    LOG("{}BCA: {}", indent, layer_descriptor.bca ? "yes" : "no");
}


void print_di_units_structure(const uint8_t *di_units, bool rom)
{
    std::string indent(4, ' ');

    uint32_t body_size = (rom ? 52 : 100);
    uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + body_size;

    for(uint32_t j = 0; j < 32; ++j)
    {
        auto unit = (READ_DISC_STRUCTURE_DiscInformationUnit *)&di_units[j * unit_size];
        std::string identifier((char *)unit->header.identifier, sizeof(unit->header.identifier));
        if(identifier != "DI")
            break;

        LOG("{}DI {{ format: {}, layer: {}, sequence number: {}, unit size: {}, continuation: {} }}", std::string(2, ' '), unit->header.format, (uint8_t)unit->header.layer,
            unit->header.sequence_number, (uint8_t)unit->header.unit_size, (uint8_t)unit->header.continuation);

        LOG("{}disc type: {}", indent, std::string((char *)unit->body_common.disc_type_identifier, sizeof(unit->body_common.disc_type_identifier)));
        if(unit->body_common.disc_size < 2)
            LOG("{}disc size: {}", indent, unit->body_common.disc_size ? "80mm" : "120mm");
        LOG("{}disc class: {}", indent, (uint8_t)unit->body_common.disc_class);
        LOG("{}disc version: {}", indent, (uint8_t)unit->body_common.disc_version);

        if(!rom)
        {
            auto trailer = (READ_DISC_STRUCTURE_DIUnitBodyTrailer *)&unit->body[body_size - sizeof(READ_DISC_STRUCTURE_DIUnitBodyTrailer)];
            LOG("{}disc manufacturer: {}", indent, std::string((char *)trailer->disc_manufacturer_id, sizeof(trailer->disc_manufacturer_id)));
            LOG("{}media type: {}", indent, std::string((char *)trailer->media_type_id, sizeof(trailer->media_type_id)));
            LOG("{}time stamp: {}", indent, endian_swap(trailer->time_stamp));
            LOG("{}product revision number: {}", indent, trailer->product_revision_number);
        }

        if(unit->header.format == 1)
        {
            auto body = (READ_DISC_STRUCTURE_DiscInformationBody1 *)unit->body;
            auto last_psn = endian_swap(body->last_psn);
            auto first_aun = endian_swap(body->first_aun);
            auto last_aun = endian_swap(body->last_aun);

            LOG("{}layers count: {}", indent, (uint8_t)body->layers_count);
            if(body->dvd_layer)
                LOG("{}DVD layer: {}", indent, BLURAY_HYBRID_LAYER[body->dvd_layer]);
            if(body->cd_layer)
                LOG("{}CD layer: {}", indent, BLURAY_HYBRID_LAYER[body->cd_layer]);
            if(body->channel_length)
                LOG("{}channel length: {}", indent, BLURAY_CHANNEL_LENGTH[body->channel_length]);
            if(body->polarity)
                LOG("{}polarity: {}", indent, body->polarity);
            if(body->recorded_polarity)
                LOG("{}recorded polarity: {}", indent, body->recorded_polarity);
            LOG("{}BCA: {}", indent, body->bca ? "yes" : "no");
            if(body->maximum_transfer)
                LOG("{}maximum transfer: {}Mbps", indent, body->maximum_transfer);

            const uint32_t lba_psn_shift = 0x100000;

            LOG("{}PSN last {{ PSN: {}, hPSN: 0x{:06X} }}", indent, last_psn, last_psn);
            uint32_t length = last_aun + 2 - first_aun;
            LOG("{}AUN {{ PSN: [{} .. {}], length: {}, hPSN: [0x{:06X} .. 0x{:06X}] }}", indent, first_aun, last_aun, length, first_aun, last_aun);
        }
    }
}


std::set<READ_DISC_STRUCTURE_Format> get_readable_formats(SPTD &sptd, bool bluray)
{
    std::set<READ_DISC_STRUCTURE_Format> readable_formats;

    std::vector<uint8_t> structure;
    cmd_read_disc_structure(sptd, structure, bluray ? 1 : 0, 0, 0, READ_DISC_STRUCTURE_Format::STRUCTURE_LIST, 0);
    strip_response_header(structure);

    auto structures_count = (uint16_t)(structure.size() / sizeof(READ_DVD_STRUCTURE_StructureListEntry));
    auto structures = (READ_DVD_STRUCTURE_StructureListEntry *)structure.data();

    for(uint16_t i = 0; i < structures_count; ++i)
        if(structures[i].rds)
            readable_formats.insert((READ_DISC_STRUCTURE_Format)structures[i].format_code);

    return readable_formats;
}


std::vector<std::vector<uint8_t>> read_physical_structures(SPTD &sptd, bool bluray, bool &rom)
{
    std::vector<std::vector<uint8_t>> structures;

    if(bluray)
    {
        std::vector<uint8_t> structure;
        auto status = cmd_read_disc_structure(sptd, structure, 1, 0, 0, READ_DISC_STRUCTURE_Format::PHYSICAL, 0);
        if(status.status_code)
            throw_line("failed to read blu-ray disc physical structure, SCSI ({})", SPTD::StatusMessage(status));

        structures.push_back(structure);

        uint8_t *di_units = &structure[sizeof(CMD_ParameterListHeader)];
        for(uint32_t j = 0; j < 32; ++j)
        {
            uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + (rom ? 52 : 100);

            auto unit = (READ_DISC_STRUCTURE_DiscInformationUnit *)&di_units[j * unit_size];
            std::string identifier((char *)unit->header.identifier, sizeof(unit->header.identifier));
            if(identifier != "DI")
                break;

            // some BD-R discs (not finalized?) are incorrectly identified as BD-ROM profile
            std::string disc_type_identifier((char *)unit->body_common.disc_type_identifier, sizeof(unit->body_common.disc_type_identifier));
            // BDO: BD-ROM, BDW: BD-RE, BDR: BD-R, BDU: UHD-BD, XG4: Xbox One XGD4
            if(rom && disc_type_identifier == "BDR")
                rom = false;
        }
    }
    else
    {
        for(uint32_t i = 0, layers_count = 0; !layers_count || i < layers_count; ++i)
        {
            std::vector<uint8_t> structure;
            auto status = cmd_read_disc_structure(sptd, structure, 0, 0, i, READ_DISC_STRUCTURE_Format::PHYSICAL, 0);
            if(status.status_code)
                throw_line("failed to read dvd disc physical structure, SCSI ({})", SPTD::StatusMessage(status));

            structures.push_back(structure);

            if(!layers_count)
            {
                auto layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor *)&structure[sizeof(CMD_ParameterListHeader)];
                layers_count = (layer_descriptor->track_path ? 0 : layer_descriptor->layers_number) + 1;

                // fallback
                if(!layers_count)
                    layers_count = 1;
            }
        }
    }

    return structures;
}


void progress_output(uint32_t sector, uint32_t sectors_count, uint32_t errors)
{
    char animation = sector == sectors_count ? '*' : spinner_animation();

    LOGC_RF("{} [{:3}%] sector: {}/{}, errors: {{ SCSI: {} }}", animation, (uint64_t)sector * 100 / sectors_count, extend_left(std::to_string(sector), ' ', digits_count(sectors_count)), sectors_count,
        errors);
}

export bool redumper_dump_dvd(Context &ctx, const Options &options, DumpMode dump_mode)
{
    image_check_empty(options);

    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    std::filesystem::path iso_path(image_prefix + ".iso");
    std::filesystem::path state_path(image_prefix + ".state");

    if(dump_mode == DumpMode::DUMP)
        image_check_overwrite(options);

    SPTD::Status status;

    // unlock drive if Kreon firmware detected so we can identify XGD later
    if(ctx.drive_config.vendor_specific.starts_with("KREON V1.00"))
    {
        status = cmd_kreon_set_lock_state(*ctx.sptd, KREON_LockState::WXRIPPER);
        if(status.status_code)
            LOG("warning: failed to unlock Kreon drive, SCSI ({})", SPTD::StatusMessage(status));
    }

    // get sectors count
    uint32_t sector_last, block_length;
    status = cmd_read_capacity(*ctx.sptd, sector_last, block_length, false, 0, false);
    if(status.status_code)
        throw_line("failed to read capacity, SCSI ({})", SPTD::StatusMessage(status));
    if(block_length != FORM1_DATA_SIZE)
        throw_line("unsupported block size (block size: {})", block_length);
    uint32_t sectors_count = sector_last + 1;

    auto readable_formats = get_readable_formats(*ctx.sptd, profile_is_bluray(ctx.current_profile));

    bool trim_to_filesystem_size = false;

    bool is_xbox = false;
    std::vector<std::array<uint32_t, 2>> xbox_skip_ranges;
    uint32_t xbox_lock_sector = 0;
    uint32_t xbox_l1_video_shift = 0;

    if(readable_formats.find(READ_DISC_STRUCTURE_Format::PHYSICAL) != readable_formats.end())
    {
        // function call changes rom flag if discrepancy is detected
        bool rom = ctx.current_profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM;
        auto physical_structures = read_physical_structures(*ctx.sptd, profile_is_bluray(ctx.current_profile), rom);
        if(ctx.current_profile == GET_CONFIGURATION_FeatureCode_ProfileList::BD_ROM && !rom)
        {
            trim_to_filesystem_size = true;
            LOG("warning: Blu-ray current profile mismatch, dump will be trimmed to disc filesystem size");
        }

        if(!profile_is_bluray(ctx.current_profile))
        {
            uint32_t physical_sectors_count = 0;
            for(uint32_t i = 0; i < physical_structures.size(); ++i)
            {
                auto const &structure = physical_structures[i];

                if(structure.size() < sizeof(CMD_ParameterListHeader) + sizeof(READ_DVD_STRUCTURE_LayerDescriptor))
                    throw_line("invalid layer descriptor size (layer: {})", i);

                auto layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)structure[sizeof(CMD_ParameterListHeader)];

                physical_sectors_count += get_layer_length(layer_descriptor);
            }

            if(physical_sectors_count != sectors_count)
            {
                // Kreon PFI sector count is only for Video portion when XGD present
                if(ctx.drive_config.vendor_specific.starts_with("KREON V1.00"))
                    is_xbox = true;
                else
                {
                    LOG("warning: READ_CAPACITY / PHYSICAL sectors count mismatch, using PHYSICAL");
                    sectors_count = physical_sectors_count;
                }
            }
        }

        if(is_xbox)
        {
            std::vector<uint8_t> security_sector(0x800);

            status = cmd_kreon_get_security_sector(*ctx.sptd, security_sector);
            if(status.status_code)
                throw_line("failed to get security sectors, SCSI ({})", SPTD::StatusMessage(status));

            // store security sector
            if(dump_mode == DumpMode::DUMP)
                write_vector(image_prefix + ".raw_ss", security_sector);

            // validate security sector
            XGD_Type xgd_type = get_xgd_type(security_sector);
            if(xgd_type == XGD_Type::UNKNOWN)
            {
                LOG("warning: READ_CAPACITY / PHYSICAL sectors count mismatch, using PHYSICAL");
                LOG("warning: Kreon Drive with malformed XGD detected, reverting to normal DVD mode");
                LOG("");
                is_xbox = false;
            }

            if(is_xbox && !physical_structures.empty())
            {
                LOG("Kreon Drive with XGD{} detected", (uint8_t)xgd_type);
                LOG("");

                std::vector<uint8_t> clean_ss = security_sector;
                clean_xbox_security_sector(clean_ss);

                // TODO: when 0800 support added check for `v2`
                auto security_sector_fn = std::format("{}.ss{}", image_prefix, xgd_type == XGD_Type::XGD1 ? "" : "v1");

                if(dump_mode == DumpMode::DUMP)
                {
                    // store cleaned security sector
                    write_vector(security_sector_fn, clean_ss);
                }
                else if(!options.force_refine)
                {
                    // if not dumping, compare security sector to stored to make sure it's the same disc
                    if(!std::filesystem::exists(security_sector_fn) || read_vector(security_sector_fn) != clean_ss)
                        throw_line("disc / file security sector doesn't match, refining from a different disc?");
                }

                auto &structure = physical_structures.front();

                if(structure.size() < sizeof(CMD_ParameterListHeader) + sizeof(READ_DVD_STRUCTURE_LayerDescriptor))
                    throw_line("invalid layer descriptor size (layer: 0)");

                auto &pfi_layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)structure[sizeof(CMD_ParameterListHeader)];

                int32_t lba_first = sign_extend<24>(endian_swap(pfi_layer_descriptor.data_start_sector));
                int32_t layer0_last = sign_extend<24>(endian_swap(pfi_layer_descriptor.layer0_end_sector));

                uint32_t l1_video_start = layer0_last + 1 - lba_first;
                uint32_t l1_video_length = get_layer_length(pfi_layer_descriptor) - l1_video_start;

                auto &ss_layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)security_sector[0];

                int32_t ss_lba_first = sign_extend<24>(endian_swap(ss_layer_descriptor.data_start_sector));
                int32_t ss_layer0_last = sign_extend<24>(endian_swap(ss_layer_descriptor.layer0_end_sector));

                uint32_t l1_padding_length = ss_lba_first - layer0_last - 1;
                if(xgd_type == XGD_Type::XGD3)
                    l1_padding_length += 4096;

                // extract security sector ranges
                bool is_xgd1 = (xgd_type == XGD_Type::XGD1);

                const auto media_specific_offset = offsetof(READ_DVD_STRUCTURE_LayerDescriptor, media_specific);
                uint8_t num_ss_regions = ss_layer_descriptor.media_specific[1632 - media_specific_offset];
                // partial pre-compute of conversion to Layer 1
                const uint32_t layer1_offset = (ss_layer0_last * 2) - 0x30000 + 1;

                for(int ss_pos = 1633 - media_specific_offset, i = 0; i < num_ss_regions; ss_pos += 9, i++)
                {
                    uint32_t start_psn = ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 3] << 16) | ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 4] << 8)
                                       | (uint32_t)ss_layer_descriptor.media_specific[ss_pos + 5];
                    uint32_t end_psn = ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 6] << 16) | ((uint32_t)ss_layer_descriptor.media_specific[ss_pos + 7] << 8)
                                     | (uint32_t)ss_layer_descriptor.media_specific[ss_pos + 8];
                    if((i < 8 && is_xgd1) || (i == 0 && !is_xgd1))
                    {
                        // Layer 0
                        xbox_skip_ranges.push_back({ start_psn - 0x30000, end_psn - 0x30000 });
                    }
                    else if((i < 16 && is_xgd1) || (i == 3 && !is_xgd1))
                    {
                        // Layer 1
                        xbox_skip_ranges.push_back({ layer1_offset - (start_psn ^ 0xFFFFFF), layer1_offset - (end_psn ^ 0xFFFFFF) });
                    }
                }

                // append L1 padding to ranges
                xbox_skip_ranges.push_back({ sectors_count, sectors_count + l1_padding_length - 1 });

                // sort the skip ranges
                std::sort(xbox_skip_ranges.begin(), xbox_skip_ranges.end(), [](const std::array<uint32_t, 2> &a, const std::array<uint32_t, 2> &b) { return a[0] < b[0]; });

                // add L1 padding to sectors count
                sectors_count += l1_padding_length;

                // must relock drive to read L1 video
                xbox_lock_sector = sectors_count;
                xbox_l1_video_shift = xbox_lock_sector - l1_video_start;

                // add L1 video to sectors count
                sectors_count += l1_video_length;

                // overwrite physical structure with true layer0_last from SS, so that disc structure logging is correct
                pfi_layer_descriptor.layer0_end_sector = ss_layer_descriptor.layer0_end_sector;
            }
        }

        if(dump_mode == DumpMode::DUMP)
        {
            std::vector<std::vector<uint8_t>> manufacturer_structures;
            if(readable_formats.find(READ_DISC_STRUCTURE_Format::MANUFACTURER) != readable_formats.end())
            {
                for(uint32_t i = 0; i < physical_structures.size(); ++i)
                {
                    std::vector<uint8_t> structure;
                    status = cmd_read_disc_structure(*ctx.sptd, structure, 0, 0, i, READ_DISC_STRUCTURE_Format::MANUFACTURER, 0);
                    if(status.status_code)
                        throw_line("failed to read disc manufacturer structure, SCSI ({})", SPTD::StatusMessage(status));

                    manufacturer_structures.push_back(structure);
                }
            }

            // store structure files
            for(uint32_t i = 0; i < physical_structures.size(); ++i)
                write_vector(std::format("{}{}.physical", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i) : ""), physical_structures[i]);
            for(uint32_t i = 0; i < manufacturer_structures.size(); ++i)
                write_vector(std::format("{}{}.manufacturer", image_prefix, manufacturer_structures.size() > 1 ? std::format(".{}", i) : ""), manufacturer_structures[i]);

            if(profile_is_bluray(ctx.current_profile))
            {
                uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + (rom ? 52 : 100);

                LOG("disc structure:");
                for(auto const &s : physical_structures)
                    print_di_units_structure(&s[sizeof(CMD_ParameterListHeader)], rom);
                LOG("");

                // layer break
                if(!physical_structures.empty())
                {
                    auto const &structure = physical_structures.front();

                    std::vector<uint32_t> layer_sizes(8);

                    auto di_units = &structure[sizeof(CMD_ParameterListHeader)];
                    for(uint32_t j = 0; j < 32; ++j)
                    {
                        auto &unit = (READ_DISC_STRUCTURE_DiscInformationUnit &)di_units[j * unit_size];
                        std::string identifier((char *)unit.header.identifier, sizeof(unit.header.identifier));
                        if(identifier != "DI")
                            break;

                        if(unit.header.format == 1)
                        {
                            auto body = (READ_DISC_STRUCTURE_DiscInformationBody1 *)unit.body;
                            auto last_psn = endian_swap(body->last_psn);
                            auto first_aun = endian_swap(body->first_aun);
                            auto last_aun = endian_swap(body->last_aun);

                            layer_sizes[unit.header.layer] = last_aun - first_aun + 2;
                        }
                    }

                    std::vector<uint32_t> layer_breaks;
                    uint32_t layer_break = 0;
                    for(uint32_t i = 0; i < layer_sizes.size() && layer_sizes[i]; ++i)
                    {
                        if(layer_break)
                            layer_breaks.push_back(layer_break);

                        layer_break += layer_sizes[i];
                    }

                    if(!layer_breaks.empty())
                    {
                        for(uint32_t i = 0; i < layer_breaks.size(); ++i)
                            LOG("layer break{}: {}", layer_breaks.size() > 1 ? std::format(" (layer: {})", i) : "", layer_breaks[i]);

                        LOG("");
                    }
                }
            }
            else
            {
                LOG("disc structure:");
                for(uint32_t i = 0; i < physical_structures.size(); ++i)
                {
                    auto const &structure = physical_structures[i];
                    print_physical_structure((READ_DVD_STRUCTURE_LayerDescriptor &)structure[sizeof(CMD_ParameterListHeader)], i);
                }
                LOG("");

                // layer break
                if(!physical_structures.empty())
                {
                    auto const &structure = physical_structures.front();

                    if(structure.size() < sizeof(CMD_ParameterListHeader) + sizeof(READ_DVD_STRUCTURE_LayerDescriptor))
                        throw_line("invalid layer descriptor size (layer: {})", 0);

                    auto layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)structure[sizeof(CMD_ParameterListHeader)];

                    uint32_t layer_break = 0;

                    // opposite
                    if(layer_descriptor.track_path)
                    {
                        int32_t lba_first = sign_extend<24>(endian_swap(layer_descriptor.data_start_sector));
                        int32_t layer0_last = sign_extend<24>(endian_swap(layer_descriptor.layer0_end_sector));

                        layer_break = layer0_last + 1 - lba_first;
                    }
                    // parallel
                    else if(physical_structures.size() > 1)
                        layer_break = get_layer_length(layer_descriptor);

                    if(layer_break)
                    {
                        LOG("layer break: {}", layer_break);
                        LOG("");
                    }
                }
            }
        }
        // compare physical structures to stored to make sure it's the same disc
        else if(!options.force_refine)
        {
            for(uint32_t i = 0; i < physical_structures.size(); ++i)
            {
                auto const &structure = physical_structures[i];

                auto structure_fn = std::format("{}{}.physical", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i) : "");

                if(!std::filesystem::exists(structure_fn) || read_vector(structure_fn) != structure)
                    throw_line("disc / file physical structure doesn't match, refining from a different disc?");
            }
        }
    }

    // authenticate CSS
    if(dump_mode == DumpMode::DUMP && readable_formats.find(READ_DISC_STRUCTURE_Format::COPYRIGHT) != readable_formats.end())
    {
        std::vector<uint8_t> copyright;
        status = cmd_read_disc_structure(*ctx.sptd, copyright, 0, 0, 0, READ_DISC_STRUCTURE_Format::COPYRIGHT, 0);
        if(!status.status_code)
        {
            strip_response_header(copyright);

            auto ci = (READ_DVD_STRUCTURE_CopyrightInformation *)copyright.data();
            auto cpst = (READ_DVD_STRUCTURE_CopyrightInformation_CPST)ci->copyright_protection_system_type;

            // TODO: distinguish CPPM
            bool cppm = false;

            if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
            {
                CSS css(*ctx.sptd);

                // authenticate for reading
                css.getDiscKey(cppm);

                LOG("protection: CSS/CPPM");
                LOG("");
            }
        }
    }

    const uint32_t sectors_at_once = (dump_mode == DumpMode::REFINE ? 1 : options.dump_read_size);

    std::vector<uint8_t> file_data(sectors_at_once * FORM1_DATA_SIZE);
    std::vector<State> file_state(sectors_at_once);

    std::fstream fs_iso(iso_path, std::fstream::out | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in) | std::fstream::binary);
    std::fstream fs_state(state_path, std::fstream::out | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in) | std::fstream::binary);

    uint32_t refine_counter = 0;
    uint32_t refine_retries = options.retries ? options.retries : 1;

    uint32_t errors_scsi = 0;
    // FIXME: verify memory usage for largest bluray and chunk it if needed
    if(dump_mode != DumpMode::DUMP)
    {
        std::vector<State> state_buffer(sectors_count);
        read_entry(fs_state, (uint8_t *)state_buffer.data(), sizeof(State), 0, sectors_count, 0, (uint8_t)State::ERROR_SKIP);
        errors_scsi = std::count(state_buffer.begin(), state_buffer.end(), State::ERROR_SKIP);
    }

    ROMEntry rom_entry(iso_path.filename().string());

    SignalINT signal;

    uint8_t skip_range_idx = 0;
    bool kreon_locked = false;
    for(uint32_t s = 0; s < sectors_count;)
    {
        bool increment = true;

        uint32_t sectors_to_read = std::min(sectors_at_once, sectors_count - s);

        if(is_xbox && !kreon_locked)
        {
            // skip xbox security sector ranges and L1 filler range
            if(skip_range_idx < xbox_skip_ranges.size())
            {
                if(xbox_skip_ranges[skip_range_idx][0] <= s && s <= xbox_skip_ranges[skip_range_idx][1] + 1)
                {
                    if(s == xbox_skip_ranges[skip_range_idx][1] + 1)
                    {
                        if(options.verbose)
                            LOG_R("skipped sectors: {}-{}", xbox_skip_ranges[skip_range_idx][0], xbox_skip_ranges[skip_range_idx][1]);

                        skip_range_idx++;
                        // skip any overlapping ranges we have already completed
                        while(skip_range_idx < xbox_skip_ranges.size() && s >= xbox_skip_ranges[skip_range_idx][1] + 1)
                            skip_range_idx++;

                        // if still in a security sector range do not allow later read to happen
                        if(skip_range_idx < xbox_skip_ranges.size() && xbox_skip_ranges[skip_range_idx][0] <= s)
                            continue;
                    }
                    else
                    {
                        // skip at most to the end of the security sector range
                        sectors_to_read = std::min(sectors_to_read, xbox_skip_ranges[skip_range_idx][1] + 1 - s);
                        progress_output(s, sectors_count, errors_scsi);

                        std::vector<uint8_t> zeroes(sectors_to_read * FORM1_DATA_SIZE);
                        write_entry(fs_iso, zeroes.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0);
                        std::fill(file_state.begin(), file_state.end(), State::SUCCESS);
                        write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);

                        rom_entry.update(zeroes.data(), sectors_to_read * FORM1_DATA_SIZE);

                        s += sectors_to_read;
                        continue;
                    }
                }
                else
                {
                    sectors_to_read = std::min(sectors_to_read, xbox_skip_ranges[skip_range_idx][0] - s);
                }
            }

            // check if Kreon drive needs locking
            if(s < xbox_lock_sector && s + sectors_to_read >= xbox_lock_sector)
                sectors_to_read = std::min(sectors_to_read, xbox_lock_sector - s);
            else if(s == xbox_lock_sector)
            {
                status = cmd_kreon_set_lock_state(*ctx.sptd, KREON_LockState::LOCKED);
                if(status.status_code)
                    throw_line("failed to set lock state, SCSI ({})", SPTD::StatusMessage(status));
                if(options.verbose)
                    LOG_R("locked kreon drive at sector: {}", s);
                kreon_locked = true;
            }
        }

        bool read = false;
        if(dump_mode == DumpMode::DUMP)
        {
            read = true;
        }
        else if(dump_mode == DumpMode::REFINE)
        {
            read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);
            read = std::count(file_state.begin(), file_state.end(), State::ERROR_SKIP);
        }
        else if(dump_mode == DumpMode::VERIFY)
        {
            read_entry(fs_iso, (uint8_t *)file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0, 0);

            read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);
            read = true;
        }

        if(read)
        {
            progress_output(s, sectors_count, errors_scsi);

            std::vector<uint8_t> drive_data(sectors_at_once * FORM1_DATA_SIZE);

            uint32_t dump_sector = s;
            if(kreon_locked)
                dump_sector -= xbox_l1_video_shift;

            status = cmd_read(*ctx.sptd, drive_data.data(), FORM1_DATA_SIZE, dump_sector, sectors_to_read, dump_mode == DumpMode::REFINE && refine_counter);

            if(status.status_code)
            {
                if(options.verbose)
                {
                    std::string status_retries;
                    if(dump_mode == DumpMode::REFINE)
                        status_retries = std::format(", retry: {}", refine_counter + 1);
                    for(uint32_t i = 0; i < sectors_to_read; ++i)
                        LOG_R("[sector: {}] SCSI error ({}{})", s + i, SPTD::StatusMessage(status), status_retries);
                }

                if(dump_mode == DumpMode::DUMP)
                    errors_scsi += sectors_to_read;
                else if(dump_mode == DumpMode::REFINE)
                {
                    ++refine_counter;
                    if(refine_counter < refine_retries)
                        increment = false;
                    else
                    {
                        if(options.verbose)
                            for(uint32_t i = 0; i < sectors_to_read; ++i)
                                LOG_R("[sector: {}] correction failure", s + i);

                        refine_counter = 0;
                    }
                }
            }
            else
            {
                if(dump_mode == DumpMode::DUMP)
                {
                    file_data.swap(drive_data);

                    write_entry(fs_iso, file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0);
                    std::fill(file_state.begin(), file_state.end(), State::SUCCESS);
                    write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);
                }
                else if(dump_mode == DumpMode::REFINE)
                {
                    for(uint32_t i = 0; i < sectors_to_read; ++i)
                    {
                        if(file_state[i] == State::SUCCESS)
                            continue;

                        if(options.verbose)
                            LOG_R("[sector: {}] correction success", s + i);

                        std::copy(drive_data.begin() + i * FORM1_DATA_SIZE, drive_data.begin() + (i + 1) * FORM1_DATA_SIZE, file_data.begin() + i * FORM1_DATA_SIZE);
                        file_state[i] = State::SUCCESS;

                        --errors_scsi;
                    }

                    refine_counter = 0;

                    write_entry(fs_iso, file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0);
                    write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);
                }
                else if(dump_mode == DumpMode::VERIFY)
                {
                    bool update = false;

                    for(uint32_t i = 0; i < sectors_to_read; ++i)
                    {
                        if(file_state[i] != State::SUCCESS)
                            continue;

                        if(!std::equal(file_data.begin() + i * FORM1_DATA_SIZE, file_data.begin() + (i + 1) * FORM1_DATA_SIZE, drive_data.begin() + i * FORM1_DATA_SIZE))
                        {
                            if(options.verbose)
                                LOG_R("[sector: {}] data mismatch, sector state updated", s + i);

                            file_state[i] = State::ERROR_SKIP;
                            update = true;

                            ++errors_scsi;
                        }
                    }

                    if(update)
                        write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0);
                }
            }
        }

        if(dump_mode == DumpMode::DUMP && !errors_scsi)
            rom_entry.update(file_data.data(), sectors_to_read * FORM1_DATA_SIZE);

        if(signal.interrupt())
        {
            LOG_R("[sector: {:6}] forced stop ", s);
            break;
        }

        if(increment)
            s += sectors_to_read;
    }

    if(is_xbox)
    {
        // re-unlock drive before returning
        status = cmd_kreon_set_lock_state(*ctx.sptd, KREON_LockState::WXRIPPER);
        if(status.status_code)
            LOG("warning: failed to unlock drive at end of dump, SCSI ({})", SPTD::StatusMessage(status));
    }

    if(!signal.interrupt())
    {
        progress_output(sectors_count, sectors_count, errors_scsi);
        LOG("");
    }
    LOG("");

    LOG("media errors: ");
    LOG("  SCSI: {}", errors_scsi);

    if(signal.interrupt())
        signal.raiseDefault();

    if(dump_mode == DumpMode::DUMP && !errors_scsi)
        ctx.dat = std::vector<std::string>(1, rom_entry.xmlLine());

    return errors_scsi;
}

}
