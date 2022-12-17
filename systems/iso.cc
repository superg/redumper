#include "iso.hh"



namespace gpsxre
{


SystemISO::SystemISO(const std::filesystem::path &file_path)
{
}


SystemISO::~SystemISO()
{
	;
}


std::string SystemISO::getName() const
{
	return "ISO9660";
}


bool SystemISO::isValid() const
{
	return true;
}


void SystemISO::print(std::ostream &os) const
{
	os << "AAAAAAAAAAA" << std::endl;
}

}
