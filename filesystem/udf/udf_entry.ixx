module;
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <variant>
#include <vector>

export module filesystem.udf:entry;

import :defs;
import readers.data_reader;
import cd.cdrom;
import utils.misc;
import utils.strings;



namespace gpsxre::udf
{

static std::variant<std::shared_ptr<udf::FileEntry>, std::shared_ptr<udf::ExtendedFileEntry>> getFileEntry(DataReader *data_reader, std::vector<uint32_t> partition_starting_locations,
    lb_addr file_entry_extent)
{
    if(partition_starting_locations.size() - 1 >= file_entry_extent.partition_reference_number)
    {
        std::vector<uint8_t> data(FORM1_DATA_SIZE);
        data_reader->read(data.data(), partition_starting_locations[file_entry_extent.partition_reference_number] + file_entry_extent.logical_block_number, 1);

        auto const &tag = (udf::DescriptorTag &)data[0];

        if(tag.tag_identifier == udf::TagIdentifier::FILE_ENTRY)
        {
            // Parse the static part of the file entry and use it to determine the true size of the file entry.
            auto const &static_file_entry = (udf::FileEntry &)data[0];
            std::size_t true_entry_size = sizeof(udf::FileEntry) + static_file_entry.length_of_extended_attributes + static_file_entry.length_of_allocation_descriptors;

            // Allocate the memory to store the entire file entry and read the file entry into that memory.
            auto storage = std::make_shared<uint8_t[]>(true_entry_size);
            auto file_entry = std::shared_ptr<udf::FileEntry>(storage, (udf::FileEntry *)storage.get());
            std::memcpy(file_entry.get(), data.data(), true_entry_size);

            return file_entry;
        }
        else if(tag.tag_identifier == udf::TagIdentifier::EXTENDED_FILE_ENTRY)
        {
            // Parse the static part of the file entry and use it to determine the true size of the file entry.
            auto const &static_file_entry = (udf::ExtendedFileEntry &)data[0];
            std::size_t true_entry_size = sizeof(udf::ExtendedFileEntry) + static_file_entry.length_of_extended_attributes + static_file_entry.length_of_allocation_descriptors;

            // Allocate the memory to store the entire file entry and read the file entry into that memory.
            auto storage = std::make_shared<uint8_t[]>(true_entry_size);
            auto file_entry = std::shared_ptr<udf::ExtendedFileEntry>(storage, (udf::ExtendedFileEntry *)storage.get());
            std::memcpy(file_entry.get(), data.data(), true_entry_size);

            return file_entry;
        }
    }

    return {};
}

}

export namespace gpsxre::udf
{

class Entry
{
public:
    Entry(DataReader *data_reader, std::string name, const udf::long_ad &icb, const std::vector<uint32_t> &partition_starting_locations)
        : _dataReader(data_reader)
        , _name(name)
        , _icb(icb)
        , _partitionStartingLocations(partition_starting_locations)
        , _fileEntry(getFileEntry(data_reader, partition_starting_locations, icb.extent_location))
    {
        ;
    }

    const std::string &name() const
    {
        return _name;
    }


    bool isDirectory() const
    {
        return std::visit([](const auto &entry) { return entry->icb_tag.file_type == udf::FileType::DIRECTORY; }, _fileEntry);
    }

    std::list<std::shared_ptr<Entry>> entries()
    {
        std::list<std::shared_ptr<Entry>> entries;

        if(isDirectory())
        {
            // Read the entire directory into memory.
            auto const directory_data = read();

            // Loop over each file identifier descriptor and record their matching entries.
            uint32_t offset = 0;
            while(offset < directory_data.size())
            {
                auto const &fid = (const udf::FileIdentifierDescriptor &)directory_data[offset];

                // As per ECMA 167 14.4.3, use bit 3 of the file characteristics to determine if this descriptors ICB refers to the files parent directory,
                // in which case we don't need to record it as an entry.
                if((fid.file_characteristics & 0x08) == 0)
                {
                    // Read the file identifier into a string, ignoring the first byte as that contains the Compression ID of the string.
                    const uint8_t *name_data = fid.implementation_use_and_file_identifier_and_padding + fid.length_of_implementation_use;
                    // The file identifier is technically stored in the OSTA Compressed Unicode format, but for now we just ignore that.
                    std::string name(name_data + 1, name_data + fid.length_of_file_identifier);

                    entries.push_back(std::make_shared<Entry>(_dataReader, name, fid.icb, _partitionStartingLocations));
                }

                // Replicate the effect of the padding bytes at the end of FileIdentifierDescriptor as specified by ECMA 167 14.4.9
                offset += round_up(sizeof(udf::FileIdentifierDescriptor) + fid.length_of_implementation_use + fid.length_of_file_identifier, 4u);
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
            auto directory_entries = entry ? entry->entries() : entries();
            auto prospective_entry = std::ranges::find(directory_entries, c, [](auto const &e) { return e->name(); });

            if(prospective_entry == directory_entries.end())
            {
                entry.reset();
                break;
            }
            else
            {
                entry = *prospective_entry;
            }
        }

        return entry;
    }

    std::vector<uint8_t> read()
    {
        // Determine how large the entry is and allocate the memory to read it into.
        auto information_length = std::visit([](const auto &entry) -> uint32_t { return entry->information_length; }, _fileEntry);
        std::vector<uint8_t> data(information_length * FORM1_DATA_SIZE);

        // Determine where the allocation descriptors start and how long they continue for.
        const uint8_t *start_of_allocation_descriptors =
            std::visit([](const auto &entry) -> const uint8_t * { return entry->extended_attributes_and_allocation_descriptors + entry->length_of_extended_attributes; }, _fileEntry);
        auto length_of_allocation_descriptors = std::visit([](const auto &entry) -> uint32_t { return entry->length_of_allocation_descriptors; }, _fileEntry);

        auto icb_tag = std::visit([](const auto &entry) -> auto { return entry->icb_tag; }, _fileEntry);
        // As per ECMA 167 14.6.8, use bits 0-2 of the flags as a 3-bit unsigned binary number to determine the type of allocation descriptors.
        auto short_descriptors = (icb_tag.flags & 0x07) == 0;
        auto descriptor_size = short_descriptors ? sizeof(udf::short_ad) : sizeof(udf::long_ad);

        // Loop over every allocation descriptor and read them into data.
        for(uint32_t i = 0; i < length_of_allocation_descriptors; i += descriptor_size)
        {
            if(short_descriptors)
            {
                udf::short_ad ad = (udf::short_ad &)*(start_of_allocation_descriptors + i);
                // TODO: It might be possible to avoid needing to use _icb here, which would allow us to remove it entirely.
                auto lba = _partitionStartingLocations[_icb.extent_location.partition_reference_number] + ad.extent_position;

                _dataReader->read(data.data() + data.size(), lba, ad.extent_length);
            }
            else
            {
                udf::long_ad ad = (udf::long_ad &)*(start_of_allocation_descriptors + i);
                auto lba = _partitionStartingLocations[ad.extent_location.partition_reference_number] + ad.extent_location.logical_block_number;

                _dataReader->read(data.data() + data.size(), lba, ad.extent_length);
            }
        }

        data.resize(information_length);
        return data;
    }

private:
    DataReader *_dataReader;
    std::string _name;
    udf::long_ad _icb;
    std::vector<uint32_t> _partitionStartingLocations;
    std::variant<std::shared_ptr<udf::FileEntry>, std::shared_ptr<udf::ExtendedFileEntry>> _fileEntry;
};

}
