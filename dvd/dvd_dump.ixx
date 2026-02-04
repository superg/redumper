module;
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>
#include "throw_line.hh"

export module dvd.dump;

import cd.cdrom;
import common;
import drive;
import dvd;
import dvd.css;
import dvd.xbox;
import filesystem.iso9660;
import filesystem.udf;
import options;
import range;
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


uint32_t get_dvd_layer_length(const READ_DVD_STRUCTURE_LayerDescriptor &layer_descriptor)
{
    int32_t psn_first = sign_extend<24>(endian_swap(layer_descriptor.data_start_sector));
    int32_t psn_last = sign_extend<24>(endian_swap(layer_descriptor.data_end_sector));
    int32_t layer0_last = sign_extend<24>(endian_swap(layer_descriptor.layer0_end_sector));

    // for opposite layout the initial length is a difference between two layers (negative value)
    int32_t length = psn_last + 1 - psn_first;
    if(layer_descriptor.track_path)
        length += 2 * (layer0_last + 1);

    return length;
}


std::vector<uint32_t> get_bluray_layer_lengths(const std::vector<uint8_t> &structure, bool rom)
{
    std::vector<uint32_t> layer_lengths;

    auto di_units = &structure[sizeof(CMD_ParameterListHeader)];
    uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + (rom ? 52 : 100);
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

            auto layers_count = unit.header.layer + 1;
            layer_lengths.resize(layers_count);
            layer_lengths[unit.header.layer] = (layers_count == body->layers_count ? last_psn - 1 : last_aun) - first_aun + 2;
        }
    }

    return layer_lengths;
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

    uint32_t psn_first_raw = endian_swap(layer_descriptor.data_start_sector);
    uint32_t psn_last_raw = endian_swap(layer_descriptor.data_end_sector);
    uint32_t psn_l0_last_raw = endian_swap(layer_descriptor.layer0_end_sector);

    int32_t psn_first = sign_extend<24>(psn_first_raw);
    int32_t psn_last = sign_extend<24>(psn_last_raw);
    int32_t psn_l0_last = sign_extend<24>(psn_l0_last_raw);

    uint32_t length = get_dvd_layer_length(layer_descriptor);

    LOG("{}data {{ PSN: [{} .. {}], length: {}, hPSN: [0x{:06X} .. 0x{:06X}] }}", indent, psn_first, psn_last, length, psn_first_raw, psn_last_raw);
    if(psn_l0_last)
        LOG("{}data layer 0 last {{ PSN: {}, hPSN: 0x{:06X} }}", indent, psn_l0_last, psn_l0_last_raw);
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


struct FilesystemContext
{
    bool search = true;
    bool udf = false;
    std::vector<std::pair<uint32_t, uint32_t>> udf_vds;
};


