module;

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "lzma/Alloc.h"
#include "lzma/LzmaEnc.h"
#include "throw_line.hh"

export module skeleton;

import cd.cd;
import cd.cdrom;
import dump;
import filesystem.iso9660;
import options;
import readers.sector_reader;
import readers.image_bin_form1_reader;
import readers.image_iso_form1_reader;
import utils.animation;
import utils.logger;
import utils.misc;



namespace gpsxre
{

typedef std::tuple<std::string, uint32_t, uint32_t, uint32_t> ContentEntry;


void progress_output(std::string name, uint64_t value, uint64_t value_count)
{
	char animation = value == value_count ? '*' : spinner_animation();

	LOGC_RF("{} [{:3}%] {}", animation, value * 100 / value_count, name);
}


struct MemorySeqInStream
{
	ISeqInStream stream;

	std::fstream _imageFS;
	const std::vector<ContentEntry> &_contents;
	bool _iso;

	uint64_t _sectorSize;
	uint64_t _sectorsCount;
	uint64_t _currentSector;

	std::vector<uint8_t> _tail;
	uint64_t _tailSize;

	MemorySeqInStream(const std::filesystem::path &image_path, const std::vector<ContentEntry> &contents, bool iso)
	    : _contents(contents)
	    , _iso(iso)
	    , _sectorSize(_iso ? FORM1_DATA_SIZE : CD_DATA_SIZE)
	    , _sectorsCount(std::filesystem::file_size(image_path) / _sectorSize)
	    , _currentSector(0)
	    , _tail(_sectorSize)
	    , _tailSize(0)
	{
		stream.Read = &readC;

		_imageFS.open(image_path, std::fstream::in | std::fstream::binary);
		if(!_imageFS.is_open())
			throw_line("unable to open file ({})", image_path.filename().string());
	}

	static SRes readC(ISeqInStreamPtr p, void *buf, size_t *size)
	{
		auto stream = (MemorySeqInStream *)p;
		return stream->read((uint8_t *)buf, size);
	}

	SRes read(uint8_t *buf, size_t *size)
	{
		progress_output("creating skeleton", _currentSector, _sectorsCount);

		// old tail
		if(_tailSize)
		{
			*size = std::min((size_t)_tailSize, *size);
			memcpy(buf, _tail.data(), *size);
			_tailSize -= *size;
		}
		else
		{
			if(_currentSector == _sectorsCount)
			{
				*size = 0;
			}
			else
			{
				// new tail
				if(*size < _sectorSize)
				{
					_imageFS.read((char *)_tail.data(), _sectorSize);
					if(_imageFS.fail())
						return SZ_ERROR_READ;

					if(insideContents((uint32_t)_currentSector))
						eraseSector(_tail.data());

					memcpy(buf, _tail.data(), *size);
					std::copy(_tail.begin() + *size, _tail.end(), _tail.begin());
					_tailSize = _sectorSize - *size;

					++_currentSector;
				}
				// body
				else
				{
					uint64_t sectors_to_process = std::min(*size / _sectorSize, _sectorsCount - _currentSector);
					*size = sectors_to_process * _sectorSize;

					_imageFS.read((char *)buf, *size);
					if(_imageFS.fail())
						return SZ_ERROR_READ;

					for(uint64_t i = 0; i < sectors_to_process; ++i)
					{
						if(insideContents((uint32_t)(_currentSector + i)))
							eraseSector(buf + i * _sectorSize);
					}

					_currentSector += sectors_to_process;
				}
			}
		}

		return SZ_OK;
	}

	bool insideContents(uint32_t value)
	{
		for(auto const &c : _contents)
			if(value >= std::get<1>(c) && value < std::get<1>(c) + std::get<2>(c))
				return true;

		return false;
	}

	void eraseSector(uint8_t *s)
	{
		if(_iso)
			memset(s, 0x00, FORM1_DATA_SIZE);
		else
		{
			auto sector = (Sector *)s;

			if(sector->header.mode == 1)
			{
				memset(sector->mode1.user_data, 0x00, FORM1_DATA_SIZE);
				memset(&sector->mode1.ecc, 0x00, sizeof(Sector::ECC));
				sector->mode1.edc = 0;
			}
			else if(sector->header.mode == 2)
			{
				if(sector->mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
				{
					memset(sector->mode2.xa.form2.user_data, 0x00, FORM2_DATA_SIZE);
					sector->mode2.xa.form2.edc = 0;
				}
				else
				{
					memset(sector->mode2.xa.form1.user_data, 0x00, FORM1_DATA_SIZE);
					memset(&sector->mode2.xa.form1.ecc, 0x00, sizeof(Sector::ECC));
					sector->mode2.xa.form1.edc = 0;
				}
			}
		}
	}
};

struct FileSeqOutStream
{
	ISeqOutStream stream;

	std::fstream ofs;

	FileSeqOutStream(const std::filesystem::path &file_path)
	{
		stream.Write = &write;

		ofs.open(file_path, std::fstream::out | std::fstream::binary);
		if(!ofs.is_open())
			throw_line("unable to create file ({})", file_path.filename().string());
	}

