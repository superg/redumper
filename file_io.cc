#include <cmath>
#include <vector>
#include "cd.hh"
#include "common.hh"
#include "scsi.hh"
#include "file_io.hh"



namespace gpsxre
{

void write_entry(std::fstream &fs, const uint8_t *data, uint32_t entry_size, uint32_t index, uint32_t count, int32_t byte_offset)
{
	int32_t file_offset = index * entry_size - byte_offset;

	uint32_t total_size = entry_size * count;

	uint32_t size = total_size;
	if(file_offset <= -(int32_t)total_size)
		size = 0;
	else if(file_offset < 0)
	{
		size += file_offset;
		file_offset = 0;
	}

	if(size)
	{
		fs.seekp(file_offset);
		if(fs.fail())
			throw_line("seek failed");

		fs.write((char *)(data + total_size - size), size);
		if(fs.fail())
			throw_line("write failed");
	}
}


void read_entry(std::fstream &fs, uint8_t *data, uint32_t entry_size, uint32_t index, uint32_t count, int32_t byte_offset, uint8_t fill_byte)
{
	int32_t file_offset = index * entry_size - byte_offset;

	uint32_t total_size = entry_size * count;

	// head
	uint32_t size = total_size;
	if(file_offset <= -(int32_t)total_size)
		size = 0;
	else if(file_offset < 0)
	{
		size += file_offset;
		file_offset = 0;
	}
	uint32_t data_offset = total_size - size;

	// tail
	fs.seekg(0, std::fstream::end);
	if(fs.fail())
		throw_line("seek failed");

	uint32_t file_tail = std::max((int32_t)fs.tellg() - file_offset, 0);

	if(size > file_tail)
		size = file_tail;

	memset(data, fill_byte, total_size);

	if(size)
	{
		fs.seekg(file_offset);
		if(fs.fail())
			throw_line("seek failed");

		fs.read((char *)data + data_offset, size);
		if(fs.fail())
			throw_line("read failed");

	}
}


void write_align(std::fstream &fs, uint32_t index, uint32_t entry_size, uint8_t fill_byte)
{
	fs.seekp(0, std::fstream::end);
	if(fs.fail())
		throw_line("seek failed");

	auto file_size = fs.tellp();

	if(file_size < index * entry_size)
	{
		std::vector<uint8_t> pad(index * entry_size - file_size, fill_byte);
		fs.write((char *)pad.data(), pad.size());
		if(fs.fail())
			throw_line("write failed");
	}
}


std::vector<uint8_t> read_vector(const std::filesystem::path &file_path)
{
	std::vector<uint8_t> data(std::filesystem::file_size(file_path));

	std::fstream fs(file_path, std::fstream::in | std::fstream::binary);
	if(!fs.is_open())
		throw_line(std::format("unable to open file ({})", file_path.filename().string()));
	fs.read((char *)data.data(), data.size());
	if(fs.fail())
		throw_line(std::format("read failed ({})", file_path.filename().string()));

	return data;
}


void write_vector(const std::filesystem::path &file_path, const std::vector<uint8_t> &data)
{
	std::fstream fs(file_path, std::fstream::out | std::fstream::binary);
	if(!fs.is_open())
		throw_line(std::format("unable to create file ({})", file_path.filename().string()));
	fs.write((char *)data.data(), data.size());
	if(fs.fail())
		throw_line(std::format("write failed ({})", file_path.filename().string()));
}


uint32_t check_file(const std::filesystem::path &file_path, uint32_t entry_size)
{
	if(!std::filesystem::exists(file_path))
		throw_line(std::format("file doesn't exist ({})", file_path.filename().string()));

	if(!std::filesystem::is_regular_file(file_path))
		throw_line(std::format("not a regular file ({})", file_path.filename().string()));

	auto file_size = (uint32_t)std::filesystem::file_size(file_path);
	if(!file_size)
		throw_line(std::format("file is empty ({})", file_path.filename().string()));

	if(file_size % entry_size)
		throw_line(std::format("incomplete file or garbage in the end ({})", file_path.filename().string()));

	return file_size / entry_size;
}

}