std::optional<std::pair<uint32_t, bool>> filesystem_search_size(FilesystemContext &ctx, std::fstream &fs_iso, std::fstream &fs_state, std::span<uint8_t> data, uint32_t lba)
{
    std::optional<std::pair<uint32_t, bool>> ss;

    if(ctx.search)
    {
        if(lba >= iso9660::SYSTEM_AREA_SIZE)
        {
            auto const &descriptor = (iso9660::VolumeDescriptor &)data[0];

            if(auto si = to_string_view(descriptor.standard_identifier); si == iso9660::DESCRIPTOR_ID_CD)
            {
                if(descriptor.type == iso9660::VolumeDescriptorType::PRIMARY)
                {
                    auto const &pvd = (iso9660::PrimaryVolumeDescriptor &)descriptor;
                    if(pvd.volume_space_size.lsb)
                        ss = std::make_pair(pvd.volume_space_size.lsb, false);
                }
            }
            else if(udf::DESCRIPTORS.find(si) != udf::DESCRIPTORS.end())
            {
                if(si == udf::DESCRIPTOR_ID_NSR2 || si == udf::DESCRIPTOR_ID_NSR3)
                    ctx.udf = true;
                else if(si == udf::DESCRIPTOR_ID_TEA)
                    ctx.search = false;
            }
            else
                ctx.search = false;
        }
    }

    if(ctx.udf)
    {
        if(lba == udf::AVDP_PRIMARY_LBA)
        {
            if(auto const &avdp = (udf::AnchorVolumeDescriptorPointer &)data[0]; avdp.descriptor_tag.tag_identifier == udf::TagIdentifier::ANCHOR_POINTER)
            {
                // ordering is intentional
                ctx.udf_vds.emplace_back(avdp.reserve_vds.location, scale_up(avdp.reserve_vds.length, FORM1_DATA_SIZE));
                ctx.udf_vds.emplace_back(avdp.main_vds.location, scale_up(avdp.main_vds.length, FORM1_DATA_SIZE));
            }
        }

        if(!ctx.udf_vds.empty() && ctx.udf_vds.back().first + ctx.udf_vds.back().second <= lba)
        {
            std::vector<uint8_t> file_data(ctx.udf_vds.back().second * FORM1_DATA_SIZE);
            std::vector<State> file_state(ctx.udf_vds.back().second);

            read_entry(fs_iso, file_data.data(), FORM1_DATA_SIZE, ctx.udf_vds.back().first, ctx.udf_vds.back().second, 0, 0);
            read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), ctx.udf_vds.back().first, ctx.udf_vds.back().second, 0, (uint8_t)State::ERROR_SKIP);

            if(std::all_of(file_state.begin(), file_state.end(), [](State s) { return s == State::SUCCESS; }))
            {
                uint32_t sectors_count = 0;

                for(uint32_t i = 0; i < ctx.udf_vds.back().second; ++i)
                {
                    auto const &tag = (udf::DescriptorTag &)file_data[i * FORM1_DATA_SIZE];

                    if(tag.tag_identifier == udf::TagIdentifier::PARTITION)
                    {
                        auto const &partition = (udf::PartitionDescriptor &)file_data[i * FORM1_DATA_SIZE];

                        sectors_count = std::max(sectors_count, partition.partition_starting_location + partition.partition_length);
                    }
                    else if(tag.tag_identifier == udf::TagIdentifier::TERMINATING)
                        break;
                }

                ctx.udf_vds.clear();

                if(sectors_count)
                    // account for trailing AVDP
                    ss = std::make_pair(sectors_count + 1, true);
            }
            else
                ctx.udf_vds.pop_back();
        }
    }

    return ss;
}


struct DumpConfig
{
    std::string image_extension;
    uint32_t sector_size;
    int32_t lba_zero;
    int32_t lba_start;
    uint32_t overread_count;
};


DumpConfig dump_get_config(DiscType disc_type, bool raw)
{
    DumpConfig config{ ".iso", FORM1_DATA_SIZE, 0, 0, 0 };

    if(disc_type == DiscType::DVD && raw)
    {
        config = DumpConfig{ ".sdram", sizeof(RecordingFrame), DVD_LBA_START, DVD_LBA_RCZ - (int32_t)OVERREAD_COUNT, OVERREAD_COUNT };
    }

    return config;
}


export SPTD::Status read_dvd_sectors(SPTD &sptd, uint8_t *sectors, uint32_t sector_size, int32_t lba, uint32_t sectors_count, bool force_unit_access, DiscType disc_type, bool raw)
{
    SPTD::Status status;

    if(disc_type == DiscType::DVD && raw)
    {
        if(sector_size != sizeof(RecordingFrame))
            throw_line("invalid sector size for raw DVD read (expected: {}, actual: {})", sizeof(RecordingFrame), sector_size);

        std::vector<DataFrame> data_frames(sectors_count);
        status = cmd_read_omnidrive(sptd, (uint8_t *)data_frames.data(), sizeof(DataFrame), lba, sectors_count, OmniDrive_DiscType::DVD, false, force_unit_access, false, OmniDrive_Subchannels::NONE,
            false);

        for(uint32_t i = 0; i < sectors_count; ++i)
        {
            auto &recording_frame = (RecordingFrame &)sectors[i * sizeof(RecordingFrame)];
            recording_frame = DataFrame_to_RecordingFrame(data_frames[i]);
        }
    }
    else
        status = cmd_read(sptd, sectors, sector_size, lba, sectors_count, force_unit_access);

    return status;
}
void progress_output(int32_t lba, int32_t lba_start, int32_t lba_end, uint32_t errors)
{
    char animation = lba == lba_end ? '*' : spinner_animation();
    auto percent_dumped = lba_start < lba_end ? (int64_t)(lba - lba_start) * 100 / (lba_end - lba_start) : 100;
    LOGC_RF("{} [{:3}%] LBA: {:7}/{}, errors: {{ SCSI: {} }}", animation, percent_dumped, lba, lba_end, errors);
}


