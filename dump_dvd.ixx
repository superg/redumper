module;
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

export module dump_dvd;

import cd.cdrom;
import crc.crc32;
import drive;
import dump;
import hash.md5;
import hash.sha1;
import options;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.animation;
import utils.endian;
import utils.file_io;
import utils.logger;
import utils.misc;
import utils.strings;



namespace gpsxre
{

enum class LayerType : uint8_t
{
	EMBOSSED   = 1 << 0,
	RECORDABLE = 1 << 1,
	REWRITABLE = 1 << 2,
	RESERVED   = 1 << 3
};


static const std::string BOOK_TYPE[] =
{
	"DVD-ROM",
	"DVD-RAM",
	"DVD-R",
	"DVD-RW",
	"RESERVED",
	"RESERVED",
	"RESERVED",
	"RESERVED",
	"RESERVED",
	"DVD+RW",
	"RESERVED",
	"RESERVED",
	"RESERVED",
	"RESERVED",
	"RESERVED",
	"RESERVED"
};


static const std::string MAXIMUM_RATE[] =
{
	"2.52 mbps",
	"5.04 mbps",
	"10.08 mbps",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"not specified"
};


static const std::string LINEAR_DENSITY[] =
{
	"0.267 um/bit",
	"0.293 um/bit",
	"0.409 to 0.435 um/bit",
	"reserved",
	"0.280 to 0.291 um/bit",
	"reserved",
	"reserved",
	"reserved",
	"0.353 um/bit",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved"
};


static const std::string TRACK_DENSITY[] =
{
	"0.74 um/track",
	"0.80 um/track",
	"0.615 um/track",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved"
};


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

	uint32_t lba_start = endian_swap(layer_descriptor.data_start_sector);
	uint32_t lba_end = endian_swap(layer_descriptor.data_end_sector) + 1;
	LOG("{}data {{ LBA: {} .. {}, length: {}, hLBA: 0x{:08X} .. 0x{:08X} }}", indent, lba_start, lba_end, lba_end - lba_start, lba_start, lba_end);
	auto layer0_last = endian_swap(layer_descriptor.layer0_end_sector);
	if(layer0_last)
		LOG("{}data layer 0 last {{ LBA: {} .. hLBA: 0x{:08X} }}", indent, layer0_last, layer0_last);
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


std::set<READ_DVD_STRUCTURE_Format> get_readable_formats(SPTD &sptd)
{
	std::set<READ_DVD_STRUCTURE_Format> readable_formats;

	std::vector<uint8_t> structure;
	cmd_read_dvd_structure(sptd, structure, 0, 0, READ_DVD_STRUCTURE_Format::STRUCTURE_LIST, 0);
	strip_toc_response(structure);

	auto structures_count = (uint16_t)(structure.size() / sizeof(READ_DVD_STRUCTURE_StructureListEntry));
	auto structures = (READ_DVD_STRUCTURE_StructureListEntry *)structure.data();

	for(uint16_t i = 0; i < structures_count; ++i)
		if(structures[i].rds)
			readable_formats.insert((READ_DVD_STRUCTURE_Format)structures[i].format_code);

	return readable_formats;
}


std::vector<std::vector<uint8_t>> read_physical_structures(SPTD &sptd)
{
	std::vector<std::vector<uint8_t>> structures;

	uint32_t layers_count = 0;
	for(uint32_t i = 0; !layers_count || i < layers_count; ++i)
	{
		auto &structure = structures.emplace_back();
		cmd_read_dvd_structure(sptd, structure, 0, i, READ_DVD_STRUCTURE_Format::PHYSICAL, 0);
		strip_toc_response(structure);

		auto layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor *)structure.data();
		if(!layers_count)
			layers_count = (layer_descriptor->track_path ? 0 : layer_descriptor->layers_number) + 1;
	}

	return structures;
}


template<typename T>
void progress_output(T sector, T sectors_count, T errors)
{
	char animation = sector == sectors_count ? '*' : spinner_animation();

	LOGC_RF("{} [{:3}%] sector: {}/{}, errors: {{ SCSI: {} }}", animation, sector * 100 / sectors_count, extend_left(std::to_string(sector), ' ', digits_count(sectors_count)), sectors_count, errors);
}


