module;
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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


// entry sizes are bytes, positive offsets move data earlier, and bits are ordered MSB first
export void write_entry_bit(std::fstream &fs, const uint8_t *data, uint64_t entry_size, uint64_t index, uint64_t count, int64_t bit_offset, bool fill_bit)
{
    constexpr int BYTE_BITS = CHAR_BIT;
    constexpr int NIBBLE_BITS = BYTE_BITS / 2;

    if(bit_offset % NIBBLE_BITS)
        throw_line("bit offset must be divisible by {}", NIBBLE_BITS);
    if(!entry_size || !count)
        return;

    constexpr uint64_t MAX_POSITION = std::min((uint64_t)INT64_MAX, (uint64_t)std::numeric_limits<std::streamoff>::max());
    constexpr uint64_t MAX_SIZE = std::min({ MAX_POSITION, (uint64_t)std::numeric_limits<std::streamsize>::max(), (uint64_t)std::numeric_limits<size_t>::max() });
    bool half_byte = bit_offset % BYTE_BITS != 0;
    int64_t byte_offset = bit_offset / BYTE_BITS + (bit_offset % BYTE_BITS > 0);
    if(entry_size > (MAX_SIZE - half_byte) / count || (index && entry_size > MAX_POSITION / index))
        throw_line("entry range is too large");
    uint64_t total_size = entry_size * count;
    if(byte_offset < 0 && (uint64_t)-byte_offset > MAX_POSITION - index * entry_size)
        throw_line("entry position is too large");
    int64_t file_offset = (int64_t)(index * entry_size) - byte_offset;
    uint64_t size = total_size + half_byte;
    if(file_offset > 0 && size > MAX_POSITION - file_offset)
        throw_line("entry range is too large");
    uint64_t skip = file_offset < 0 ? std::min((uint64_t)-file_offset, size) : 0;
    if(skip == size)
        return;

    uint8_t fill_byte = fill_bit ? 0xFF : 0;
    std::vector<uint8_t> buffer;
    if(half_byte)
    {
        buffer.resize(size);
        buffer[0] = (fill_byte & 0xF0) | (data[0] >> NIBBLE_BITS);
        for(uint64_t i = 1; i < total_size; ++i)
            buffer[i] = (data[i - 1] << NIBBLE_BITS) | (data[i] >> NIBBLE_BITS);
        buffer[total_size] = (data[total_size - 1] << NIBBLE_BITS) | (fill_byte & 0x0F);
    }

    fs.seekp(0, std::ios::end);
    int64_t file_size = fs.tellp();
    if(fs.fail() || file_size < 0)
        throw_line("seek failed");

    if(half_byte)
    {
        // preserve only the neighboring nibbles that already exist
        for(uint64_t boundary : { (uint64_t)0, total_size })
        {
            int64_t position = file_offset + (int64_t)boundary;
            if(position < 0 || position >= file_size)
                continue;
            fs.seekg(position);
            if(fs.fail())
                throw_line("seek failed");
            char value;
            fs.get(value);
            if(fs.fail())
                throw_line("read failed");
            uint8_t mask = boundary ? 0x0F : 0xF0;
            buffer[boundary] = (buffer[boundary] & ~mask) | ((uint8_t)value & mask);
        }
        data = buffer.data();
    }

    if(file_offset > file_size)
    {
        fs.seekp(file_size);
        if(fs.fail())
            throw_line("seek failed");
        for(int64_t position = file_size; position < file_offset && fs; ++position)
            fs.put((char)fill_byte);
        if(fs.fail())
            throw_line("write failed");
    }

    fs.seekp(std::max(file_offset, (int64_t)0));
    if(fs.fail())
        throw_line("seek failed");
    fs.write((const char *)(data + skip), size - skip);
    fs.flush();
    if(fs.fail())
        throw_line("write failed");
}


// missing bits before the file or beyond EOF use fill_bit
export void read_entry_bit(std::fstream &fs, uint8_t *data, uint64_t entry_size, uint64_t index, uint64_t count, int64_t bit_offset, bool fill_bit)
{
    constexpr int BYTE_BITS = CHAR_BIT;
    constexpr int NIBBLE_BITS = BYTE_BITS / 2;

    if(bit_offset % NIBBLE_BITS)
        throw_line("bit offset must be divisible by {}", NIBBLE_BITS);
    if(!entry_size || !count)
        return;

    constexpr uint64_t MAX_POSITION = std::min((uint64_t)INT64_MAX, (uint64_t)std::numeric_limits<std::streamoff>::max());
    constexpr uint64_t MAX_SIZE = std::min({ MAX_POSITION, (uint64_t)std::numeric_limits<std::streamsize>::max(), (uint64_t)std::numeric_limits<size_t>::max() });
    bool half_byte = bit_offset % BYTE_BITS != 0;
    int64_t byte_offset = bit_offset / BYTE_BITS + (bit_offset % BYTE_BITS > 0);
    if(entry_size > (MAX_SIZE - half_byte) / count || (index && entry_size > MAX_POSITION / index))
        throw_line("entry range is too large");
    uint64_t total_size = entry_size * count;
    if(byte_offset < 0 && (uint64_t)-byte_offset > MAX_POSITION - index * entry_size)
        throw_line("entry position is too large");
    int64_t file_offset = (int64_t)(index * entry_size) - byte_offset;
    uint64_t size = total_size + half_byte;
    if(file_offset > 0 && size > MAX_POSITION - file_offset)
        throw_line("entry range is too large");
    uint64_t skip = file_offset < 0 ? std::min((uint64_t)-file_offset, size) : 0;
    file_offset = std::max(file_offset, (int64_t)0);

    std::vector<uint8_t> buffer;
    if(half_byte)
        buffer.resize(size);
    uint8_t *target = half_byte ? buffer.data() : data;
    memset(target, fill_bit ? 0xFF : 0, size);

    fs.seekg(0, std::ios::end);
    int64_t file_size = fs.tellg();
    if(fs.fail() || file_size < 0)
        throw_line("seek failed");
    uint64_t read_size = std::min(size - skip, (uint64_t)std::max(file_size - file_offset, (int64_t)0));
    if(read_size)
    {
        fs.seekg(file_offset);
        if(fs.fail())
            throw_line("seek failed");
        fs.read((char *)(target + skip), read_size);
        if(fs.fail())
            throw_line("read failed");
    }

    if(half_byte)
        for(uint64_t i = 0; i < total_size; ++i)
            data[i] = (buffer[i] << NIBBLE_BITS) | (buffer[i + 1] >> NIBBLE_BITS);
}


export void write_align(std::fstream &fs, uint64_t index, uint64_t entry_size, uint8_t fill_byte)
{
    fs.seekp(0, std::fstream::end);
    if(fs.fail())
        throw_line("seek failed");

    auto file_size = (uint64_t)fs.tellp();

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

    // TODO: improve unaligned handling
    //    if(file_size % entry_size)
    //        throw_line("incomplete file or garbage in the end ({})", file_path.filename().string());

    return file_size / entry_size;
}

}