export bool redumper_dump_dvd(Context &ctx, const Options &options, DumpMode dump_mode)
{
    auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

    if(dump_mode == DumpMode::DUMP)
    {
        image_check_overwrite(options);

        if(!options.image_path.empty())
            std::filesystem::create_directories(options.image_path);
    }
    else
        image_check_exists(options);

    std::vector<Range<int32_t>> protection;
    for(auto const &p : string_to_ranges<int32_t>(options.skip))
        insert_range(protection, { p.first, p.second });

    bool omnidrive_firmware = is_omnidrive_firmware(ctx.drive_config) != std::nullopt;
    bool kreon_firmware = is_kreon_firmware(ctx.drive_config);
    bool kreon_locked = false;

    // unlock Kreon drive early, otherwise get capacity will return different value and check for xbox disc will fail
    if(kreon_firmware)
    {
        auto status = cmd_kreon_set_lock_state(*ctx.sptd, KREON_LockState::WXRIPPER);
        if(status.status_code)
            LOG("kreon: failed to unlock drive, SCSI ({})", SPTD::StatusMessage(status));
    }

    // get capacity
    uint32_t sectors_count_capacity;
    {
        uint32_t lba_last, block_length;
        auto status = cmd_read_capacity(*ctx.sptd, lba_last, block_length, false, 0, false);
        if(status.status_code)
            throw_line("failed to read capacity, SCSI ({})", SPTD::StatusMessage(status));
        if(block_length != FORM1_DATA_SIZE)
            throw_line("unsupported block size (block size: {})", block_length);
        sectors_count_capacity = lba_last + 1;
    }

    auto readable_formats = get_readable_formats(*ctx.sptd, ctx.disc_type == DiscType::BLURAY || ctx.disc_type == DiscType::BLURAY_R);

    bool trim_to_filesystem_size = options.filesystem_trim;
    FilesystemContext fs_ctx;

    std::shared_ptr<xbox::Context> xbox;
    std::optional<uint32_t> sectors_count_xbox;

    std::optional<uint32_t> sectors_count_physical;
    if(readable_formats.find(READ_DISC_STRUCTURE_Format::PHYSICAL) != readable_formats.end())
    {
        // function call changes rom flag if discrepancy is detected
        bool rom = ctx.disc_type == DiscType::BLURAY;
        auto physical_structures = read_physical_structures(*ctx.sptd, ctx.disc_type == DiscType::BLURAY || ctx.disc_type == DiscType::BLURAY_R, rom);
        if(ctx.disc_type == DiscType::BLURAY && !rom)
        {
            trim_to_filesystem_size = true;
            LOG("warning: Blu-ray current profile mismatch, dump will be trimmed to disc filesystem size");
        }

        if(!physical_structures.empty())
        {
            // Blu-ray
            if(ctx.disc_type == DiscType::BLURAY || ctx.disc_type == DiscType::BLURAY_R)
            {
                auto layer_lengths = get_bluray_layer_lengths(physical_structures.front(), rom);
                if(!layer_lengths.empty())
                    sectors_count_physical = std::accumulate(layer_lengths.begin(), layer_lengths.end(), (uint32_t)0);
            }
            // DVD
            else
            {
                for(uint32_t i = 0; i < physical_structures.size(); ++i)
                {
                    auto const &structure = physical_structures[i];

                    // verify physical structure is valid
                    if(structure.size() < sizeof(CMD_ParameterListHeader) + sizeof(READ_DVD_STRUCTURE_LayerDescriptor))
                        throw_line("invalid layer descriptor size (layer: {})", i);

                    // calculate physical sectors count based on all layers of physical structures
                    auto &layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor &)structure[sizeof(CMD_ParameterListHeader)];
                    sectors_count_physical = sectors_count_physical.value_or(0) + get_dvd_layer_length(layer_descriptor);
                }

                // XGD physical sector count is only for video partition
                if(auto &layer0_ld = (READ_DVD_STRUCTURE_LayerDescriptor &)physical_structures.front()[sizeof(CMD_ParameterListHeader)];
                    kreon_firmware && physical_structures.size() == 1 && get_dvd_layer_length(layer0_ld) != sectors_count_capacity)
                {
                    xbox = xbox::initialize(protection, *ctx.sptd, layer0_ld, sectors_count_capacity, options.kreon_partial_ss, is_custom_kreon_firmware(ctx.drive_config));

                    if(xbox)
                    {
                        sectors_count_xbox = get_dvd_layer_length(xbox::get_final_layer_descriptor(layer0_ld, xbox->security_sector));

                        // store security sector
                        auto security_sector_fn = image_prefix + ".security";
                        if(dump_mode == DumpMode::DUMP)
                            write_vector(security_sector_fn, xbox->security_sector);
                        else if(dump_mode == DumpMode::REFINE && !options.force_refine)
                        {
                            auto ss(xbox->security_sector);
                            xbox::clean_security_sector(ss);

                            // compare cleaned security sector to stored to make sure it's the same disc
                            bool match = false;
                            if(std::filesystem::exists(security_sector_fn))
                            {
                                auto ss_file = read_vector(security_sector_fn);
                                xbox::clean_security_sector(ss_file);
                                match = ss_file == ss;
                            }

                            if(!match)
                                throw_line("disc / file security sector mismatch, refining from a different disc?");
                        }
                    }
                    else
                    {
                        LOG("kreon: malformed XGD detected, continuing in normal dump mode");
                        LOG("");
                    }
                }
            }
        }

        if(dump_mode == DumpMode::DUMP)
        {
            // read and store manufacturer structure files
            if(readable_formats.find(READ_DISC_STRUCTURE_Format::MANUFACTURER) != readable_formats.end())
            {
                for(uint32_t i = 0; i < physical_structures.size(); ++i)
                {
                    std::vector<uint8_t> structure;
                    auto status = cmd_read_disc_structure(*ctx.sptd, structure, 0, 0, i, READ_DISC_STRUCTURE_Format::MANUFACTURER, 0);
                    if(status.status_code)
                        throw_line("failed to read disc manufacturer structure, SCSI ({})", SPTD::StatusMessage(status));

                    write_vector(std::format("{}{}.manufacturer", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i) : ""), structure);
                }
            }

            // store physical structure files
            for(uint32_t i = 0; i < physical_structures.size(); ++i)
                write_vector(std::format("{}{}.physical", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i) : ""), physical_structures[i]);

            // print physical structures information (Blu-ray)
            if(ctx.disc_type == DiscType::BLURAY || ctx.disc_type == DiscType::BLURAY_R)
            {
                uint32_t unit_size = sizeof(READ_DISC_STRUCTURE_DiscInformationUnit) + (rom ? 52 : 100);

                LOG("disc structure:");
                for(auto const &s : physical_structures)
                    print_di_units_structure(&s[sizeof(CMD_ParameterListHeader)], rom);
                LOG("");

                // layer break
                if(!physical_structures.empty())
                {
                    auto layer_lengths = get_bluray_layer_lengths(physical_structures.front(), rom);

                    uint32_t layer_break = 0;
                    for(uint32_t i = 0; i + 1 < layer_lengths.size(); ++i)
                    {
                        layer_break += layer_lengths[i];
                        LOG("layer break{}: {}", layer_lengths.size() > 1 ? std::format(" (layer: {})", i) : "", layer_break);
                    }

                    LOG("");
                }
            }
            // print physical structures information (DVD)
            else
            {
                std::vector<READ_DVD_STRUCTURE_LayerDescriptor> layer_descriptors;
                for(auto const &p : physical_structures)
                    layer_descriptors.push_back((READ_DVD_STRUCTURE_LayerDescriptor &)p[sizeof(CMD_ParameterListHeader)]);

                // adjust layer descriptor for xbox discs
                if(xbox)
                    layer_descriptors.front() = xbox::get_final_layer_descriptor(layer_descriptors.front(), xbox->security_sector);

                LOG("disc structure:");
                for(uint32_t i = 0; i < layer_descriptors.size(); ++i)
                    print_physical_structure(layer_descriptors[i], i);
                LOG("");

                // layer break
                if(!layer_descriptors.empty())
                {
                    auto const &layer_descriptor = layer_descriptors.front();

                    std::optional<uint32_t> layer_break;

                    // opposite
                    if(layer_descriptor.track_path)
                    {
                        int32_t psn_first = sign_extend<24>(endian_swap(layer_descriptor.data_start_sector));
                        int32_t layer0_last = sign_extend<24>(endian_swap(layer_descriptor.layer0_end_sector));

                        layer_break = layer0_last + 1 - psn_first;
                    }
                    // parallel
                    else if(layer_descriptors.size() > 1)
                        layer_break = get_dvd_layer_length(layer_descriptor);

                    if(layer_break)
                    {
                        LOG("layer break: {}", *layer_break);
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
                    throw_line("disc / file physical structure mismatch, refining from a different disc?");
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

    LOG("sectors count (READ_CAPACITY): {}", sectors_count_capacity);
    uint32_t sectors_count = sectors_count_capacity;
    if(sectors_count_xbox)
    {
        LOG("sectors count (XBOX): {}", *sectors_count_xbox);
        sectors_count = *sectors_count_xbox;
    }
    else
    {
        if(sectors_count_physical)
        {
            LOG("sectors count (PHYSICAL): {}", *sectors_count_physical);
            if(*sectors_count_physical != sectors_count_capacity)
                LOG("warning: READ_CAPACITY / PHYSICAL sectors count mismatch, using PHYSICAL");

            sectors_count = *sectors_count_physical;
        }
    }

    const uint32_t sectors_at_once = (dump_mode == DumpMode::REFINE ? 1 : options.dump_read_size);

    bool raw = options.dvd_raw && omnidrive_firmware;
    if(options.dvd_raw && !omnidrive_firmware)
        LOG("warning: drive not compatible with raw DVD dumping");

    auto cfg = dump_get_config(ctx.disc_type, raw);

    std::vector<uint8_t> file_data(sectors_at_once * cfg.sector_size);
    std::vector<State> file_state(sectors_at_once);

    auto file_mode = std::fstream::out | std::fstream::in | std::fstream::binary;
    if(dump_mode == DumpMode::DUMP)
        file_mode |= std::fstream::trunc;

    std::filesystem::path iso_path(image_prefix + cfg.image_extension);
    std::filesystem::path state_path(image_prefix + ".state");

    std::fstream fs_iso(iso_path, file_mode);
    std::fstream fs_state(state_path, file_mode);

    uint32_t refine_counter = 0;
    uint32_t refine_retries = options.retries ? options.retries : 1;

    ROMEntry rom_entry(iso_path.filename().string());
    bool rom_update = true;

    // TODO: can be implemented later
    if(raw)
    {
        rom_update = false;
        fs_ctx.search = false;
    }

    SignalINT signal;

    int32_t lba_start = cfg.lba_start;
    if(options.lba_start)
    {
        if(*options.lba_start < cfg.lba_zero)
            throw_line("lba_start minimum value: {}", cfg.lba_zero);
        lba_start = *options.lba_start;

        rom_update = false;
    }

    int32_t lba_end = sectors_count + cfg.overread_count;
    if(options.lba_end)
    {
        if(*options.lba_end < cfg.lba_zero)
            throw_line("lba_end minimum value: {}", cfg.lba_zero);
        lba_end = *options.lba_end;

        rom_update = false;
    }

    if(lba_start >= lba_end)
        throw_line("lba_start must be less than lba_end");

    Errors errors = {};
    if(dump_mode != DumpMode::DUMP)
    {
        std::vector<State> state_buffer(CHUNK_1MB);
        uint32_t count = lba_end - lba_start;
        for(uint32_t offset = 0; offset < count; offset += state_buffer.size())
        {
            uint32_t sectors_to_read = std::min((uint32_t)state_buffer.size(), count - offset);
            read_entry(fs_state, (uint8_t *)state_buffer.data(), sizeof(State), lba_start - cfg.lba_zero + offset, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);
            errors.scsi += std::count(state_buffer.begin(), state_buffer.begin() + sectors_to_read, State::ERROR_SKIP);
        }
    }

    for(int32_t lba = lba_start; lba < lba_end;)
    {
        progress_output(lba, lba_start, lba_end, errors.scsi);

        bool increment = true;

        int32_t lba_shift = 0;

        // ensure all sectors in the read belong to the same range (skip or non-skip)
        uint32_t sectors_to_read = std::min(sectors_at_once, (uint32_t)(lba_end - lba));
        auto base_range = find_range(protection, lba);
        for(uint32_t i = 0; i < sectors_to_read; ++i)
        {
            if(base_range != find_range(protection, lba + (int32_t)i))
            {
                sectors_to_read = i;
                break;
            }
        }

        if(xbox)
        {
            if(lba < xbox->lock_lba_start)
                sectors_to_read = std::min(sectors_to_read, xbox->lock_lba_start - lba);

            // lock kreon drive before L1 video reading
            if(!kreon_locked && lba >= xbox->lock_lba_start)
            {
                auto status = cmd_kreon_set_lock_state(*ctx.sptd, KREON_LockState::LOCKED);
                if(status.status_code)
                    throw_line("kreon: failed lock drive, SCSI ({})", SPTD::StatusMessage(status));
                if(options.verbose)
                    LOG_R("[LBA: {}] kreon: drive locked", lba);
                kreon_locked = true;
            }

            if(kreon_locked)
                lba_shift = xbox->layer1_video_lba_start - xbox->lock_lba_start;
        }

        bool read = true;
        bool store = false;

        uint32_t lba_index = lba - cfg.lba_zero;

        if(dump_mode == DumpMode::REFINE || dump_mode == DumpMode::VERIFY)
        {
            read_entry(fs_iso, file_data.data(), cfg.sector_size, lba_index, sectors_to_read, 0, 0);
            read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), lba_index, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);

            if(dump_mode == DumpMode::REFINE)
                read = std::any_of(file_state.begin(), file_state.end(), [](State s) { return s == State::ERROR_SKIP; });
        }

        if(read)
        {
            std::vector<uint8_t> drive_data(file_data.size());
            if(auto range = find_range(protection, lba); range != nullptr)
                store = true;
            else
            {
                if(auto status = read_dvd_sectors(*ctx.sptd, drive_data.data(), cfg.sector_size, lba + lba_shift, sectors_to_read, dump_mode == DumpMode::REFINE && refine_counter, ctx.disc_type, raw);
                    status.status_code)
                {
                    if(options.verbose)
                    {
                        std::string status_retries;
                        if(dump_mode == DumpMode::REFINE)
                            status_retries = std::format(", retry: {}", refine_counter + 1);
                        for(uint32_t i = 0; i < sectors_to_read; ++i)
                            LOG_R("[LBA: {}] SCSI error ({}){}", lba + i, SPTD::StatusMessage(status), status_retries);
                    }

                    if(dump_mode == DumpMode::DUMP)
                        errors.scsi += sectors_to_read;
                    else if(dump_mode == DumpMode::REFINE)
                    {
                        ++refine_counter;
                        if(refine_counter < refine_retries)
                            increment = false;
                        else
                        {
                            if(options.verbose)
                                for(uint32_t i = 0; i < sectors_to_read; ++i)
                                    LOG_R("[LBA: {}] correction failure", lba + i);

                            refine_counter = 0;
                        }
                    }
                }
                else
                    store = true;
            }

            if(store)
            {
                if(dump_mode == DumpMode::DUMP)
                {
                    file_data.swap(drive_data);

                    write_entry(fs_iso, file_data.data(), cfg.sector_size, lba_index, sectors_to_read, 0);
                    std::fill(file_state.begin(), file_state.end(), State::SUCCESS);
                    write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), lba_index, sectors_to_read, 0);
                }
                else if(dump_mode == DumpMode::REFINE)
                {
                    for(uint32_t i = 0; i < sectors_to_read; ++i)
                    {
                        if(file_state[i] == State::SUCCESS)
                            continue;

                        if(options.verbose)
                            LOG_R("[LBA: {}] correction success", lba + i);

                        std::copy(drive_data.begin() + i * cfg.sector_size, drive_data.begin() + (i + 1) * cfg.sector_size, file_data.begin() + i * cfg.sector_size);
                        file_state[i] = State::SUCCESS;

                        --errors.scsi;
                    }

                    refine_counter = 0;

                    write_entry(fs_iso, file_data.data(), cfg.sector_size, lba_index, sectors_to_read, 0);
                    write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), lba_index, sectors_to_read, 0);
                }
                else if(dump_mode == DumpMode::VERIFY)
                {
                    bool update = false;

                    for(uint32_t i = 0; i < sectors_to_read; ++i)
                    {
                        if(file_state[i] != State::SUCCESS)
                            continue;

                        if(!std::equal(file_data.begin() + i * cfg.sector_size, file_data.begin() + (i + 1) * cfg.sector_size, drive_data.begin() + i * cfg.sector_size))
                        {
                            if(options.verbose)
                                LOG_R("[LBA: {}] data mismatch, sector state updated", lba + i);

                            file_state[i] = State::ERROR_SKIP;
                            update = true;

                            ++errors.scsi;
                        }
                    }

                    if(update)
                        write_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), lba_index, sectors_to_read, 0);
                }
            }
        }

        if(!read || store)
        {
            if(rom_update)
                rom_entry.update(file_data.data(), sectors_to_read * cfg.sector_size);

            for(uint32_t i = 0; i < sectors_to_read; ++i)
            {
                if(auto ss = filesystem_search_size(fs_ctx, fs_iso, fs_state, std::span(&file_data[i * cfg.sector_size], cfg.sector_size), lba + i); ss)
                {
                    LOG_R("sectors count ({}): {}", ss->second ? "UDF" : "ISO9660", ss->first);

                    if(trim_to_filesystem_size && !options.lba_end)
                    {
                        lba_end = ss->first;
                        trim_to_filesystem_size = false;
                    }
                }
            }
        }
        else
            rom_update = false;

        if(signal.interrupt())
        {
            LOG_R("[LBA: {}] forced stop ", lba);
            break;
        }

        if(increment)
            lba += sectors_to_read;
    }

    // re-unlock drive before returning
    if(kreon_locked)
    {
        auto status = cmd_kreon_set_lock_state(*ctx.sptd, KREON_LockState::WXRIPPER);
        if(status.status_code)
            LOG("kreon: failed to unlock drive at end of dump, SCSI ({})", SPTD::StatusMessage(status));
    }

    if(!signal.interrupt())
    {
        progress_output(lba_end, lba_start, lba_end, errors.scsi);
        LOG("");
    }
    LOG("");

    LOG("media errors: ");
    LOG("  SCSI: {}", errors.scsi);
    ctx.dump_errors = errors;

    if(signal.interrupt())
        signal.raiseDefault();

    if(rom_update)
        ctx.dat = std::vector<std::string>(1, rom_entry.xmlLine());

    return errors.scsi;
}

}
