module;
#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660:entry;

import :defs;
import readers.sector_reader;
import cd.cdrom;
import utils.endian;
import utils.misc;
import utils.strings;



#define DIRECTORY_RECORD_WORKAROUNDS



export namespace gpsxre::iso9660
{

class Entry
{
public:
	Entry(SectorReader *sector_reader, const std::string &name, uint32_t version, const iso9660::DirectoryRecord &directory_record)
		: _sectorReader(sector_reader)
		, _name(name)
		, _version(version)
		, _directoryRecord(directory_record)
	{
		;
	}


	const std::string &name() const
	{
		return _name;
	}


	uint32_t version() const
	{
		return _version;
	}


	time_t dateTime() const
	{
		return convert_time(_directoryRecord.recording_date_time);
	}


	uint32_t sectorOffset() const
	{
		return _directoryRecord.offset.lsb;
	}


	uint32_t sectorSize() const
	{
		return round_up(_directoryRecord.data_length.lsb, FORM1_DATA_SIZE);
	}


	bool isDirectory() const
	{
		return _directoryRecord.file_flags & (uint8_t)iso9660::DirectoryRecord::FileFlags::DIRECTORY;
	}

	
	std::list<std::shared_ptr<Entry>> entries()
	{
		std::list<std::shared_ptr<Entry>> entries;
		
		if(isDirectory())
		{
			// read whole directory record to memory
			std::vector<uint8_t> buffer(read());

			for(uint32_t i = 0, n = (uint32_t)buffer.size(); i < n;)
			{
				iso9660::DirectoryRecord &dr = *(iso9660::DirectoryRecord *)&buffer[i];

				if(dr.length && dr.length <= FORM1_DATA_SIZE - i % FORM1_DATA_SIZE)
				{
					// (1) [01/12/2020]: "All Star Racing 2 (Europe) (Track 1).bin"
					// (2) [11/10/2020]: "All Star Racing 2 (USA) (Track 1).bin"
					// (3) [01/21/2020]: "Aitakute... - Your Smiles in My Heart - Oroshitate no Diary - Introduction Disc (Japan) (Track 1).bin"
					// (4) [01/21/2020]: "MLB 2005 (USA).bin"
					// all these tracks have messed up directory records, (1), (2) and (4) have garbage after valid entries, (3) has garbage before
					// good DirectoryRecord validity trick is to compare lsb to msb for offset and data_length and make sure it's the same
					if(!isValid(dr))
					{
#ifdef DIRECTORY_RECORD_WORKAROUNDS
						//FIXME:
						// 1) try to find legit entry after garbage, useful for (3)
//							++i;
//							continue;
						// 2) skip everything
						break;
#else
						throw_line("garbage in directory record");
#endif
					}

					char b1 = (char)buffer[i + sizeof(dr)];
					if(b1 != (char)iso9660::Characters::DIR_CURRENT && b1 != (char)iso9660::Characters::DIR_PARENT)
					{
						std::string identifier((const char *)& buffer[i + sizeof(dr)], dr.file_identifier_length);

						uint32_t version;
						std::string name = splitIdentifier(version, identifier);

						entries.push_back(std::make_shared<Entry>(_sectorReader, name, version, dr));
					}

					i += dr.length;
				}
				// skip sector boundary
				else
					i = ((i / FORM1_DATA_SIZE) + 1) * FORM1_DATA_SIZE;
			}
		}
			
		return entries;
	}

	
	std::shared_ptr<Entry> subEntry(const std::string &path)
	{
		std::shared_ptr<Entry> entry;
		
		auto components = tokenize(path, "/\\", nullptr);
		for(auto const &c : components)
		{
			uint32_t version;
			std::string name = str_uppercase(splitIdentifier(version, c));

			bool found = false;

			auto directories = entry ? entry->entries() : entries();
			for(auto &d : directories)
			{
				if(name == str_uppercase(d->name()) && (!version || version == d->version()))
				{
					entry = d;
					found = true;
					break;
				}
			}

			if(!found)
			{
				entry.reset();
				break;
			}
		}
			
		return entry;
	}


	std::vector<uint8_t> read()
	{
		std::vector<uint8_t> sectors(sectorSize());
		
		uint32_t sectors_read = _sectorReader->readLBA(sectors.data(), sectorOffset(), scale_up(_directoryRecord.data_length.lsb, FORM1_DATA_SIZE));
		sectors.resize(sectors_read * FORM1_DATA_SIZE);

		return sectors;
	}
	
private:
	SectorReader *_sectorReader;
	std::string _name;
	uint32_t _version;
	iso9660::DirectoryRecord _directoryRecord;
	
	bool isValid(const iso9660::DirectoryRecord &dr) const
	{
		return dr.offset.lsb == endian_swap(dr.offset.msb) && dr.data_length.lsb == endian_swap(dr.data_length.msb);
	}


	std::string splitIdentifier(uint32_t &version, std::string identifier)
	{
		auto s = identifier.find((char)iso9660::Characters::SEPARATOR2);

		version = (s == std::string::npos ? 0 : stoll_strict(identifier.substr(s + 1)));
		return identifier.substr(0, s);
	}
};

}
