#include <format>
#include <stdexcept>
#include <windows.h>
#include "common.hh"
#include "windows.hh"



namespace gpsxre
{

std::string get_last_error()
{
	LPSTR buffer = nullptr;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
				  nullptr, ::GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, nullptr);

	std::string message(buffer);

	LocalFree(buffer);

	return message;
}


std::list<std::string> list_logical_drives()
{
	std::list<std::string> drives;

	DWORD drive_mask = GetLogicalDrives();
	if(!drive_mask)
		throw_line(std::format("WIN32 ({})", get_last_error()));

	for(uint32_t i = 0, n = sizeof(drive_mask) * CHAR_BIT; i < n; ++i)
	{
		if(drive_mask & 1 << i)
		{
			std::string drive(std::format("{}:", (char)('A' + i)));
			if(GetDriveType(std::format("{}\\", drive).c_str()) == DRIVE_CDROM)
				drives.push_back(drive);
			;
		}
	}

	return drives;
}

}
