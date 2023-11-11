module;
#include <filesystem>
#include <fstream>
#include <vector>
#include "throw_line.hh"

export module utils.file_io;



namespace gpsxre
{

export void write_entry(std::fstream &fs, const uint8_t *data, uint64_t entry_size, uint64_t index, uint64_t count, int64_t byte_offset)
{
	int64_t file_offset = index * entry_size - byte_offset;

	uint64_t total_size = entry_size * count;

	uint64_t size = total_size;
	if(file_offset <= -(int64_t)total_size)
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
		fs << std::flush;
	}
}


export void read_entry(std::fstream &fs, uint8_t *data, uint64_t entry_size, uint64_t index, uint64_t count, int64_t byte_offset, uint8_t fill_byte)
{
	int64_t file_offset = index * entry_size - byte_offset;

	uint64_t total_size = entry_size * count;

	// head
	uint64_t size = total_size;
	if(file_offset <= -(int64_t)total_size)
		size = 0;
	else if(file_offset < 0)
	{
		size += file_offset;
		file_offset = 0;
	}
	uint64_t data_offset = total_size - size;

	// tail
	fs.seekg(0, std::fstream::end);
	if(fs.fail())
		throw_line("seek failed");

	uint64_t file_tail = std::max((int64_t)fs.tellg() - file_offset, (int64_t)0);

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


export void write_align(std::fstream &fs, uint64_t index, uint64_t entry_size, uint8_t fill_byte)
{
	fs.seekp(0, std::fstream::end);
	if(fs.fail())
		throw_line("seek failed");

	auto file_size = fs.tellp();

	if(file_size < index * entry_size)
	{
		std::vector<uint8_t> pad((std::vector<uint8_t>::size_type)(index * entry_size - file_size), fill_byte);
		fs.write((char *)pad.data(), pad.size());
		if(fs.fail())
			throw_line("write failed");
	}
}


export std::vector<uint8_t> read_vector(const std::filesystem::path &file_path)
{
	std::vector<uint8_t> data((std::vector<uint8_t>::size_type)std::filesystem::file_size(file_path));

	std::fstream fs(file_path, std::fstream::in | std::fstream::binary);
	if(!fs.is_open())
		throw_line("unable to open file ({})", file_path.filename().string());

	fs.read((char *)data.data(), data.size());
	if(fs.fail())
		throw_line("read failed ({})", file_path.filename().string());

	return data;
}


export void write_vector(const std::filesystem::path &file_path, const std::vector<uint8_t> &data)
{
	std::fstream fs(file_path, std::fstream::out | std::fstream::binary);
	if(!fs.is_open())
		throw_line("unable to create file ({})", file_path.filename().string());
	fs.write((char *)data.data(), data.size());
	if(fs.fail())
		throw_line("write failed ({})", file_path.filename().string());
}


export uint64_t check_file(const std::filesystem::path &file_path, uint64_t entry_size)
{
	if(!std::filesystem::exists(file_path))
		throw_line("file doesn't exist ({})", file_path.filename().string());

	if(!std::filesystem::is_regular_file(file_path))
		throw_line("not a regular file ({})", file_path.filename().string());

	auto file_size = (uint64_t)std::filesystem::file_size(file_path);
	if(!file_size)
		throw_line("file is empty ({})", file_path.filename().string());

	//TODO: improve unaligned handling
//	if(file_size % entry_size)
//		throw_line("incomplete file or garbage in the end ({})", file_path.filename().string());

	return file_size / entry_size;
}

}
