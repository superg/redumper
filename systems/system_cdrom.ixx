module;
#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <vector>
#include "throw_line.hh"

export module systems.cdrom;

import cd.cd;
import cd.cdrom;
import cd.ecc;
import cd.edc;
import systems.system;
import utils.file_io;



namespace gpsxre
{

export class SystemCDROM : public System
{
public:
	std::string getName() override
	{
		return "CD-ROM";
	}

	Type getType() override
	{
		return Type::RAW_DATA;
	}

	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override;
};


void SystemCDROM::printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const
{
	uint32_t sectors_count = sector_reader->getSectorsCount();

	uint32_t invalid_sync = 0;
	uint32_t mode2_form1 = 0;
	uint32_t mode2_form2 = 0;
	uint32_t mode2_form2_edc = 0;
	uint32_t ecc_errors = 0;
	uint32_t edc_errors = 0;
	uint32_t subheader_mismatches = 0;
	uint32_t redump_errors = 0;

	std::vector<uint32_t> modes(3);
	uint32_t invalid_modes = 0;

	Sector sector;
	for(int32_t i = 0; i < sectors_count; ++i)
	{
		if(!sector_reader->read((uint8_t *)&sector, i, 1))
			throw_line("read failed (sector: {})", i);

		if(memcmp(sector.sync, CD_DATA_SYNC, sizeof(CD_DATA_SYNC)))
		{
			// not a data track
			if(!i)
				return;

			++invalid_sync;
			continue;
		}

		if(sector.header.mode < modes.size())
			++modes[sector.header.mode];
		else
			++invalid_modes;

		if(sector.header.mode == 0)
		{
			;
		}
		else if(sector.header.mode == 1)
		{
			bool error_detected = false;

			Sector::ECC ecc(ECC().Generate((uint8_t *)&sector.header));
			if(memcmp(ecc.p_parity, sector.mode1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode1.ecc.q_parity, sizeof(ecc.q_parity)))
			{
				++ecc_errors;
				error_detected = true;
			}

			uint32_t edc = EDC().update((uint8_t *)&sector, offsetof(Sector, mode1.edc)).final();
			if(edc != sector.mode1.edc)
			{
				++edc_errors;
				error_detected = true;
			}

			// log dual ECC/EDC mismatch as one error
			if(error_detected)
				++redump_errors;
		}
		// XA Mode2 EDC covers subheader, subheader copy and user data, user data size depends on Form1 / Form2 flag
		else if(sector.header.mode == 2)
		{
			// subheader mismatch, just a warning
			if(memcmp(&sector.mode2.xa.sub_header, &sector.mode2.xa.sub_header_copy, sizeof(sector.mode2.xa.sub_header)))
			{
				++subheader_mismatches;
				++redump_errors;
			}

			// Form2
			if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
			{
				++mode2_form2;

				// Form2 EDC can be zero depending on mastering utility
				if(sector.mode2.xa.form2.edc)
				{
					uint32_t edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header,
												offsetof(Sector, mode2.xa.form2.edc) - offsetof(Sector, mode2.xa.sub_header)).final();

					if(edc != sector.mode2.xa.form2.edc)
					{
						++edc_errors;
						++redump_errors;
					}

					++mode2_form2_edc;
				}
			}
			// Form1
			else
			{
				++mode2_form1;

				bool error_detected = false;

				// EDC
				uint32_t edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header,
											offsetof(Sector, mode2.xa.form1.edc) - offsetof(Sector, mode2.xa.sub_header)).final();
				if(edc != sector.mode2.xa.form1.edc)
				{
					++edc_errors;
					error_detected = true;
				}

				// ECC
				// modifies sector, make sure sector data is not used after ECC calculation, otherwise header has to be restored
				Sector::Header header = sector.header;
				std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0);

				Sector::ECC ecc(ECC().Generate((uint8_t *)&sector.header));
				if(memcmp(ecc.p_parity, sector.mode2.xa.form1.ecc.p_parity, sizeof(ecc.p_parity)) || memcmp(ecc.q_parity, sector.mode2.xa.form1.ecc.q_parity, sizeof(ecc.q_parity)))
				{
					++ecc_errors;
					error_detected = true;
				}

				// restore modified sector header
				sector.header = header;

				// log dual ECC/EDC mismatch as one error
				if(error_detected)
					++redump_errors;
			}
		}
	}

	os << std::format("  sectors count: {}", sectors_count) << std::endl;
	for(uint32_t i = 0; i < modes.size(); ++i)
		if(modes[i])
			os << std::format("  mode{} sectors: {}", i, modes[i]) << std::endl;

	if(mode2_form1)
		os << std::format("  mode2 (form 1) sectors: {}", mode2_form1) << std::endl;
	if(mode2_form2)
	{
		os << std::format("  mode2 (form 2) sectors: {}", mode2_form2) << std::endl;
		os << std::format("  mode2 (form 2) EDC: {}", mode2_form2_edc ? "yes" : "no") << std::endl;
	}
	if(invalid_sync)
		os << std::format("  invalid sync sectors: {}", invalid_sync) << std::endl;
	if(invalid_modes)
		os << std::format("  invalid mode sectors: {}", invalid_modes) << std::endl;
	if(ecc_errors)
		os << std::format("  ECC errors: {}", ecc_errors) << std::endl;
	if(edc_errors)
		os << std::format("  EDC errors: {}", edc_errors) << std::endl;
	if(subheader_mismatches)
		os << std::format("  CD-XA subheader mismatches: {}", subheader_mismatches) << std::endl;
	os << std::endl;
	os << std::format("  REDUMP.ORG errors: {}", redump_errors) << std::endl;
}

}
