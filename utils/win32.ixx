module;
#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

export module utils.win32;



namespace gpsxre
{

struct MZHeader
{
    char magic[2];
    uint16_t last_page_bytes_count;
    uint16_t pages_count;
    uint16_t relocation_entries_count;
    uint16_t header_paragraphs_count;
    uint16_t min_extra_paragraphs_count;
    uint16_t max_extra_paragraphs_count;
    uint16_t initial_ss;
    uint16_t initial_sp;
    uint16_t checksum;
    uint16_t initial_ip;
    uint16_t initial_cs;
    uint16_t relocation_table_offset;
    uint16_t overlays_count;
    uint16_t reserved1[4];
    uint16_t oem_identifier;
    uint16_t oem_information;
    uint16_t reserved2[10];
    uint32_t lfanew;
};


struct PEHeader
{
    char magic[4];
    uint16_t machine;
    uint16_t sections_count;
    uint32_t time_date_stamp;
    uint32_t symbol_table_pointer;
    uint32_t symbols_count;
    uint16_t optional_header_size;
    uint16_t characteristics;
};


struct PESectionHeader
{
    char name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t raw_data_size;
    uint32_t raw_data_pointer;
    uint32_t relocations_pointer;
    uint32_t linenumbers_pointer;
    uint16_t relocations_count;
    uint16_t linenumbers_count;
    uint32_t characteristics;
};


struct PEDataDirectory
{
    uint32_t virtual_address;
    uint32_t size;
};


struct PE32OptionalHeader
{
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t code_size;
    uint32_t initialized_data_size;
    uint32_t uninitialized_data_size;
    uint32_t entry_point_address;
    uint32_t code_base;
    uint32_t data_base;
    uint32_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_operating_system_version;
    uint16_t minor_operating_system_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t image_size;
    uint32_t headers_size;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint32_t stack_reserve_size;
    uint32_t stack_commit_size;
    uint32_t heap_reserve_size;
    uint32_t heap_commit_size;
    uint32_t loader_flags;
    uint32_t rva_and_sizes_count;
    PEDataDirectory data_directory[16];
};


struct PE64OptionalHeader
{
    uint16_t magic;
    uint8_t major_linker_version;
    uint8_t minor_linker_version;
    uint32_t code_size;
    uint32_t initialized_data_size;
    uint32_t uninitialized_data_size;
    uint32_t entry_point_address;
    uint32_t code_base;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_operating_system_version;
    uint16_t minor_operating_system_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t image_size;
    uint32_t headers_size;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t stack_reserve_size;
    uint64_t stack_commit_size;
    uint64_t heap_reserve_size;
    uint64_t heap_commit_size;
    uint32_t loader_flags;
    uint32_t rva_and_sizes_count;
    PEDataDirectory data_directory[16];
};


constexpr std::string_view MZ_MAGIC("MZ");
constexpr std::string_view PE_MAGIC("PE\0\0", 4);
constexpr uint16_t PE32_OPTIONAL_MAGIC = 0x010B;
constexpr uint16_t PE64_OPTIONAL_MAGIC = 0x020B;

enum class PEDirectoryEntry : uint32_t
{
    EXPORT,
    IMPORT,
    RESOURCE,
    EXCEPTION,
    SECURITY,
    BASERELOC,
    DEBUG,
    ARCHITECTURE,
    GLOBALPTR,
    TLS,
    LOAD_CONFIG,
    BOUND_IMPORT,
    IAT,
    DELAY_IMPORT,
    COM_DESCRIPTOR
};


export uint64_t get_pe_executable_extent(std::span<const uint8_t> file_data)
{
    uint64_t extent_size = 0;

    uint64_t p = 0;

    // MZ header
    auto mz_header = (const MZHeader &)file_data[p];
    p += sizeof(MZHeader);
    if(p > file_data.size())
        return extent_size;
    if(std::string_view(mz_header.magic, std::size(mz_header.magic)) != MZ_MAGIC)
        return extent_size;
    p = mz_header.lfanew;

    // PE header
    auto pe_header = (const PEHeader &)file_data[p];
    p += sizeof(PEHeader);
    if(p > file_data.size())
        return extent_size;
    if(std::string_view(pe_header.magic, std::size(pe_header.magic)) != PE_MAGIC)
        return extent_size;

    // optional PE header
    auto pe_optional_header = (const PE32OptionalHeader &)file_data[p];
    p += pe_header.optional_header_size;
    if(p > file_data.size())
        return extent_size;
    if(pe_header.optional_header_size)
    {
        if(pe_optional_header.magic == PE32_OPTIONAL_MAGIC)
        {
            extent_size = std::max(extent_size,
                (uint64_t)pe_optional_header.data_directory[(uint32_t)PEDirectoryEntry::SECURITY].virtual_address + pe_optional_header.data_directory[(uint32_t)PEDirectoryEntry::SECURITY].size);
        }
        else if(pe_optional_header.magic == PE64_OPTIONAL_MAGIC)
        {
            auto h = (const PE64OptionalHeader &)file_data[p];
            extent_size = std::max(extent_size, (uint64_t)h.data_directory[(uint32_t)PEDirectoryEntry::SECURITY].virtual_address + h.data_directory[(uint32_t)PEDirectoryEntry::SECURITY].size);
        }
        // for Authenticode signatures, virtual address is actually a file offset, not an RVA
    }

    // PE sections
    auto section_headers = (PESectionHeader *)&file_data[p];
    p += sizeof(PESectionHeader) * pe_header.sections_count;
    if(p > file_data.size())
        return extent_size;
    for(uint16_t i = 0; i < pe_header.sections_count; ++i)
        extent_size = std::max(extent_size, (uint64_t)section_headers[i].raw_data_pointer + section_headers[i].raw_data_size);

    return extent_size;
}

}
