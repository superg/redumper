#pragma once



#include <filesystem>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#include "image_browser.hh"



namespace gpsxre
{

class SystemPSX
{
public:
	SystemPSX(const std::filesystem::path &track_path);

	void operator()(std::ostream &os) const;

private:
	static const std::string _EXE_MAGIC;
	static const std::vector<uint32_t> _LIBCRYPT_SECTORS_BASE;
	static const uint32_t _LIBCRYPT_SECTORS_SHIFT;
	static const std::set<uint32_t> _LIBCRYPT_SECTORS_COUNT;

	std::filesystem::path _trackPath;
	uint64_t _trackSize;

	std::string findEXE(ImageBrowser &browser) const;
	std::pair<std::string, std::string> deduceSerial(std::string exe_path) const;
	std::string detectRegion(std::string serial) const;
	bool findAntiModchipStrings(std::ostream &os, ImageBrowser &browser) const;
	bool detectEdcFast() const;
	bool detectLibCrypt(std::ostream &os, std::filesystem::path sub_path) const;
};

}
