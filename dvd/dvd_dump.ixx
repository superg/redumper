module;
#include <algorithm>
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

    // get sectors count
    uint32_t sector_last, block_length;
    auto status = cmd_read_capacity(*ctx.sptd, sector_last, block_length, false, 0, false);
    if(status.status_code)
        throw_line("failed to read capacity, SCSI ({})", SPTD::StatusMessage(status));
    if(block_length != FORM1_DATA_SIZE)
        throw_line("unsupported block size (block size: {})", block_length);
    uint32_t sectors_count = sector_last + 1;

    auto readable_formats = get_readable_formats(*ctx.sptd, profile_is_bluray(ctx.current_profile));

    bool trim_to_filesystem_size = false;
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

            // Kreon drives return incorrect sectors count
            if(physical_sectors_count != sectors_count)
            {
                LOG("warning: READ_CAPACITY / PHYSICAL sectors count mismatch, using PHYSICAL");
                sectors_count = physical_sectors_count;
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
                    cmd_read_disc_structure(*ctx.sptd, structure, 0, 0, i, READ_DISC_STRUCTURE_Format::MANUFACTURER, 0);
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
        auto status = cmd_read_disc_structure(*ctx.sptd, copyright, 0, 0, 0, READ_DISC_STRUCTURE_Format::COPYRIGHT, 0);
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

    // Get XBOX Security sectors (Currently works with Kreon 1.00 only)
    if (dump_mode == DumpMode::DUMP && ctx.drive_config.vendor_specific.starts_with("KREON V1.00")) { // && "is an Xbox Disc"
        std::filesystem::path ss_path(image_prefix + ".raw_ss");
        std::fstream fs_ss(ss_path, std::fstream::out | std::fstream::trunc | std::fstream::binary);

        uint8_t security_sectors[0x800] = {};
        cmd_kreon_get_security_sectors(*ctx.sptd, security_sectors);

        write_entry(fs_ss, security_sectors, sizeof(security_sectors), 0, 1, 0);
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

    for(uint32_t s = 0; s < sectors_count;)
    {
        bool increment = true;

        uint32_t sectors_to_read = std::min(sectors_at_once, sectors_count - s);

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
            auto status = cmd_read(*ctx.sptd, drive_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, dump_mode == DumpMode::REFINE && refine_counter);

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
