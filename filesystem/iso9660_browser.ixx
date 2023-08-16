module;
#include <cstdint>
//#include <filesystem>
//#include <fstream>
//#include <list>
//#include <memory>
//#include <queue>
//#include <string>
//#include <vector>
#include "throw_line.hh"

export module filesystem.iso9660_browser;

import cd.cd;
//import cd.cdrom;
//import cd.scrambler;
//import filesystem.iso9660;
import readers.block_reader;
//import utils.endian;
//import utils.misc;
//import utils.strings;



//#define DIRECTORY_RECORD_WORKAROUNDS



namespace gpsxre
{

export class ISO9660
{
public:
	static constexpr uint32_t SYSTEM_AREA_SIZE = 16;
	static constexpr uint8_t STANDARD_IDENTIFIER[] = "CD001";
	static constexpr uint8_t STANDARD_IDENTIFIER_CDI[] = "CD-I ";

	enum class VolumeDescriptorType : uint8_t
	{
		BOOT_RECORD = 0,
		PRIMARY = 1,
		SUPPLEMENTARY = 2,
		PARTITION = 3,
		RESERVED_4 = 4,
		RESERVED_254 = 254,
		SET_TERMINATOR = 255
	};

#pragma pack(push, 1)
	struct uint64_lsb_msb
	{
		uint32_t lsb;
		uint32_t msb;
	};
	
	struct uint32_lsb_msb
	{
		uint16_t lsb;
		uint16_t msb;
	};

	struct RecordingDateTime
	{
		uint8_t year;
		uint8_t month;
		uint8_t day;
		uint8_t hour;
		uint8_t minute;
		uint8_t second;
		uint8_t gmt_offset;
	};

	struct DirectoryRecord
	{
		enum class FileFlags : uint8_t
		{
			EXISTENCE = 1 << 0,
			DIRECTORY = 1 << 1,
			ASSOCIATED_FILE = 1 << 2,
			RECORD = 1 << 3,
			PROTECTION = 1 << 4,
			RESERVED1 = 1 << 5,
			RESERVED2 = 1 << 6,
			MULTI_EXTENT = 1 << 7
		};

		uint8_t length;
		uint8_t xa_length;
		uint64_lsb_msb offset;
		uint64_lsb_msb data_length;
		RecordingDateTime recording_date_time;
		uint8_t file_flags;
		uint8_t file_unit_size;
		uint8_t interleave_gap_size;
		uint32_t volume_sequence_number;
		uint8_t file_identifier_length;
	};

	struct DateTime
	{
		uint8_t year[4];
		uint8_t month[2];
		uint8_t day[2];
		uint8_t hour[2];
		uint8_t minute[2];
		uint8_t second[2];
		uint8_t centisecond[2];
		int8_t gmt_offset;
	};

	struct VolumeDescriptor
	{
		VolumeDescriptorType type;
		uint8_t standard_identifier[5];
		uint8_t version;
		uint8_t data[2041];
	};

	struct PrimaryVolumeDescriptor
	{
		VolumeDescriptorType type;
		uint8_t standard_identifier[5];
		uint8_t version;
		uint8_t unused1;
		char system_identifier[32];
		char volume_identifier[32];
		uint8_t unused2[8];
		uint64_lsb_msb volume_space_size;
		uint8_t unused3[32];
		uint32_lsb_msb volume_set_size;
		uint32_lsb_msb volume_sequence_number;
		uint32_lsb_msb logical_block_size;
		uint64_lsb_msb path_table_size;
		uint32_t type_l_path_table_offset;
		uint32_t optional_type_l_path_table_offset;
		uint32_t type_m_path_table_offset;
		uint32_t optional_type_m_path_table_offset;
		DirectoryRecord root_directory_record;
		uint8_t root_directory_identifier;
		char volume_set_identifier[128];
		char publisher_identifier[128];
		char data_preparer_identifier[128];
		char application_identifier[128];
		char copyright_file_identifier[37];
		char abstract_file_identifier[37];
		char bibliographic_file_identifier[37];
		DateTime volume_creation_date_time;
		DateTime volume_modification_date_time;
		DateTime volume_expiration_date_time;
		DateTime volume_effective_date_time;
		uint8_t file_structure_version;
		uint8_t reserved1;
		uint8_t application_use[512];
		uint8_t reserved2[653];
	};
#pragma pack(pop)

