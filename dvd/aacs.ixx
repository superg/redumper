// based on aacskeys

module;
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "throw_line.hh"

export module bd.aacs;

import dvd.css;
import filesystem.udf;
import hash.edcsa;
import hash.sha1;
import options;
import readers.data_reader;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.endian;
import utils.hex_bin;
import utils.strings;



namespace gpsxre
{

export class AACS
{
public:
    static int getMKBVersion(DataReader *reader)
    {
        auto root_directory = udf::Browser::rootDirectory(reader);
        if(!root_directory)
        {
            return -1;
        }

        auto media_key_block = root_directory->subEntry("AACS/MKB_RO.inf");
        if(!media_key_block)
        {
            return -1;
        }

        auto data = media_key_block->read();

        // The media key block version is stored as a big endian integer from 0x00000008 to 0x0000000B
        uint32_t mkbv;
        memcpy(&mkbv, data.data() + 8, sizeof(uint32_t));
        mkbv = endian_swap(mkbv);

        return mkbv;
    }

    // This method of getting the volume ID (authenticating with the drive and asking nicely) will only work as long as the drive has not revoked the provided certificate.
    // Therefore, it would be beneficial to implement other methods of reading the volume ID that do not require authentication:
    // TODO: Some drives have firmware patches that bypass the need for authentication prior to reading the volume ID.
    // TODO: Some drives, including the Xbox 360 HD-DVD Player, can be used to read the volume ID without authentication by clearing certain areas of memory prior to requesting the read.
    // (It might only be the xbox drive, and/or it might need to have patched firmware. its unclear)
    // TODO: libredrive allows the reading of the volume ID without needing authentication. It's not via the regular command (i think),
    // although its probably possible to just trace the scsi commands makemkv outputs to get it, or it might be documented in libdriveio.
    static std::string getVolumeID(SPTD &sptd, Options &options)
    {
        if(!options.host_nonce.get() || !options.host_certificate.get() || !options.host_key_point.get() || !options.host_private_key.get())
        {
            return "";
        }

        std::array<uint8_t, 20> host_nonce;
        std::array<uint8_t, 92> host_certificate;
        std::array<uint8_t, 40> host_key_point;
        std::array<uint8_t, 20> host_private_key;
        hex2bin(host_nonce.data(), host_nonce.size(), *options.host_nonce.get());
        hex2bin(host_certificate.data(), host_certificate.size(), *options.host_certificate.get());
        hex2bin(host_key_point.data(), host_key_point.size(), *options.host_key_point.get());
        hex2bin(host_private_key.data(), host_private_key.size(), *options.host_private_key.get());

        // Authenticate with the drive.
        CSS css(sptd);
        auto agid = css.reportAGID(false, KEY_KeyClass::AACS);
        auto drive_nonce = getDriveNonce(sptd, agid, host_nonce, host_certificate);
        reportDriveKey(sptd, agid);
        sendHostKey(sptd, agid, drive_nonce, host_key_point, host_private_key);

        // Having authenticated with the drive, read the volume ID.
        std::vector<uint8_t> response_data;
        auto status = cmd_read_disc_structure(sptd, response_data, 1, 0, 0, READ_DISC_STRUCTURE_Format::AACS_VID, agid);
        if(status.status_code)
            throw_line("failed to read volume ID, SCSI ({})", SPTD::StatusMessage(status));

        strip_response_header(response_data);
        // The response data contains the 16-byte volume ID and then the 16-byte volume MAC, but we only need the volume ID.
        response_data.resize(16);
        return str_uppercase(bin2hex(response_data));
    }

private:
    static std::vector<uint8_t> getDriveNonce(SPTD &sptd, uint8_t agid, std::array<uint8_t, 20> host_nonce, std::array<uint8_t, 92> host_certificate)
    {
        REPORT_KEY_ChallengeCertificate challenge_certificate = {};
        std::memcpy(challenge_certificate.host_nonce, host_nonce.data(), host_nonce.size());
        std::memcpy(challenge_certificate.host_certificate, host_certificate.data(), host_certificate.size());

        auto status = cmd_send_key(sptd, (uint8_t *)&challenge_certificate, sizeof(challenge_certificate), KEY_KeyClass::AACS, KEY_KeyFormat::CHALLENGE_KEY, agid);
        if(status.status_code)
            throw_line("failed to send host certificate, SCSI ({})", SPTD::StatusMessage(status));

        std::vector<uint8_t> response_data;
        status = cmd_report_key(sptd, response_data, 0, KEY_KeyClass::AACS, agid, KEY_KeyFormat::CHALLENGE_KEY);
        if(status.status_code)
            throw_line("failed to read drive certificate, SCSI ({})", SPTD::StatusMessage(status));

        strip_response_header(response_data);
        // The response data contains the 20-byte drive nonce and then the 92-byte drive certificate, but we only need the drive nonce for authentication.
        response_data.resize(20);
        return response_data;
    }

    static void reportDriveKey(SPTD &sptd, uint8_t agid)
    {
        std::vector<uint8_t> response_data;
        auto status = cmd_report_key(sptd, response_data, 0, KEY_KeyClass::AACS, agid, KEY_KeyFormat::KEY1);
        if(status.status_code)
            throw_line("failed to read drive key, SCSI ({})", SPTD::StatusMessage(status));

        // The response data contains the 40-byte drive key point and then the 40-byte drive key signature,
        // but we don't actually need them for anything since we don't perform any cryptographic verification on our end.
    }

    static void sendHostKey(SPTD &sptd, uint8_t agid, std::vector<uint8_t> drive_nonce, std::array<uint8_t, 40> host_key_point, std::array<uint8_t, 20> host_private_key)
    {
        SHA1 sha1;
        sha1.update(drive_nonce.data(), drive_nonce.size());
        sha1.update(host_key_point.data(), host_key_point.size());
        std::array<uint8_t, 20> hash;
        hex2bin(hash.data(), hash.size(), sha1.final());
        auto host_key_signature = ECDSA::sign(host_private_key, hash);

        REPORT_KEY_Signed host_signed = {};
        std::memcpy(host_signed.host_key_point, host_key_point.data(), host_key_point.size());
        std::memcpy(host_signed.host_key_signature, host_key_signature.data(), host_key_signature.size());

        auto status = cmd_send_key(sptd, (uint8_t *)&host_signed, sizeof(host_signed), KEY_KeyClass::AACS, KEY_KeyFormat::KEY1, agid);
        if(status.status_code)
            throw_line("failed to send host key, SCSI ({})", SPTD::StatusMessage(status));
    }
};

}
