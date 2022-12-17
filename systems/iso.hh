#pragma once



#include "system.hh"



namespace gpsxre
{

class SystemISO : public System
{
public:
	SystemISO(const std::filesystem::path &file_path);
	virtual ~SystemISO();

	virtual std::string getName() const override;
	virtual bool isValid() const override;
	virtual void print(std::ostream &os) const override;
};

}
