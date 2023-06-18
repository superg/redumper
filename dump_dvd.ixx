module;
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include "throw_line.hh"

export module dump_dvd;

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
	strip_response_header(structure);

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
		strip_response_header(structure);

		auto layer_descriptor = (READ_DVD_STRUCTURE_LayerDescriptor *)structure.data();
		if(!layers_count)
			layers_count = (layer_descriptor->track_path ? 0 : layer_descriptor->layers_number) + 1;
	}

	return structures;
}


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


std::map<std::string, iso9660::DirectoryRecord> dr_list(SPTD &sptd, const iso9660::DirectoryRecord &dr)
{
	std::map<std::string, iso9660::DirectoryRecord> entries;

	if(!(dr.file_flags & (uint8_t)iso9660::DirectoryRecord::FileFlags::DIRECTORY))
		return entries;

	uint32_t sectors_count = scale_up(dr.data_length.lsb, FORM1_DATA_SIZE);
	std::vector<uint8_t> buffer(sectors_count * FORM1_DATA_SIZE);
	auto status = cmd_read(sptd, buffer.data(), FORM1_DATA_SIZE, dr.offset.lsb, sectors_count, false);
	if(status.status_code)
		throw_line("failed to read directory record, SCSI ({})", SPTD::StatusMessage(status));
	buffer.resize(dr.data_length.lsb);

	for(uint32_t i = 0, n = (uint32_t)buffer.size(); i < n;)
	{
		iso9660::DirectoryRecord &dr = *(iso9660::DirectoryRecord *)&buffer[i];

		if(dr.length && dr.length <= FORM1_DATA_SIZE - i % FORM1_DATA_SIZE)
		{
			char b1 = (char)buffer[i + sizeof(dr)];
			if(b1 != (char)iso9660::Characters::DIR_CURRENT && b1 != (char)iso9660::Characters::DIR_PARENT)
			{
				std::string identifier((const char *)&buffer[i + sizeof(dr)], dr.file_identifier_length);
				auto s = identifier.find((char)iso9660::Characters::SEPARATOR2);
				std::string name(s == std::string::npos ? identifier : identifier.substr(0, s));

				entries[name] = dr;
			}

			i += dr.length;
		}
		// skip sector boundary
		else
			i = ((i / FORM1_DATA_SIZE) + 1) * FORM1_DATA_SIZE;
	}

	return entries;
}


//TODO: reimplement properly after ImageBrowser rewrite
std::map<std::string, uint32_t> extract_vob_list(SPTD &sptd)
{
	std::map<std::string, uint32_t> titles;

	SPTD::Status status;

	// read PVD root directory record lba
	iso9660::DirectoryRecord root_dr;
	bool pvd_found = false;
	for(uint32_t lba = iso9660::SYSTEM_AREA_SIZE; ; ++lba)
	{
		std::vector<uint8_t> sector(FORM1_DATA_SIZE);
		status = cmd_read(sptd, sector.data(), FORM1_DATA_SIZE, lba, 1, false);
		if(status.status_code)
			throw_line("failed to read PVD, SCSI ({})", SPTD::StatusMessage(status));

		auto vd = (iso9660::VolumeDescriptor *)sector.data();
		if(memcmp(vd->standard_identifier, iso9660::STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)))
			break;

		if(vd->type == iso9660::VolumeDescriptor::Type::PRIMARY)
		{
			auto pvd = (iso9660::VolumeDescriptor *)vd;
			root_dr = pvd->primary.root_directory_record;
			pvd_found = true;
			break;
		}
		else if(vd->type == iso9660::VolumeDescriptor::Type::SET_TERMINATOR)
			break;
	}

	if(!pvd_found)
		throw_line("PVD not found");

	auto root_entries = dr_list(sptd, root_dr);
	auto it = root_entries.find("VIDEO_TS");
	if(it != root_entries.end())
	{
		auto video_entries = dr_list(sptd, it->second);
		for(auto const &e : video_entries)
			if(ends_with(e.first, ".VOB"))
				titles[e.first] = e.second.offset.lsb;
	}

	return titles;
}


void progress_output(uint32_t sector, uint32_t sectors_count, uint32_t errors)
{
	char animation = sector == sectors_count ? '*' : spinner_animation();

	LOGC_RF("{} [{:3}%] sector: {}/{}, errors: {{ SCSI: {} }}", animation, sector * 100 / sectors_count,
			extend_left(std::to_string(sector), ' ', digits_count(sectors_count)), sectors_count, errors);
}


