module;
#include <cstdint>
#include <format>
#include <vector>

export module scsi.cmd;

import cd.cd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.misc;



namespace gpsxre
{

// clang-format off
static const uint32_t READ_CD_C2_SIZES[] =
{
    0,
    CD_C2_SIZE,
    2 + CD_C2_SIZE,
    0
};

static const uint32_t READ_CD_SUB_SIZES[] =
{
    0,
    CD_SUBCODE_SIZE,
    16,
    0,
    CD_SUBCODE_SIZE,
    0,
    0,
    0
};

static const uint32_t READ_CDDA_SIZES[] =
{
    CD_DATA_SIZE,
    CD_DATA_SIZE + 16,
    CD_DATA_SIZE + CD_SUBCODE_SIZE,
    CD_SUBCODE_SIZE,
    0, //TODO: analyze other values
    0,
    0,
    0,
    CD_RAW_DATA_SIZE
};
// clang-format on


export void strip_response_header(std::vector<uint8_t> &data)
{
    if(data.size() < sizeof(CMD_ParameterListHeader))
        data.clear();
    else
        data.erase(data.begin(), data.begin() + sizeof(CMD_ParameterListHeader));
}


// sends SCSI command once to get partial data and optionally followed by another command with appropriately sized buffer to get everything
template<typename T>
SPTD::Status cdb_send_receive(SPTD &sptd, std::vector<uint8_t> &response, T &cdb)
{
    SPTD::Status status;

    // some drives expect CSS related calls to be completed in one go
    // otherwise SCSI error is returned, keep this value high
    constexpr uint16_t initial_size = 4096;

    response.resize(initial_size);
    *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(response.size());
    status = sptd.sendCommand(&cdb, sizeof(cdb), response.data(), (uint32_t)response.size());
    if(status.status_code)
    {
        response.clear();
    }
    else
    {
        auto response_header = (CMD_ParameterListHeader *)response.data();

        uint16_t response_size = sizeof(response_header->data_length) + endian_swap(response_header->data_length);
        if(response_size > response.size())
        {
            response.resize(round_up_pow2<uint16_t>(response_size, sizeof(uint32_t)));

            *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(response_size);

            status = sptd.sendCommand(&cdb, sizeof(cdb), response.data(), (uint32_t)response.size());
            if(status.status_code)
                response_size = 0;
        }

        response.resize(response_size);
    }

    return status;
}


export SPTD::Status cmd_drive_ready(SPTD &sptd)
{
    CDB6_Generic cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::TEST_UNIT_READY;

    return sptd.sendCommand(&cdb, sizeof(cdb), nullptr, 0);
}


export SPTD::Status cmd_inquiry(SPTD &sptd, uint8_t *data, uint32_t data_size, INQUIRY_VPDPageCode page_code, bool command_support_data, bool enable_vital_product_data)
{
    CDB6_Inquiry cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::INQUIRY;
    cdb.command_support_data = command_support_data ? 1 : 0;
    cdb.enable_vital_product_data = enable_vital_product_data ? 1 : 0;
    cdb.page_code = (uint8_t)page_code;

    *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(data_size);

    return sptd.sendCommand(&cdb, sizeof(cdb), data, data_size);
}


export SPTD::Status cmd_read_capacity(SPTD &sptd, uint32_t &lba, uint32_t &block_length, bool rel_adr, uint32_t address, bool pmi)
{
    CDB10_ReadCapacity cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_CAPACITY;
    cdb.rel_adr = rel_adr ? 1 : 0;
    *(uint32_t *)cdb.address = endian_swap(address);
    cdb.pmi = pmi ? 1 : 0;

    READ_CAPACITY_Response response;

    auto status = sptd.sendCommand(&cdb, sizeof(cdb), &response, sizeof(response));
    if(!status.status_code)
    {
        lba = endian_swap(response.address);
        block_length = endian_swap(response.block_length);
    }

    return status;
}


export SPTD::Status cmd_read_toc(SPTD &sptd, std::vector<uint8_t> &response_data, bool time, READ_TOC_Format format, uint8_t track_number)
{
    response_data.clear();

    CDB10_ReadTOC cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_TOC;
    cdb.time = time ? 1 : 0;
    cdb.format = (uint8_t)format;
    cdb.track_number = track_number;

    return cdb_send_receive(sptd, response_data, cdb);
}


// OBSOLETE: remove after migrating to new CD dump code
export std::vector<uint8_t> cmd_read_toc(SPTD &sptd)
{
    std::vector<uint8_t> toc;

    CDB10_ReadTOC cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_TOC;
    cdb.format = (uint8_t)READ_TOC_Format::TOC;
    cdb.track_number = 1;

    // read TOC header first to get the full TOC size
    CMD_ParameterListHeader toc_response;
    *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(sizeof(toc_response));
    auto status = sptd.sendCommand(&cdb, sizeof(cdb), &toc_response, sizeof(toc_response));
    if(!status.status_code)
    {
        uint16_t toc_buffer_size = sizeof(toc_response.data_length) + endian_swap(toc_response.data_length);
        toc.resize(round_up_pow2<uint16_t>(toc_buffer_size, sizeof(uint32_t)));

        *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(toc_buffer_size);
        status = sptd.sendCommand(&cdb, sizeof(cdb), toc.data(), (uint32_t)toc.size());
        if(status.status_code)
            toc.clear();
        else
            toc.resize(toc_buffer_size);
    }

    return toc;
}


// OBSOLETE: remove after migrating to new CD dump code
export std::vector<uint8_t> cmd_read_full_toc(SPTD &sptd)
{
    std::vector<uint8_t> full_toc;

    CDB10_ReadTOC cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_TOC;
    cdb.format = (uint8_t)READ_TOC_Format::FULL_TOC;
    cdb.track_number = 1;

    // read TOC header first to get the full TOC size
    CMD_ParameterListHeader toc_response;
    *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(sizeof(toc_response));
    auto status = sptd.sendCommand(&cdb, sizeof(cdb), &toc_response, sizeof(toc_response));
    if(!status.status_code)
    {
        uint16_t toc_buffer_size = sizeof(toc_response.data_length) + endian_swap(toc_response.data_length);
        full_toc.resize(round_up_pow2<uint16_t>(toc_buffer_size, sizeof(uint32_t)));

        *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(toc_buffer_size);
        status = sptd.sendCommand(&cdb, sizeof(cdb), full_toc.data(), (uint32_t)full_toc.size());
        if(status.status_code)
            full_toc.clear();
        else
            full_toc.resize(toc_buffer_size);
    }

    return full_toc;
}


// OBSOLETE: remove after migrating to new CD dump code
export SPTD::Status cmd_read_cd_text(SPTD &sptd, std::vector<uint8_t> &cd_text)
{
    SPTD::Status status;

    CDB10_ReadTOC cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_TOC;
    cdb.format = (uint8_t)READ_TOC_Format::CD_TEXT;

    // read CD-TEXT header first to get the full TOC size
    CMD_ParameterListHeader toc_response;
    *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(sizeof(toc_response));
    status = sptd.sendCommand(&cdb, sizeof(cdb), &toc_response, sizeof(toc_response));
    if(!status.status_code)
    {
        uint16_t cd_text_buffer_size = sizeof(toc_response.data_length) + endian_swap(toc_response.data_length);
        if(cd_text_buffer_size > sizeof(toc_response))
        {
            cd_text.resize(round_up_pow2<uint16_t>(cd_text_buffer_size, sizeof(uint32_t)));

            *(uint16_t *)cdb.allocation_length = endian_swap<uint16_t>(cd_text_buffer_size);

            status = sptd.sendCommand(&cdb, sizeof(cdb), cd_text.data(), (uint32_t)cd_text.size());
            if(!status.status_code)
                cd_text.resize(cd_text_buffer_size);
        }
    }

    return status;
}


export SPTD::Status cmd_read_disc_structure(SPTD &sptd, std::vector<uint8_t> &response_data, uint8_t media_type, uint32_t address, uint8_t layer_number, READ_DISC_STRUCTURE_Format format,
    uint8_t agid)
{
    SPTD::Status status;

    response_data.clear();

    CDB12_ReadDiscStructure cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_DISC_STRUCTURE;
    cdb.media_type = media_type;
    *(uint32_t *)cdb.address = endian_swap(address);
    cdb.layer_number = layer_number;
    cdb.format = (uint8_t)format;
    cdb.agid = agid;

    status = cdb_send_receive(sptd, response_data, cdb);

    return status;
}


export SPTD::Status cmd_send_key(SPTD &sptd, const uint8_t *data, uint32_t data_size, SEND_KEY_KeyFormat key_format, uint8_t agid)
{
    CDB12_SendKey cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::SEND_KEY;
    cdb.key_format = (uint8_t)key_format;
    cdb.agid = agid;

    std::vector<uint8_t> parameter_list;
    if(data_size)
    {
        parameter_list.resize(sizeof(CMD_ParameterListHeader) + data_size);
        cdb.parameter_list_length = endian_swap<uint16_t>(parameter_list.size());

        auto header = (CMD_ParameterListHeader *)parameter_list.data();
        header->data_length = endian_swap<uint16_t>(parameter_list.size() - sizeof(header->data_length));

        memcpy(parameter_list.data() + sizeof(CMD_ParameterListHeader), data, data_size);
    }

    return sptd.sendCommand(&cdb, sizeof(cdb), parameter_list.empty() ? nullptr : parameter_list.data(), parameter_list.size(), true);
}



export SPTD::Status cmd_report_key(SPTD &sptd, std::vector<uint8_t> &response, uint32_t lba, REPORT_KEY_KeyClass key_class, uint8_t agid, REPORT_KEY_KeyFormat key_format)
{
    CDB12_ReportKey cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::REPORT_KEY;
    *(uint32_t *)cdb.lba = endian_swap(lba);
    cdb.key_class = (uint8_t)key_class;
    cdb.agid = agid;
    cdb.key_format = (uint8_t)key_format;

    return key_format == REPORT_KEY_KeyFormat::INVALIDATE_AGID ? sptd.sendCommand(&cdb, sizeof(cdb), nullptr, 0) : cdb_send_receive(sptd, response, cdb);
}


export SPTD::Status cmd_read(SPTD &sptd, uint8_t *buffer, uint32_t block_size, int32_t start_lba, uint32_t transfer_length, bool force_unit_access)
{
    CDB12_Read cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::READ12;
    cdb.force_unit_access = force_unit_access ? 1 : 0;
    *(int32_t *)cdb.starting_lba = endian_swap(start_lba);
    *(uint32_t *)cdb.transfer_blocks = endian_swap(transfer_length);

    return sptd.sendCommand(&cdb, sizeof(cdb), buffer, block_size * transfer_length);
}


export SPTD::Status cmd_read_cd(SPTD &sptd, uint8_t *sectors, int32_t start_lba, uint32_t transfer_length, READ_CD_ExpectedSectorType expected_sector_type, READ_CD_ErrorField error_field,
    READ_CD_SubChannel sub_channel)
{
    CDB12_ReadCD cdb = {};

    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_CD;
    cdb.expected_sector_type = (uint8_t)expected_sector_type;
    *(int32_t *)cdb.starting_lba = endian_swap(start_lba);
    cdb.transfer_blocks[0] = ((uint8_t *)&transfer_length)[2];
    cdb.transfer_blocks[1] = ((uint8_t *)&transfer_length)[1];
    cdb.transfer_blocks[2] = ((uint8_t *)&transfer_length)[0];
    cdb.error_flags = (uint8_t)error_field;
    cdb.include_edc = 1;
    cdb.include_user_data = 1;
    cdb.header_code = (uint8_t)READ_CD_HeaderCode::ALL;
    cdb.include_sync_data = 1;
    cdb.sub_channel_selection = (uint8_t)sub_channel;

    return sptd.sendCommand(&cdb, sizeof(cdb), sectors, CD_RAW_DATA_SIZE * transfer_length);
}


export SPTD::Status cmd_read_cdda(SPTD &sptd, uint8_t *sectors, int32_t start_lba, uint32_t transfer_length, READ_CDDA_SubCode sub_code)
{
    CDB12_ReadCDDA cdb = {};

    cdb.operation_code = (uint8_t)CDB_OperationCode::READ_CDDA;
    *(int32_t *)cdb.starting_lba = endian_swap(start_lba);
    *(uint32_t *)cdb.transfer_blocks = endian_swap(transfer_length);
    cdb.sub_code = (uint8_t)sub_code;

    return sptd.sendCommand(&cdb, sizeof(cdb), sectors, CD_RAW_DATA_SIZE * transfer_length);
}


export SPTD::Status cmd_plextor_reset(SPTD &sptd)
{
    CDB6_Generic cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::PLEXTOR_RESET;

    return sptd.sendCommand(&cdb, sizeof(cdb), nullptr, 0);
}


export SPTD::Status cmd_synchronize_cache(SPTD &sptd)
{
    CDB6_Generic cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::SYNCHRONIZE_CACHE;

    return sptd.sendCommand(&cdb, sizeof(cdb), nullptr, 0);
}


export SPTD::Status cmd_set_cd_speed(SPTD &sptd, uint16_t speed)
{
    CDB12_SetCDSpeed cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::SET_CD_SPEED;
    *(uint16_t *)cdb.read_speed = endian_swap(speed);

    return sptd.sendCommand(&cdb, sizeof(cdb), nullptr, 0);
}


export SPTD::Status cmd_asus_read_cache(SPTD &sptd, uint8_t *buffer, uint32_t offset, uint32_t size)
{
    CDB10_ASUS_ReadCache cdb;
    cdb.operation_code = (uint8_t)CDB_OperationCode::ASUS_READ_CACHE;
    cdb.unknown = 6;
    cdb.offset = endian_swap(offset);
    cdb.size = endian_swap(size);

    return sptd.sendCommand(&cdb, sizeof(cdb), buffer, size);
}


export SPTD::Status cmd_get_configuration_current_profile(SPTD &sptd, GET_CONFIGURATION_FeatureCode_ProfileList &current_profile)
{
    CDB10_GetConfiguration cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::GET_CONFIGURATION;
    cdb.requested_type = (uint8_t)GET_CONFIGURATION_RequestedType::ONE;
    cdb.starting_feature_number = 0;

    GET_CONFIGURATION_FeatureHeader feature_header = {};
    uint16_t size = sizeof(feature_header);
    *(uint16_t *)cdb.allocation_length = endian_swap(size);
    auto status = sptd.sendCommand(&cdb, sizeof(cdb), &feature_header, size);

    current_profile = (GET_CONFIGURATION_FeatureCode_ProfileList)endian_swap(feature_header.current_profile);

    return status;
}


SPTD::Status cmd_get_configuration(SPTD &sptd)
{
    CDB10_GetConfiguration cdb = {};
    cdb.operation_code = (uint8_t)CDB_OperationCode::GET_CONFIGURATION;
    cdb.requested_type = (uint8_t)GET_CONFIGURATION_RequestedType::ALL;
    cdb.starting_feature_number = 0;

    uint16_t size = std::numeric_limits<uint16_t>::max();
    *(uint16_t *)cdb.allocation_length = endian_swap(size);
    std::vector<uint8_t> buffer(size);

    auto status = sptd.sendCommand(&cdb, sizeof(cdb), buffer.data(), buffer.size());

    auto feature_header = (GET_CONFIGURATION_FeatureHeader *)buffer.data();
    uint32_t fds_size = endian_swap(feature_header->data_length) - (sizeof(GET_CONFIGURATION_FeatureHeader) - sizeof(feature_header->data_length));
    uint8_t *fds_start = buffer.data() + sizeof(GET_CONFIGURATION_FeatureHeader);
    uint8_t *fds_end = fds_start + fds_size;

    // FIXME: support more than 0xFFFF size?

    for(auto fds = (GET_CONFIGURATION_FeatureDescriptor *)fds_start;
        (uint8_t *)fds + sizeof(GET_CONFIGURATION_FeatureDescriptor) <= fds_end && (uint8_t *)fds + sizeof(GET_CONFIGURATION_FeatureDescriptor) + fds->additional_length <= fds_end;
        fds = (GET_CONFIGURATION_FeatureDescriptor *)((uint8_t *)fds + sizeof(GET_CONFIGURATION_FeatureDescriptor) + fds->additional_length))
    {
        // std::cout << endian_swap(fds->feature_code) << std::endl;
    }

    return status;
}

}
