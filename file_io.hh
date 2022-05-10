#pragma once



#include <cstdint>
#include <filesystem>
#include <fstream>
#include "toc.hh"



namespace gpsxre
{

void write_entry(std::fstream &fs, const uint8_t *data, uint32_t entry_size, uint32_t index, uint32_t count, int32_t byte_offset);
void read_entry(std::fstream &fs, uint8_t *data, uint32_t entry_size, uint32_t index, uint32_t count, int32_t byte_offset, uint8_t fill_byte);
void write_align(std::fstream &fs, uint32_t index, uint32_t entry_size, uint8_t fill_byte);
std::vector<uint8_t> read_vector(const std::filesystem::path &file_path);
void write_vector(const std::filesystem::path &file_path, const std::vector<uint8_t> &data);
uint32_t check_file(const std::filesystem::path &file_path, uint32_t entry_size);

}
