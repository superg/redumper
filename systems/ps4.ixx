module;
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <ostream>
#include "system.hh"
#include "throw_line.hh"

export module systems.ps4;

import filesystem.iso9660;
import readers.sector_reader;
import utils.misc;
import utils.strings;



namespace gpsxre
{

export class SystemPS4 : public System
{
public:
	std::string getName() override
	{
		return "PS4";
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

		auto param_sfo = loadSFO(root_directory, "bd/param.sfo");

		auto it = param_sfo.find("VERSION");
		if(it != param_sfo.end())
			os << std::format("  version: {}", it->second) << std::endl;

		it = param_sfo.find("TITLE_ID");
		if(it != param_sfo.end())
			os << std::format("  serial: {}", it->second) << std::endl;
	}

private:
	static constexpr std::array<uint8_t, 4> _SFO_MAGIC = { 0x00, 0x50, 0x53, 0x46 };


	std::map<std::string, std::string> loadSFO(std::shared_ptr<iso9660::Entry> root_directory, std::string sfo_file) const
	{
		std::map<std::string, std::string> sfo;

		auto sfo_entry = root_directory->subEntry(sfo_file);
		if(sfo_entry)
		{
			auto data = sfo_entry->read();
			if(data.size() < 0x824)
				return sfo;

			if(!std::equal(data.begin() + 0x800, data.begin() + 0x804, _SFO_MAGIC.begin()))
				return sfo;

			uint32_t key_table = static_cast<uint32_t>(data[0x808]) |
								 (static_cast<uint32_t>(data[0x809]) << 8) |
								 (static_cast<uint32_t>(data[0x80A]) << 16) |
								 (static_cast<uint32_t>(data[0x80B]) << 24);
			uint32_t value_table = static_cast<uint32_t>(data[0x80C]) |
								   (static_cast<uint32_t>(data[0x80D]) << 8) |
								   (static_cast<uint32_t>(data[0x80E]) << 16) |
								   (static_cast<uint32_t>(data[0x80F]) << 24);
			uint32_t param_count = static_cast<uint32_t>(data[0x810]) |
								   (static_cast<uint32_t>(data[0x811]) << 8) |
								   (static_cast<uint32_t>(data[0x812]) << 16) |
								   (static_cast<uint32_t>(data[0x813]) << 24);

			if(data.size() < 0x814 + param_count * 0x10 || param_count > 255)
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
				key_pos = static_cast<uint16_t>(data[0x814 + i * 0x10]) |
						  (static_cast<uint16_t>(data[0x815 + i * 0x10]) << 8);
				value_fmt = static_cast<uint8_t>(data[0x817 + i * 0x10]);
				value_len = static_cast<uint32_t>(data[0x818 + i * 0x10]) |
							(static_cast<uint32_t>(data[0x819 + i * 0x10]) << 8) |
							(static_cast<uint32_t>(data[0x81A + i * 0x10]) << 16) |
							(static_cast<uint32_t>(data[0x81B + i * 0x10]) << 24);
				value_pos = static_cast<uint32_t>(data[0x820 + i * 0x10]) |
							(static_cast<uint32_t>(data[0x821 + i * 0x10]) << 8) |
							(static_cast<uint32_t>(data[0x822 + i * 0x10]) << 16) |
							(static_cast<uint32_t>(data[0x823 + i * 0x10]) << 24);
				next_key = static_cast<uint16_t>(data[0x814 + (i + 1) * 0x10]) |
						   (static_cast<uint16_t>(data[0x815 + (i + 1) * 0x10]) << 8);
				
				if(i == param_count - 1)
					key_len = value_table - key_table - key_pos;
				else
					key_len = next_key - key_pos;

				if (data.size() < 0x800 + key_table + key_pos + key_len)
					return sfo;
				key.assign(reinterpret_cast<char*>(&data[0x800 + key_table + key_pos]), key_len);
				
				if (value_fmt == 0x04)
				{
					if (data.size() < 0x800 + value_table + value_pos + 4)
						return sfo;
					value_num = static_cast<uint32_t>(data[0x800 + value_table + value_pos]) |
								(static_cast<uint32_t>(data[0x800 + value_table + value_pos + 1]) << 8) |
								(static_cast<uint32_t>(data[0x800 + value_table + value_pos + 2]) << 16) |
								(static_cast<uint32_t>(data[0x800 + value_table + value_pos + 3]) << 24);
					value_str.assign(std::to_string(value_num));
				}
				else
				{
					if (data.size() < 0x800 + value_table + value_pos + value_len)
						return sfo;
					value_str.assign(reinterpret_cast<char*>(&data[0x800 + value_table + value_pos]), value_len);
				}
				
				sfo.emplace(trim(erase_all(key, '\0')), trim(erase_all(value_str, '\0')));
			}
		}

		return sfo;
	}
};

}