export bool dump_dvd(const Options &options, DumpMode dump_mode)
{
	SPTD sptd(options.drive);

	auto drive_config = drive_init(sptd, options);
	auto image_prefix = image_init(options);

	std::filesystem::path iso_path(image_prefix + ".iso");
	std::filesystem::path state_path(image_prefix + ".state");

	if(dump_mode == DumpMode::DUMP)
		image_check_overwrite(state_path, options);

	// BD: cdb.reserved1 = 1, dump PIC area

	auto readable_formats = get_readable_formats(sptd);

	if(readable_formats.find(READ_DVD_STRUCTURE_Format::PHYSICAL) == readable_formats.end())
		throw_line("disc physical structure not found");

	auto physical_structures = read_physical_structures(sptd);

	uint32_t sectors_count = 0;
	for(uint32_t i = 0; i < physical_structures.size(); ++i)
	{
		if(physical_structures[i].size() < sizeof(READ_DVD_STRUCTURE_LayerDescriptor))
			throw_line("invalid layer descriptor size (layer: {})", i);

		auto layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor *)physical_structures[i].data();

		sectors_count += endian_swap(layer_descriptor->data_end_sector) + 1 - endian_swap(layer_descriptor->data_start_sector);
	}

	if(!sectors_count)
		throw_line("disc physical structure invalid");

	if(dump_mode == DumpMode::DUMP)
	{
		LOG("");
		LOG("disc structure:");
		for(uint32_t i = 0; i < physical_structures.size(); ++i)
		{
			write_vector(std::format("{}{}.physical", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i + 1) : ""), physical_structures[i]);

			print_physical_structure(*(READ_DVD_STRUCTURE_LayerDescriptor *)physical_structures[i].data(), i);

			if(readable_formats.find(READ_DVD_STRUCTURE_Format::MANUFACTURER) != readable_formats.end())
			{
				std::vector<uint8_t> manufacturer;
				cmd_read_dvd_structure(sptd, manufacturer, 0, i, READ_DVD_STRUCTURE_Format::MANUFACTURER, 0);
				strip_toc_response(manufacturer);

				if(!manufacturer.empty())
					write_vector(std::format("{}{}.manufacturer", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i + 1) : ""), manufacturer);
			}
		}
		LOG("");
	}
	// compare physical structures to stored to make sure it's the same disc
	else
	{
		for(uint32_t i = 0; i < physical_structures.size(); ++i)
		{
			auto structure_fn = std::format("{}{}.physical", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i + 1) : "");

			if(!std::filesystem::exists(structure_fn) || read_vector(structure_fn) != physical_structures[i])
				throw_line("disc / file physical structure doesn't match, refining from a different disc?");
		}
	}

	const uint32_t sectors_at_once = (dump_mode == DumpMode::REFINE ? 1 : options.dump_read_size);

	std::vector<uint8_t> sector_buffer(sectors_at_once * FORM1_DATA_SIZE);
	std::vector<State> state_success(sectors_at_once, State::SUCCESS);
	std::vector<State> state_failure(sectors_at_once, State::ERROR_SKIP);

	uint32_t errors_scsi = 0;

	std::fstream fs_iso(iso_path, std::fstream::out | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in) | std::fstream::binary);
	std::fstream fs_state(state_path, std::fstream::out | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in) | std::fstream::binary);

/*
	// preallocate data
	if(!refine)
	{
		LOG_F("preallocating space... ");
		write_entry(fs_iso, sector_buffer.data(), FORM1_DATA_SIZE, sectors_count - 1, 1, 0);
		write_entry(fs_state, (uint8_t *)state_failure.data(), sizeof(State), sectors_count - 1, 1, 0);
		LOG("done");
	}
*/

	CRC32 crc32;
	MD5 bh_md5;
	SHA1 bh_sha1;

	LOG("operation started");
	auto dump_time_start = std::chrono::high_resolution_clock::now();

	for(uint32_t s = 0; s < sectors_count; s += sectors_at_once)
	{
		progress_output(s, sectors_count, errors_scsi);

		uint32_t sectors_to_read = std::min(sectors_at_once, sectors_count - s);
		auto status = cmd_read(sptd, sector_buffer.data(), FORM1_DATA_SIZE, s, sectors_to_read, false);
		if(status.status_code)
			errors_scsi += sectors_to_read;
		else
		{
			write_entry(fs_iso, sector_buffer.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0);
			write_entry(fs_state, (uint8_t *)state_success.data(), sizeof(State), s, sectors_to_read, 0);
		}

		if(dump_mode != DumpMode::REFINE && !errors_scsi)
		{
			crc32.update(sector_buffer.data(), sectors_to_read * FORM1_DATA_SIZE);
			bh_md5.update(sector_buffer.data(), sectors_to_read * FORM1_DATA_SIZE);
			bh_sha1.update(sector_buffer.data(), sectors_to_read * FORM1_DATA_SIZE);
		}
	}

	progress_output(sectors_count, sectors_count, errors_scsi);
	LOG("");
	LOG("");

	auto dump_time_stop = std::chrono::high_resolution_clock::now();
	LOG("operation complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(dump_time_stop - dump_time_start).count());
	LOG("");

	LOG("media errors: ");
	LOG("  SCSI: {}", errors_scsi);
	LOG("");

	if(dump_mode != DumpMode::REFINE && !errors_scsi)
	{
		LOG("dat:");
		std::string filename = iso_path.filename().string();
		replace_all_occurences(filename, "&", "&amp;");

		LOG("<rom name=\"{}\" size=\"{}\" crc=\"{:08x}\" md5=\"{}\" sha1=\"{}\" />", filename, (uint64_t)sectors_count * FORM1_DATA_SIZE, crc32.final(), bh_md5.final(), bh_sha1.final());
	}

	return errors_scsi;
}

}
