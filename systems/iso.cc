#include <format>
#include "image_browser.hh"
#include "hex_bin.hh"
#include "iso.hh"



namespace gpsxre
{

SystemISO::SystemISO(const std::filesystem::path &track_path)
	: _trackPath(track_path)
	, _trackSize(std::filesystem::file_size(track_path))
{
	;
}


void SystemISO::operator()(std::ostream &os) const
{
	if(ImageBrowser::IsDataTrack(_trackPath))
	{
		ImageBrowser browser(_trackPath, 0, _trackSize, false);

		os << std::format("ISO9660 [{}]:", _trackPath.filename().string()) << std::endl;

		auto pvd = browser.GetPVD();
		os << "  PVD:" << std::endl;
		os << std::format("{}", hexdump((uint8_t *)&pvd, 0x320, 96));
	}
}

}