	class Browser
	{
	public:
		Browser()
		{
			;
		}

		static bool findDescriptor(VolumeDescriptor &descriptor, BlockReader32 *block_reader, VolumeDescriptorType type)
		{
			bool found = false;

			// find primary volume descriptor
			VolumeDescriptor *pvd = nullptr;
			for(uint32_t s = SYSTEM_AREA_SIZE; block_reader->read((uint8_t *)&descriptor, s, 1); ++s)
			{
				if(memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER, sizeof(descriptor.standard_identifier)) &&
				   memcmp(descriptor.standard_identifier, STANDARD_IDENTIFIER_CDI, sizeof(descriptor.standard_identifier)))
					break;

				if(descriptor.type == type)
				{
					found = true;
					break;
				}
				else if(descriptor.type == VolumeDescriptorType::SET_TERMINATOR)
					break;
			}

			return found;
		}

	private:
		;
	};
};

/*
export class ISO9660Browser
{
public:
	class Entry
	{
		friend class ISO9660Browser;

	public:
		bool IsDirectory() const
		{
			return _directory_record.file_flags & (uint8_t)iso9660::DirectoryRecord::FileFlags::DIRECTORY;
		}


		std::list<std::shared_ptr<Entry>> Entries()
		{
			std::list<std::shared_ptr<ISO9660Browser::Entry>> entries;
			
			if(IsDirectory())
			{
				// read whole directory record to memory
				std::vector<uint8_t> buffer(Read(false, true));

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
						if(!DirectoryRecordValid(dr))
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
							auto s = identifier.find((char)iso9660::Characters::SEPARATOR2);
							std::string name(s == std::string::npos ? identifier : identifier.substr(0, s));

							uint32_t version(s == std::string::npos ? 1 : stoll_strict(identifier.substr(s + 1)));

//							entries.push_back(std::make_shared<Entry>(name, version, dr, _ifs));
							entries.push_back(std::shared_ptr<Entry>(new Entry(_browser, name, version, dr)));
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


		std::shared_ptr<Entry> SubEntry(const std::string &path)
		{
			std::shared_ptr<Entry> entry;
			
			std::string p(path);

			// libc++ std::filesystem implementation doesn't iterate on linux if path delimiter is '\\'
			std::replace(p.begin(), p.end(), '\\', '/');

			std::filesystem::path path_case(p);

			for(auto const &p : path_case)
			{
				auto directories = entry ? entry->Entries() : Entries();
				bool found = false;
				for(auto &d : directories)
				{
					std::string name_case(str_uppercase(d->Name()));
					if(name_case == p || (name_case + ';' + std::to_string(d->Version())) == p)
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


		const std::string &Name() const
		{
			return _name;
		}


		uint32_t Version() const
		{
			return _version;
		}


		time_t DateTime() const
		{
			return convert_time(_directory_record.recording_date_time);
		}


		bool IsDummy() const
		{
			uint32_t offset = _directory_record.offset.lsb - _browser._trackLBA;
			return _browser._fileStartOffset + (offset + SectorSize()) * sizeof(Sector) >= _browser._fileEndOffset;
		}


		bool IsInterleaved() const
		{
			bool interleaved = false;
			
			Scrambler scrambler;

			static const uint32_t SECTORS_TO_ANALYZE = 8 * 4;

			uint32_t offset = _directory_record.offset.lsb - _browser._trackLBA;
			_browser._fs.seekg(_browser._fileStartOffset + offset * sizeof(Sector));

			if(_browser._fs.fail())
			{
				_browser._fs.clear();
				throw_line("seek failure");
			}

			uint8_t file_form = 0;

			uint32_t sectors_count = std::min(SectorSize(), SECTORS_TO_ANALYZE);
			for(uint32_t s = 0; s < sectors_count; ++s)
			{
				Sector sector;
				_browser._fs.read((char *)&sector, sizeof(sector));
				if(_browser._fs.fail())
				{
					_browser._fs.clear();
					throw_line("read failure ({})", std::strerror(errno));
				}

				if(_browser._scrambled)
					scrambler.process((uint8_t *)&sector, (uint8_t *)&sector, 0, CD_DATA_SIZE);

				uint8_t file_form_next = 0;
				if(sector.header.mode == 1)
					file_form_next = 1;
				else if(sector.header.mode == 2)
					file_form_next = sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2 ? 2 : 1;

				if(file_form)
				{
					if(file_form != file_form_next)
					{
						interleaved = true;
						break;
					}
				}
				else
					file_form = file_form_next;
			}
			
			return interleaved;
		}


		uint32_t SectorOffset() const
		{
			return _directory_record.offset.lsb;
		}
		
		
		uint32_t SectorSize() const
		{
			return _directory_record.data_length.lsb / FORM1_DATA_SIZE
				+ (_directory_record.data_length.lsb % FORM1_DATA_SIZE ? 1 : 0);
		}


		std::vector<uint8_t> Read(bool form2 = false, bool throw_on_error = false)
		{
			std::vector<uint8_t> data;
			
			Scrambler scrambler;

			uint32_t size = _directory_record.data_length.lsb;
			data.reserve(size);

			uint32_t offset = _directory_record.offset.lsb - _browser._trackLBA;
			_browser._fs.seekg(_browser._fileStartOffset + offset * sizeof(Sector));

			if(_browser._fs.fail())
			{
				_browser._fs.clear();
				if(throw_on_error)
					throw_line("seek failure");
			}
			else
			{
				uint32_t sectors_count = SectorSize();
				for(uint32_t s = 0; s < sectors_count; ++s)
				{
					Sector sector;
					_browser._fs.read((char *)&sector, sizeof(sector));
					if(_browser._fs.fail())
					{
						_browser._fs.clear();
						if(throw_on_error)
							throw_line("read failure ({})", std::strerror(errno));

						break;
					}

					if(_browser._scrambled)
						scrambler.process((uint8_t *)&sector, (uint8_t *)&sector, 0, CD_DATA_SIZE);

					uint8_t *user_data;
					uint32_t bytes_to_copy;
					if(sector.header.mode == 1)
					{
						if(form2)
							continue;

						user_data = sector.mode1.user_data;
						bytes_to_copy = std::min(FORM1_DATA_SIZE, size);
					}
					else if(sector.header.mode == 2)
					{
						if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
						{
							if(!form2)
								continue;

							user_data = sector.mode2.xa.form2.user_data;
							bytes_to_copy = size < FORM1_DATA_SIZE ? size : FORM2_DATA_SIZE;
						}
						else
						{
							if(form2)
								continue;

							user_data = sector.mode2.xa.form1.user_data;
							bytes_to_copy = std::min(FORM1_DATA_SIZE, size);
						}
					}
					else
						continue;

					data.insert(data.end(), user_data, user_data + bytes_to_copy);

					size -= std::min(FORM1_DATA_SIZE, size);
				}
			}
			
			return data;
		}

	private:
		ISO9660Browser &_browser;
		std::string _name;
		uint32_t _version;
		iso9660::DirectoryRecord _directory_record;
		
		Entry(ISO9660Browser &browser, const std::string &name, uint32_t version, const iso9660::DirectoryRecord &directory_record)
			: _browser(browser)
			, _name(name)
			, _version(version)
			, _directory_record(directory_record)
		{
			;
		}
		
		bool DirectoryRecordValid(const iso9660::DirectoryRecord &dr) const
		{
			return dr.offset.lsb == endian_swap(dr.offset.msb) && dr.data_length.lsb == endian_swap(dr.data_length.msb);
		}
	};

	//FIXME: reorganize so no code duplication in ISO9660Browser::ISO9660Browser()
	static bool IsDataTrack(const std::filesystem::path &track)
	{
		std::fstream fs(track, std::fstream::in | std::fstream::binary);

		if(fs.fail())
			return false;

		uint64_t size = std::filesystem::file_size(track);

		if(size % sizeof(Sector))
			return false;

		if(size < ((uint64_t)iso9660::SYSTEM_AREA_SIZE + 1) * sizeof(Sector))
			return false;

		// skip system area
		fs.seekg(iso9660::SYSTEM_AREA_SIZE * sizeof(Sector));
		if(fs.fail())
			return false;

		// find primary volume descriptor
		iso9660::VolumeDescriptor *pvd = nullptr;
		for(;;)
		{
			Sector sector;
			fs.read((char *)&sector, sizeof(sector));
			if(fs.fail())
				break;

			iso9660::VolumeDescriptor *vd;
			switch(sector.header.mode)
			{
			case 1:
				vd = (iso9660::VolumeDescriptor *)sector.mode1.user_data;
				break;

			case 2:
				vd = (iso9660::VolumeDescriptor *)sector.mode2.xa.form1.user_data;
				break;

			default:
				continue;
			}

			if(memcmp(vd->standard_identifier, iso9660::STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)) &&
			   memcmp(vd->standard_identifier, iso9660::CDI_STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)))
				break;

			if(vd->type == iso9660::VolumeDescriptor::Type::PRIMARY)
			{
				pvd = vd;
				break;
			}
			else if(vd->type == iso9660::VolumeDescriptor::Type::SET_TERMINATOR)
				break;
		}

		return pvd != nullptr;
	}


	ISO9660Browser(const std::filesystem::path &data_track, uint64_t file_start_offset, uint64_t file_end_offset, bool scrambled)
		: _fsProxy(data_track, std::fstream::in | std::fstream::binary)
		, _fs(_fsProxy)
		, _fileStartOffset(file_start_offset)
		, _fileEndOffset(file_end_offset)
		, _scrambled(scrambled)
	{
		Init();
	}


	ISO9660Browser(std::fstream &fs, uint64_t file_start_offset, uint64_t file_end_offset, bool scrambled)
		: _fs(fs)
		, _fileStartOffset(file_start_offset)
		, _fileEndOffset(file_end_offset)
		, _scrambled(scrambled)
	{
		Init();
	}


	std::shared_ptr<Entry> RootDirectory()
	{
		return std::shared_ptr<Entry>(new Entry(*this, std::string(""), 1, _pvd.primary.root_directory_record));
	}

	
	const iso9660::VolumeDescriptor &GetPVD() const
	{
		return _pvd;
	}
	

	std::vector<uint8_t> getSystemArea()
	{
		return read(_trackLBA, iso9660::SYSTEM_AREA_SIZE);
	}


	template<typename F>
	bool Iterate(F f)
	{
		bool interrupted = false;
		
		std::queue<std::pair<std::string, std::shared_ptr<Entry>>> q;
		q.push(std::pair<std::string, std::shared_ptr<Entry>>(std::string(""), RootDirectory()));

		while(!q.empty())
		{
			auto p = q.front();
			q.pop();

			if(p.second->IsDirectory())
				for(auto &dd : p.second->Entries())
					q.push(std::pair<std::string, std::shared_ptr<Entry>>(dd->IsDirectory() ? (p.first.empty() ? "" : p.first + "/") + dd->Name() : p.first, dd));
			else
			{
				if(f(p.first, p.second))
				{
					interrupted = true;
					break;
				}
			}
		}
		
		return interrupted;
	}

private:
	std::fstream _fsProxy;
	std::fstream &_fs;
	uint64_t _fileStartOffset;
	uint64_t _fileEndOffset;
	bool _scrambled;
	iso9660::VolumeDescriptor _pvd;
	uint32_t _trackLBA;
	
	void Init()
	{
		if(_fs.fail())
			throw_line("unable to open file ({})", std::strerror(errno));

		Sector sector;
		Scrambler scrambler;

		// calculate data track sector offset
		_fs.seekg(_fileStartOffset);
		if(_fs.fail())
			throw_line("seek failure");

		_fs.read((char *)&sector, sizeof(sector));
		if(_fs.fail())
			throw_line("read failure");

		if(_scrambled)
			scrambler.process((uint8_t *)&sector, (uint8_t *)&sector, 0, CD_DATA_SIZE);
		_trackLBA = BCDMSF_to_LBA(sector.header.address);

		// skip system area
		_fs.seekg(_fileStartOffset + iso9660::SYSTEM_AREA_SIZE * sizeof(Sector));
		if(_fs.fail())
			throw_line("seek failure");

		// find primary volume descriptor
		iso9660::VolumeDescriptor *pvd = nullptr;
		for(;;)
		{
			Sector sector;
			_fs.read((char *)&sector, sizeof(sector));
			if(_fs.fail())
			{
				_fs.clear();
				break;
			}

			if(_scrambled)
				scrambler.process((uint8_t *)&sector, (uint8_t *)&sector, 0, CD_DATA_SIZE);

			iso9660::VolumeDescriptor *vd;
			switch(sector.header.mode)
			{
			case 1:
				vd = (iso9660::VolumeDescriptor *)sector.mode1.user_data;
				break;

			case 2:
				vd = (iso9660::VolumeDescriptor *)sector.mode2.xa.form1.user_data;
				break;

			default:
				continue;
			}

			if(memcmp(vd->standard_identifier, iso9660::STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)) &&
			   memcmp(vd->standard_identifier, iso9660::CDI_STANDARD_IDENTIFIER, sizeof(vd->standard_identifier)))
				break;

			if(vd->type == iso9660::VolumeDescriptor::Type::PRIMARY)
			{
				pvd = vd;
				break;
			}
			else if(vd->type == iso9660::VolumeDescriptor::Type::SET_TERMINATOR)
				break;
		}

		if(pvd == nullptr)
			throw_line("primary volume descriptor not found");

		_pvd = *pvd;

		// can't use _pvd.primary.volume_space_size.lsb here
		// for some PSX discs where dummy files are present
		// this value includes all following audio tracks
		if(!_fileEndOffset)
			_fileEndOffset = _fileStartOffset + _pvd.primary.volume_space_size.lsb * sizeof(Sector);
	}


	std::vector<uint8_t> read(uint32_t lba, uint32_t count)
	{
		std::vector<uint8_t> data;
		data.reserve(count * FORM1_DATA_SIZE);

		Scrambler scrambler;

		uint32_t offset = lba - _trackLBA;
		_fs.seekg(_fileStartOffset + offset * sizeof(Sector));
		if(_fs.fail())
		{
			_fs.clear();
			throw_line("seek failure");
		}

		for(uint32_t s = 0; s < count; ++s)
		{
			Sector sector;
			_fs.read((char *)&sector, sizeof(sector));
			if(_fs.fail())
			{
				_fs.clear();
				throw_line("read failure ({})", std::strerror(errno));
			}

			if(_scrambled)
				scrambler.process((uint8_t *)&sector, (uint8_t *)&sector, 0, CD_DATA_SIZE);

			uint8_t *user_data;
			uint32_t bytes_to_copy;
			if(sector.header.mode == 1)
			{
				user_data = sector.mode1.user_data;
				bytes_to_copy = FORM1_DATA_SIZE;
			}
			else if(sector.header.mode == 2)
			{
				if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
				{
					user_data = sector.mode2.xa.form2.user_data;
					bytes_to_copy = FORM2_DATA_SIZE;
				}
				else
				{
					user_data = sector.mode2.xa.form1.user_data;
					bytes_to_copy = FORM1_DATA_SIZE;
				}
			}
			else
				throw_line("unexpected sector mode (mode: {})", sector.header.mode);

			data.insert(data.end(), user_data, user_data + bytes_to_copy);
		}

		return data;
	}
};
*/
}
