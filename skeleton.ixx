module;

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <string>
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

struct MemorySeqInStream
{
	ISeqInStream stream;

	std::fstream _imageFS;
	std::string _imageName;
	const std::vector<std::pair<uint32_t, uint32_t>> &_files;
	bool _iso;

	uint64_t _sectorSize;
	uint64_t _sectorsCount;
	uint64_t _currentSector;

	std::vector<uint8_t> _tail;
	uint64_t _tailSize;

	MemorySeqInStream(const std::filesystem::path &image_path, const std::vector<std::pair<uint32_t, uint32_t>> &files, bool iso)
		: _files(files)
		, _iso(iso)
		, _sectorSize(_iso ? FORM1_DATA_SIZE : CD_DATA_SIZE)
		, _sectorsCount(std::filesystem::file_size(image_path) / _sectorSize)
		, _currentSector(0)
		, _tail(_sectorSize)
		, _tailSize(0)
		, _imageName(image_path.filename().string())
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
		progressOutput(_currentSector, _sectorsCount);

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

					if(inside_range((uint32_t)_currentSector, _files) != nullptr)
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
						if(inside_range((uint32_t)(_currentSector + i), _files) != nullptr)
							eraseSector(buf + i * _sectorSize);
					}

					_currentSector += sectors_to_process;
				}
			}
		}

		return SZ_OK;
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

	void progressOutput(uint64_t sector, uint64_t sectors_count)
	{
		char animation = sector == sectors_count ? '*' : spinner_animation();

		LOGC_RF("{} [{:3}%] {}", animation, sector * 100 / sectors_count, _imageName);
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

	if(!options.overwrite && (std::filesystem::exists(skeleton_path)))
		throw_line("skeleton/index file already exists");

	std::vector<std::pair<uint32_t, uint32_t>> files;

	std::unique_ptr<SectorReader> sector_reader;
	if(iso)
		sector_reader = std::make_unique<Image_ISO_Reader>(image_path);
	else
		sector_reader = std::make_unique<Image_BIN_Form1Reader>(image_path);

	// can't use pvd.volume_space_size here because there might be more than one extent (UDF specific?) [Gran Turismo 4]
	uint32_t sectors_count = std::filesystem::file_size(image_path) / (iso ? FORM1_DATA_SIZE : CD_DATA_SIZE);

	auto area_map = iso9660::area_map(sector_reader.get(), 0, sectors_count);
	if(area_map.empty())
		return;

	area_map.emplace_back(iso9660::Area{ sectors_count, iso9660::Area::Type::SYSTEM_AREA, 0, "" });

	LOG("excluded areas hashes (SHA-1):");
	for(uint32_t i = 0; i + 1 < area_map.size(); ++i)
	{
		auto const &a = area_map[i];

		std::string name(a.name.empty() ? enum_to_string(a.type, iso9660::AREA_TYPE_STRING) : a.name);

		if(a.type == iso9660::Area::Type::SYSTEM_AREA || a.type == iso9660::Area::Type::FILE_EXTENT)
		{
			uint32_t sector_size = scale_up(a.size, sector_reader->sectorSize());

			bool xa = false;
			LOG("{} {}", sector_reader->calculateSHA1(a.offset, sector_size, a.size, false, &xa), name);

			if(xa)
				LOG("{} {}.XA", sector_reader->calculateSHA1(a.offset, sector_size, a.size, true), name);

			files.emplace_back(a.offset, a.offset + sector_size);
		}

		uint32_t gap_start = a.offset + scale_up(a.size, sector_reader->sectorSize());
		if(gap_start <= area_map[i + 1].offset)
		{
			uint32_t gap_size = area_map[i + 1].offset - gap_start;

			// 5% or more in relation to the total filesystem size
			if((uint64_t)gap_size * 100 / sectors_count > 5)
			{
				bool xa = false;
				LOG("{} GAP_{:07}", sector_reader->calculateSHA1(gap_start, gap_size, gap_size * sector_reader->sectorSize(), false, &xa), gap_start);

				if(xa)
					LOG("{} GAP_{:07}.XA", sector_reader->calculateSHA1(gap_start, gap_size, gap_size * sector_reader->sectorSize(), true), gap_start);

				files.emplace_back(gap_start, gap_start + gap_size);
			}
		}
	}
	LOG("");

	CLzmaEncHandle enc = LzmaEnc_Create(&g_Alloc);
	if(enc == nullptr)
		throw_line("failed to create LZMA encoder");

	CLzmaEncProps props;
	LzmaEncProps_Init(&props);
	if(LzmaEnc_SetProps(enc, &props) != SZ_OK)
		throw_line("failed to set LZMA properties");

	MemorySeqInStream msis(image_path, files, iso);
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

	LOG("");
}


export void redumper_skeleton(Context &ctx, Options &options)
{
	if(options.image_name.empty())
		throw_line("image name is not provided");

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
