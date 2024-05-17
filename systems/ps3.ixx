module;
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps3;

import filesystem.iso9660;
import readers.sector_reader;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemPS3 : public System
{
public:
	std::string getName() override
	{
		return "PS3";
	}


	Type getType() override
	{
		return Type::ISO;
	}


	void printInfo(std::ostream &os, SectorReader *sector_reader, const std::filesystem::path &) const override
	{
		iso9660::PrimaryVolumeDescriptor pvd;
		if(!iso9660::Browser::findDescriptor((iso9660::VolumeDescriptor &)pvd, sector_reader, iso9660::VolumeDescriptorType::PRIMARY))
			return;
		auto root_directory = iso9660::Browser::rootDirectory(sector_reader, pvd);

		auto ps3_disc_sfb = loadSFB(root_directory, "PS3_DISC.SFB");
		auto param_sfo = loadSFO(root_directory, "PS3_GAME/PARAM.SFO");

		auto it = ps3_disc_sfb.find("VERSION");
		if(it != ps3_disc_sfb.end())
			os << std::format("  version: {}", it->second) << std::endl;
		else
		{
			it = param_sfo.find("VERSION");
			if(it != param_sfo.end())
				os << std::format("  version: {}", it->second) << std::endl;
		}

		std::string title_id;
		it = ps3_disc_sfb.find("TITLE_ID");
		if(it != ps3_disc_sfb.end())
			title_id = it->second;
		else
		{
			it = param_sfo.find("TITLE_ID");
			if(it != param_sfo.end())
				title_id = it->second;
		}

		auto serial = deduceSerial(title_id);
		if(!serial.first.empty() && !serial.second.empty())
			os << std::format("  serial: {}-{}", serial.first, serial.second) << std::endl;

		auto region = detectRegion(serial.first);
		if(!region.empty())
			os << std::format("  region: {}", region) << std::endl;

		auto fw = deduceFirmware(root_directory, "PS3_UPDATE/PS3UPDAT.PUP");
		if(!fw.empty())
			os << std::format("  firmware: {}", fw) << std::endl;
	}

private:
	static constexpr std::array<uint8_t, 4> _SFB_MAGIC = { 0x2E, 0x53, 0x46, 0x42 };
	static constexpr std::array<uint8_t, 4> _SFO_MAGIC = { 0x00, 0x50, 0x53, 0x46 };

	std::map<std::string, std::string> loadSFB(std::shared_ptr<iso9660::Entry> root_directory, std::string sfb_file) const
	{
		std::map<std::string, std::string> sfb;

		auto sfb_entry = root_directory->subEntry(sfb_file);
		if(sfb_entry)
		{
			auto data = sfb_entry->read();
			if(data.size() < 0x200)
				return sfb;

			if(!std::equal(data.begin(), data.begin() + 4, _SFB_MAGIC.begin()))
				return sfb;

			size_t pos = 0x20;
			std::string key(data.begin() + pos, data.begin() + pos + 0x10);
			std::string value;
			uint32_t offset;
			uint32_t length;
			while(pos < 0x1E0)
			{
				offset = (static_cast<uint32_t>(data[pos + 0x10]) << 24) |
						 (static_cast<uint32_t>(data[pos + 0x11]) << 16) |
						 (static_cast<uint32_t>(data[pos + 0x12]) << 8) |
						  static_cast<uint32_t>(data[pos + 0x13]);
				length = (static_cast<uint32_t>(data[pos + 0x14]) << 24) |
					     (static_cast<uint32_t>(data[pos + 0x15]) << 16) |
					     (static_cast<uint32_t>(data[pos + 0x16]) << 8) |
					      static_cast<uint32_t>(data[pos + 0x17]);

				if(data.size() < offset + length)
					return sfb;

				value.assign(reinterpret_cast<char*>(&data[offset]), length);

				sfb.emplace(trim(erase_all(key, '\0')), trim(erase_all(value, '\0')));

				pos += 0x20;

				key.assign(data.begin() + pos, data.begin() + pos + 0x10);
				
				if (key[0] == '\0')
					return sfb;
			}
		}

		return sfb;
	}


	std::map<std::string, std::string> loadSFO(std::shared_ptr<iso9660::Entry> root_directory, std::string sfo_file) const
	{
		std::map<std::string, std::string> sfo;

		auto sfo_entry = root_directory->subEntry(sfo_file);
		if(sfo_entry)
		{
			auto data = sfo_entry->read();
			if(data.size() < 0x24)
				return sfo;

			if(!std::equal(data.begin(), data.begin() + 4, _SFO_MAGIC.begin()))
				return sfo;

			uint32_t key_table = static_cast<uint32_t>(data[0x08]) |
								 (static_cast<uint32_t>(data[0x09]) << 8) |
								 (static_cast<uint32_t>(data[0x0A]) << 16) |
								 (static_cast<uint32_t>(data[0x0B]) << 24);
			uint32_t value_table = static_cast<uint32_t>(data[0x0C]) |
								   (static_cast<uint32_t>(data[0x0D]) << 8) |
								   (static_cast<uint32_t>(data[0x0E]) << 16) |
								   (static_cast<uint32_t>(data[0x0F]) << 24);
			uint32_t param_count = static_cast<uint32_t>(data[0x10]) |
								   (static_cast<uint32_t>(data[0x11]) << 8) |
								   (static_cast<uint32_t>(data[0x12]) << 16) |
								   (static_cast<uint32_t>(data[0x13]) << 24);

			if(data.size() < 0x14 + param_count * 0x10 || param_count > 255)
				return sfo;
			
			std::string key;
			std::string value_str;
			uint32_t value_num;
			uint16_t key_pos;
			uint16_t next_key;
			uint16_t key_len;
			uint8_t value_fmt;
			uint32_t value_len;
			uint32_t value_pos;
			for(int i = 0; i < param_count; ++i)
			{
				key_pos = static_cast<uint16_t>(data[0x14 + i * 0x10]) |
						  (static_cast<uint16_t>(data[0x15 + i * 0x10]) << 8);
				value_fmt = static_cast<uint8_t>(data[0x17 + i * 0x10]);
				value_len = static_cast<uint32_t>(data[0x18 + i * 0x10]) |
							(static_cast<uint32_t>(data[0x19 + i * 0x10]) << 8) |
							(static_cast<uint32_t>(data[0x1A + i * 0x10]) << 16) |
							(static_cast<uint32_t>(data[0x1B + i * 0x10]) << 24);
				value_pos = static_cast<uint32_t>(data[0x20 + i * 0x10]) |
							(static_cast<uint32_t>(data[0x21 + i * 0x10]) << 8) |
							(static_cast<uint32_t>(data[0x22 + i * 0x10]) << 16) |
							(static_cast<uint32_t>(data[0x23 + i * 0x10]) << 24);
				next_key = static_cast<uint16_t>(data[0x14 + (i + 1) * 0x10]) |
						   (static_cast<uint16_t>(data[0x15 + (i + 1) * 0x10]) << 8);
				
				if(i == param_count - 1)
					key_len = value_table - key_table - key_pos;
				else
					key_len = next_key - key_pos;

				if (data.size() < key_table + key_pos + key_len)
					return sfo;
				key.assign(reinterpret_cast<char*>(&data[key_table + key_pos]), key_len);
				
				if (value_fmt == 0x04)
				{
					if (data.size() < value_table + value_pos + 4)
						return sfo;
					value_num = static_cast<uint32_t>(data[value_table + value_pos]) |
								(static_cast<uint32_t>(data[value_table + value_pos + 1]) << 8) |
								(static_cast<uint32_t>(data[value_table + value_pos + 2]) << 16) |
								(static_cast<uint32_t>(data[value_table + value_pos + 3]) << 24);
					value_str.assign(std::to_string(value_num));
				}
				else
				{
					if (data.size() < value_table + value_pos + value_len)
						return sfo;
					value_str.assign(reinterpret_cast<char*>(&data[value_table + value_pos]), value_len);
				}

				sfo.emplace(trim(erase_all(key, '\0')), trim(erase_all(value_str, '\0')));	
			}
		}

		return sfo;
	}


	std::pair<std::string, std::string> deduceSerial(std::string title_id) const
	{
		std::pair<std::string, std::string> serial;

		if(title_id.size() == 9)
		{
			serial.first = title_id.substr(0, 4);
			serial.second = title_id.substr(4, 5);
		}
		else if(title_id.size() == 10 && title_id[4] == '-')
		{
			serial.first = title_id.substr(0, 4);
			serial.second = title_id.substr(5, 5);
		}

		return serial;
	}


	std::string detectRegion(std::string prefix) const
	{
		std::string region;

		if (prefix.size() < 4)
			return region;

		// All Internal serials currently in redump.org
		//const std::set<std::string> REGION_A{ "BCAS", "BLAS"};
		//const std::set<std::string> REGION_E{ "BCED", "BCES", "BCET", "BLED", "BLES" };
		//const std::set<std::string> REGION_J{ "BCJB", "BCJS", "BCJX", "BCJS", "BLJB", "BLJM", "BLJS", "BLJX" };
		//const std::set<std::string> REGION_K{ "BCKS", "BLKS" };
		//const std::set<std::string> REGION_U{ "BCUS", "BLUD", "BLUS" };
		
		// Determine region based on third char in prefix
		if(prefix[2] == 'A')
			region = "Asia";
		else if(prefix[2] == 'E')
			region = "Europe";
		else if(prefix[2] == 'J')
			region = "Japan";
		else if(prefix[2] == 'K')
			region = "South Korea";
		else if(prefix[2] == 'U')
			region = "USA";

		return region;
	}


	std::string deduceFirmware(std::shared_ptr<iso9660::Entry> root_directory, std::string pup_file) const
	{
		std::string fw;

		auto pup_entry = root_directory->subEntry(pup_file);
		if(pup_entry)
		{
			auto data = pup_entry->read();
			if(data.size() < 0x40)
				return fw;

			uint16_t index = (static_cast<uint16_t>(data[0x3E]) << 8) | data[0x3F];
			if(data.size() < index + 4)
				return fw;

			fw.assign(reinterpret_cast<char*>(&data[index]), 4);
		}

		return fw;
	}
};

}