	static size_t write(ISeqOutStreamPtr p, const void *buf, size_t size)
	{
		auto stream = (FileSeqOutStream *)p;

		stream->ofs.write((char *)buf, size);

		return stream->ofs ? size : 0;
	}
};


void skeleton(const std::string &image_prefix, const std::string &image_path, bool iso, Options &options)
{
	std::filesystem::path skeleton_path(image_prefix + ".skeleton");
	std::filesystem::path hash_path(image_prefix + ".hash");

	if(!options.overwrite && (std::filesystem::exists(skeleton_path) || std::filesystem::exists(hash_path)))
		throw_line("skeleton/index file already exists");

	std::unique_ptr<SectorReader> sector_reader;
	if(iso)
		sector_reader = std::make_unique<Image_ISO_Reader>(image_path);
	else
		sector_reader = std::make_unique<Image_BIN_Form1Reader>(image_path);

	uint32_t sectors_count = std::filesystem::file_size(image_path) / (iso ? FORM1_DATA_SIZE : CD_DATA_SIZE);

	auto area_map = iso9660::area_map(sector_reader.get(), 0, sectors_count);
	if(area_map.empty())
		return;

	if(options.debug)
	{
		LOG("ISO9660 map: ");
		std::for_each(area_map.cbegin(), area_map.cend(),
		    [](const iso9660::Area &area)
		    {
			    auto count = scale_up(area.size, FORM1_DATA_SIZE);
			    LOG("LBA: [{:6} .. {:6}], count: {:6}, type: {}{}", area.offset, area.offset + count - 1, count, enum_to_string(area.type, iso9660::AREA_TYPE_STRING),
			        area.name.empty() ? "" : std::format(", name: {}", area.name));
		    });
	}

	std::vector<ContentEntry> contents;
	for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
	{
		auto const &a = area_map[i];

		std::string name(a.name.empty() ? enum_to_string(a.type, iso9660::AREA_TYPE_STRING) : a.name);

		if(a.type == iso9660::Area::Type::SYSTEM_AREA || a.type == iso9660::Area::Type::FILE_EXTENT)
			contents.emplace_back(name, a.offset, scale_up(a.size, sector_reader->sectorSize()), a.size);

		uint32_t gap_start = a.offset + scale_up(a.size, sector_reader->sectorSize());
		if(gap_start < area_map[i + 1].offset)
		{
			uint32_t gap_size = area_map[i + 1].offset - gap_start;

			// 5% or more in relation to the total filesystem size
			if((uint64_t)gap_size * 100 / sectors_count > 5)
				contents.emplace_back(std::format("GAP_{:07}", gap_start), gap_start, gap_size, gap_size * sector_reader->sectorSize());
		}
	}

	uint64_t contents_sectors_count = 0;
	for(auto const &c : contents)
		contents_sectors_count += std::get<2>(c);

	std::fstream hash_fs(hash_path, std::fstream::out);
	if(!hash_fs.is_open())
		throw_line("unable to create file ({})", hash_path.filename().string());

	uint32_t contents_sectors_processed = 0;
	for(auto const &c : contents)
	{
		progress_output(std::format("hashing {}", std::get<0>(c)), contents_sectors_processed, contents_sectors_count);

		bool xa = false;
		hash_fs << std::format("{} {}", sector_reader->calculateSHA1(std::get<1>(c), std::get<2>(c), std::get<3>(c), false, &xa), std::get<0>(c)) << std::endl;

		if(xa)
			hash_fs << std::format("{} {}.XA", sector_reader->calculateSHA1(std::get<1>(c), std::get<2>(c), std::get<3>(c), true), std::get<0>(c)) << std::endl;

		contents_sectors_processed += std::get<2>(c);
	}
	progress_output("hashing complete", contents_sectors_processed, contents_sectors_count);
	LOGC("");

	CLzmaEncHandle enc = LzmaEnc_Create(&g_Alloc);
	if(enc == nullptr)
		throw_line("failed to create LZMA encoder");

	CLzmaEncProps props;
	LzmaEncProps_Init(&props);
	if(LzmaEnc_SetProps(enc, &props) != SZ_OK)
		throw_line("failed to set LZMA properties");

	MemorySeqInStream msis(image_path, contents, iso);
	FileSeqOutStream fsos(skeleton_path);

	// store props
	Byte header[LZMA_PROPS_SIZE];
	size_t props_size = LZMA_PROPS_SIZE;
	if(LzmaEnc_WriteProperties(enc, header, &props_size) != SZ_OK)
		throw_line("failed to write LZMA properties");
	if(fsos.stream.Write(&fsos.stream, header, props_size) != props_size)
		throw_line("failed to store LZMA properties");

	uint64_t decompressed_size = std::filesystem::file_size(image_path);
	if(fsos.stream.Write(&fsos.stream, &decompressed_size, sizeof(decompressed_size)) != sizeof(decompressed_size))
		throw_line("failed to store decompressed size");

	if(LzmaEnc_Encode(enc, &fsos.stream, &msis.stream, nullptr, &g_Alloc, &g_Alloc) != SZ_OK)
		throw_line("failed to encode LZMA stream");

	LzmaEnc_Destroy(enc, &g_Alloc, &g_Alloc);

	LOGC("");
}


export void redumper_skeleton(Context &ctx, Options &options)
{
	image_check_empty(options);

	auto image_prefix = (std::filesystem::path(options.image_path) / options.image_name).string();

	if(std::filesystem::exists(image_prefix + ".cue"))
	{
		for(auto const &t : cue_get_entries(image_prefix + ".cue"))
		{
			// skip audio tracks
			if(!t.second)
				continue;

			auto track_prefix = (std::filesystem::path(options.image_path) / std::filesystem::path(t.first).stem()).string();

			skeleton(track_prefix, (std::filesystem::path(options.image_path) / t.first).string(), false, options);
		}
	}
	else if(std::filesystem::exists(image_prefix + ".iso"))
	{
		skeleton(image_prefix, image_prefix + ".iso", true, options);
	}
	else
		throw_line("image file not found");
}

}