export DumpStatus dump_dvd(const Options &options, DumpMode dump_mode)
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
				strip_response_header(manufacturer);

				if(!manufacturer.empty())
					write_vector(std::format("{}{}.manufacturer", image_prefix, physical_structures.size() > 1 ? std::format(".{}", i + 1) : ""), manufacturer);
			}
		}
		LOG("");

		// protection
		if(readable_formats.find(READ_DVD_STRUCTURE_Format::COPYRIGHT) != readable_formats.end())
		{
			std::vector<uint8_t> copyright;
			cmd_read_dvd_structure(sptd, copyright, 0, 0, READ_DVD_STRUCTURE_Format::COPYRIGHT, 0);
			strip_response_header(copyright);

			auto ci = (READ_DVD_STRUCTURE_CopyrightInformation *)copyright.data();
			auto cpst = (READ_DVD_STRUCTURE_CopyrightInformation_CPST)ci->copyright_protection_system_type;

			LOG("copyright: ");

			//TODO: distinguish CSS/CPPM?
			std::string protection("unknown");
			if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::NONE)
				protection = "none";
			else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
				protection = "CSS/CPPM";
			else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CPRM)
				protection = "CPRM";
			LOG("  protection system type: {}", protection);
			LOG("  region management information: {}", region_string(ci->region_management_information));

			if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CSS_CPPM)
			{
				CSS css(sptd);

				auto disc_key = css.getDiscKey();
				if(!disc_key.empty())
				{
					LOG("  disc key: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}", disc_key[0], disc_key[1], disc_key[2], disc_key[3], disc_key[4]);
					LOG("  title keys:");
					auto titles = extract_vob_list(sptd);
					for(auto const &t : titles)
					{
						auto title_key = css.getTitleKey(disc_key, t.second);
						if(!title_key.empty())
							LOG("    {}: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}", t.first, title_key[0], title_key[1], title_key[2], title_key[3], title_key[4]);
					}
				}
			}
			else if(cpst == READ_DVD_STRUCTURE_CopyrightInformation_CPST::CPRM)
			{
				LOG("warning: CPRM protection is unsupported");
			}

			LOG("");
		}
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

	std::vector<uint8_t> file_data(sectors_at_once * FORM1_DATA_SIZE);
	std::vector<State> file_state(sectors_at_once);

	std::fstream fs_iso(iso_path, std::fstream::out | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in) | std::fstream::binary);
	std::fstream fs_state(state_path, std::fstream::out | (dump_mode == DumpMode::DUMP ? std::fstream::trunc : std::fstream::in) | std::fstream::binary);

	uint32_t refine_counter = 0;
	uint32_t refine_retries = options.retries ? options.retries : 1;

	uint32_t errors_scsi = 0;
	if(dump_mode != DumpMode::DUMP)
	{
		std::vector<State> state_buffer(sectors_count);
		read_entry(fs_state, (uint8_t *)state_buffer.data(), sizeof(State), 0, sectors_count, 0, (uint8_t)State::ERROR_SKIP);
		errors_scsi = std::count(state_buffer.begin(), state_buffer.end(), State::ERROR_SKIP);
	}

	ROMEntry rom_entry(iso_path.filename().string());

	LOG("operation started");
	auto dump_time_start = std::chrono::high_resolution_clock::now();

	bool interrupted = false;
	Signal::get().engage();
	int signal_dummy = 0;
	std::unique_ptr<int, std::function<void(int *)>> signal_guard(&signal_dummy, [](int *)
	{
		Signal::get().disengage();
	});
	
	for(uint32_t s = 0; s < sectors_count;)
	{
		bool increment = true;

		uint32_t sectors_to_read = std::min(sectors_at_once, sectors_count - s);

		progress_output(s, sectors_count, errors_scsi);

		bool read = false;
		if(dump_mode == DumpMode::DUMP)
		{
			read = true;
		}
		else if(dump_mode == DumpMode::REFINE || dump_mode == DumpMode::VERIFY)
		{
			read_entry(fs_iso, (uint8_t *)file_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, 0, 0);

			read_entry(fs_state, (uint8_t *)file_state.data(), sizeof(State), s, sectors_to_read, 0, (uint8_t)State::ERROR_SKIP);
			read = std::count(file_state.begin(), file_state.end(), State::ERROR_SKIP);
		}

		if(read)
		{
			std::vector<uint8_t> drive_data(sectors_at_once * FORM1_DATA_SIZE);
			auto status = cmd_read(sptd, drive_data.data(), FORM1_DATA_SIZE, s, sectors_to_read, dump_mode == DumpMode::REFINE && refine_counter);

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

		rom_entry.update(file_data.data(), sectors_to_read * FORM1_DATA_SIZE);

		if(Signal::get().interrupt())
		{
			LOG_R("[sector: {:6}] forced stop ", s);
			interrupted = true;
			break;
		}

		if(increment)
			s += sectors_at_once;
	}

	if(!interrupted)
	{
		progress_output(sectors_count, sectors_count, errors_scsi);
		LOG("");
		LOG("");
	}

	auto dump_time_stop = std::chrono::high_resolution_clock::now();
	LOG("operation complete (time: {}s)", std::chrono::duration_cast<std::chrono::seconds>(dump_time_stop - dump_time_start).count());
	LOG("");

	LOG("media errors: ");
	LOG("  SCSI: {}", errors_scsi);
	LOG("");

	if(!errors_scsi && !interrupted)
	{
		LOG("dat:");
		LOG("{}", rom_entry.xmlLine());
		LOG("");
	}

	return interrupted ? DumpStatus::INTERRUPTED : (errors_scsi ? DumpStatus::ERRORS : DumpStatus::SUCCESS);
}

}
