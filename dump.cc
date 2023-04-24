#include "cmd.hh"
#include "common.hh"
#include "logger.hh"
#include "dump.hh"



namespace gpsxre
{

DriveConfig drive_init(SPTD &sptd, const Options &options)
{
	// set drive speed
	uint16_t speed = options.speed ? 150 * *options.speed : 0xFFFF;
	auto status = cmd_set_cd_speed(sptd, speed);
	if(status.status_code)
		LOG("drive set speed failed, SCSI ({})", SPTD::StatusMessage(status));

	// query/override drive configuration
	DriveConfig drive_config = drive_get_config(cmd_drive_query(sptd));
	drive_override_config(drive_config, options.drive_type.get(), options.drive_read_offset.get(),
		options.drive_c2_shift.get(), options.drive_pregap_start.get(), options.drive_read_method.get(), options.drive_sector_order.get());

	LOG("drive path: {}", options.drive);
	LOG("drive: {}", drive_info_string(drive_config));
	LOG("drive configuration: {}", drive_config_string(drive_config));

	return drive_config;
}


std::string image_init(const Options &options)
{
	if(options.image_name.empty())
		throw_line("image name is not provided");

	LOG("image path: {}", options.image_path.empty() ? "." : options.image_path);
	LOG("image name: {}", options.image_name);

	return (std::filesystem::path(options.image_path) / options.image_name).string();
}


void image_check_overwrite(std::filesystem::path state_path, const Options &options)
{
	if(!options.overwrite && std::filesystem::exists(state_path))
		throw_line(fmt::format("dump already exists (image name: {})", options.image_name));
}


void strip_toc_response(std::vector<uint8_t> &data)
{
	if(data.size() < sizeof(READ_TOC_Response))
		data.clear();
	else
		data.erase(data.begin(), data.begin() + sizeof(READ_TOC_Response));
}

}
