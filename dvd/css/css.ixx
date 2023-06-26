// based on libdvdcss

module;
#include <algorithm>
#include <vector>
#include "throw_line.hh"

export module dvd.css;

import cd.cdrom;
import readers.form1_reader;
import scsi.cmd;
import scsi.mmc;
import scsi.sptd;
import utils.misc;



namespace gpsxre
{

export class CSS
{
public:
	CSS(SPTD &sptd)
		: _sptd(sptd)
	{
		;
	}


	std::vector<uint8_t> getDiscKey(bool cprm)
	{
		std::vector<uint8_t> disc_key;

		uint8_t agid = reportAGID(cprm);
		auto bus_key = getBusKey(agid);

		auto disc_keys = readDiscKey(agid);

		if(!reportASF())
			throw_line("authentication failed, authentication success flag (ASF) is 0");

		shuffle(disc_keys.data(), disc_keys.size(), bus_key.data());

		for(unsigned int n = 0; n < countof(_PLAYER_KEYS) && disc_key.empty(); ++n)
		{
			for(unsigned int i = 1; i < disc_keys.size() / _BLOCK_SIZE; ++i)
			{
				// first entry in disc keys block is the key encrypted with itself
				// decrypt current disc key using player key and compare it to the first entry
				auto k = decryptKey(0, _PLAYER_KEYS[n], &disc_keys[_BLOCK_SIZE * i]);
				if(k == decryptKey(0, k.data(), &disc_keys[0]))
				{
					disc_key = k;
					break;
				}
			}
		}

		return disc_key;
	}


	std::vector<uint8_t> getTitleKey(const std::vector<uint8_t> &disc_key, uint32_t lba, bool cprm)
	{
		std::vector<uint8_t> title_key;

		uint8_t agid = reportAGID(cprm);
		auto bus_key = getBusKey(agid);

		auto encrypted_title_key = reportTitleKey(agid, lba);

		if(reportASF())
		{
			if(!encrypted_title_key.empty())
			{
				shuffle(encrypted_title_key.data(), encrypted_title_key.size(), bus_key.data());

				// only if not zeroed
				if(!is_zeroed(encrypted_title_key.data(), encrypted_title_key.size()))
					title_key = decryptTitleKey(disc_key.data(), encrypted_title_key.data());
			}
		}

		return title_key;
	}


	static std::vector<uint8_t> crackTitleKey(uint32_t lba_start, uint32_t lba_end, Form1Reader &form1_reader)
	{
		std::vector<uint8_t> title_key;

		bool encrypted = false;

		std::vector<uint8_t> data(FORM1_DATA_SIZE);
		for(uint32_t lba = lba_start; lba < lba_end; ++lba)
		{
			if(!form1_reader.read(data.data(), lba, 1))
				continue;

			// PES_scrambling_control does not exist in a system_header, a padding_stream or a private_stream2 (and others?)
			if(data[0x14] & 0x30 && !(data[0x11] == 0xbb || data[0x11] == 0xbe || data[0x11] == 0xbf))
			{
				encrypted = true;

				title_key = attackPattern(data.data());

				//TODO: evaluate if this attack is useful for small vobs
//				if(title_key.empty())
//					title_key = attackPadding(data.data());

				if(!title_key.empty())
					break;
			}

			// stop after 2000 blocks if we haven't seen any encrypted blocks
			if(!encrypted && lba >= lba_start + 2000)
				break;

		}

		if(title_key.empty() && !encrypted)
			title_key.resize(_BLOCK_SIZE);

		return title_key;
	}

private:
	enum class KeyType : uint8_t
	{
		KEY1,
		KEY2,
		BUS_KEY,
		COUNT
	};

	constexpr static unsigned int _AGID_COUNT = 4;
	constexpr static unsigned int _BLOCK_SIZE = 5;
	constexpr static unsigned int _CHALLENGE_SIZE = _BLOCK_SIZE * 2;
	constexpr static unsigned int _BLOCKS_COUNT = 6;
	constexpr static unsigned int _VARIANTS_COUNT = 32;

	static const uint8_t _DECRYPT_TAB1[256];
	static const uint8_t _DECRYPT_TAB2[256];
	static const uint8_t _DECRYPT_TAB3[8];
	static const uint8_t _DECRYPT_TAB4[256];
	static const uint8_t _DECRYPT_TAB5[256];
	static const uint8_t _CRYPT_TAB0[256];
	static const uint8_t _CRYPT_TAB1[256];
	static const uint8_t _CRYPT_TAB2[256];
	static const uint8_t _CRYPT_TAB3[288];
	static const uint8_t _PERM_VARIANT[(uint8_t)KeyType::COUNT][_VARIANTS_COUNT];
	static const uint8_t _PERM_CHALLENGE[(uint8_t)KeyType::COUNT][_CHALLENGE_SIZE];
	static const uint8_t _VARIANTS[_VARIANTS_COUNT];
	static const uint8_t _SECRET[_BLOCK_SIZE];
	static const uint8_t _PLAYER_KEYS[31][_BLOCK_SIZE];

	SPTD &_sptd;


	std::vector<uint8_t> getBusKey(uint8_t agid)
	{
		// setup a challenge, any values should work
		std::vector<uint8_t> challenge(_CHALLENGE_SIZE);
		for(unsigned int i = 0; i < _CHALLENGE_SIZE; ++i)
			challenge[i] = i;

		// send generated challenge to the drive and receive encrypted key1
		sendChallengeKey(agid, challenge.data());
		auto key1 = reportKey1(agid);

		// find a variant that was used to encrypt the key by the drive
		uint8_t variant = _VARIANTS_COUNT;
		for(unsigned int i = 0; i < _VARIANTS_COUNT; ++i)
		{
			if(encryptKey(_PERM_VARIANT[(uint8_t)KeyType::KEY1][i], _PERM_CHALLENGE[(uint8_t)KeyType::KEY1], challenge.data()) == key1)
			{
				variant = i;
				break;
			}
		}
		if(variant == _VARIANTS_COUNT)
			throw_line("failed to determine a variant");

		// now that we know a variant, request a challenge from the drive, encrypt key2 and send it back
		challenge = reportChallengeKey(agid);
		auto key2 = encryptKey(_PERM_VARIANT[(uint8_t)KeyType::KEY2][variant], _PERM_CHALLENGE[(uint8_t)KeyType::KEY2], challenge.data());
		sendKey2(agid, key2.data());

		// generate bus key based on key1 and key2 challenge
		memcpy(challenge.data(), key1.data(), _BLOCK_SIZE);
		memcpy(challenge.data() + _BLOCK_SIZE, key2.data(), _BLOCK_SIZE);
		return encryptKey(_PERM_VARIANT[(uint8_t)KeyType::BUS_KEY][variant], _PERM_CHALLENGE[(uint8_t)KeyType::BUS_KEY], challenge.data());
	}


	static std::vector<uint8_t> decryptTitleKey(const uint8_t *disc_key, const uint8_t *title_key)
	{
		return decryptKey(0xFF, disc_key, title_key);
	}


	static std::vector<uint8_t> encryptKey(uint8_t css_variant, const uint8_t *perm_challenge, const uint8_t *challenge)
	{
		std::vector<uint8_t> key(_BLOCK_SIZE);

		uint8_t scratch[_BLOCK_SIZE];
		for(unsigned int i = 0; i < _BLOCK_SIZE; ++i)
			scratch[i] = challenge[perm_challenge[_BLOCK_SIZE + i]] ^ _SECRET[i] ^ _CRYPT_TAB2[i];

		uint8_t bits[_BLOCKS_COUNT * _BLOCK_SIZE];

		uint32_t lfsr0 = scratch[0] << 17 | scratch[1] << 9 | (scratch[2] & ~7) << 1 | 8 | scratch[2] & 7;
		uint32_t lfsr1 = scratch[3] << 9 | 0x100 | scratch[4];

		uint8_t carry = 0;
		for(unsigned int i = _BLOCKS_COUNT * _BLOCK_SIZE; i > 0; --i)
		{
			uint8_t value = 0;

			for(unsigned int bit = 0; bit < 8; ++bit)
			{
				uint8_t lfsr0_o = (lfsr0 >> 24 ^ lfsr0 >> 21 ^ lfsr0 >> 20 ^ lfsr0 >> 12) & 1;
				lfsr0 = lfsr0 << 1 | lfsr0_o;

				uint8_t lfsr1_o = (lfsr1 >> 16 ^ lfsr1 >> 2) & 1;
				lfsr1 = lfsr1 << 1 | lfsr1_o;

				uint8_t combined = !lfsr1_o + carry + !lfsr0_o;
					
				carry = combined >> 1 & 1;
				value |= (combined & 1) << bit;
			}

			bits[i - 1] = value;
		}

		for(unsigned int i = 0; i < _BLOCK_SIZE; ++i)
			key[i] = challenge[perm_challenge[i]];

		uint8_t *src = key.data(), *dst = scratch;
		for(unsigned int i = 0; i < _BLOCKS_COUNT; ++i)
		{
			encryptBlock(dst, src, bits + (_BLOCK_SIZE - i) * _BLOCK_SIZE, css_variant, i / 2 & 1);
			std::swap(dst, src);
		}

		// xor back final value
		key[4] ^= key[0];

		return key;
	}


	static void encryptBlock(uint8_t *dst, const uint8_t *src, const uint8_t *bits, uint8_t css_variant, bool mid)
	{
		for(unsigned int i = 0; i < _BLOCK_SIZE; ++i)
		{
			uint8_t value = bits[i] ^ src[i];
			value = _CRYPT_TAB1[value] ^ ~_CRYPT_TAB2[value] ^ _VARIANTS[css_variant] ^ _CRYPT_TAB2[css_variant];
			value = _CRYPT_TAB2[value] ^ _CRYPT_TAB3[value] ^ (i + 1 == _BLOCK_SIZE ? 0 : src[i + 1]);
			if(mid)
				value = _CRYPT_TAB0[value] ^ _CRYPT_TAB2[value];

			dst[i] = value;
		}

		dst[4] ^= dst[0];
	}


	static std::vector<uint8_t> decryptKey(uint8_t invert, const uint8_t *key, const uint8_t *encrypted_key)
	{
		std::vector<uint8_t> decrypted_key(_BLOCK_SIZE);

		uint8_t scratch[_BLOCK_SIZE];

		unsigned int lfsr1_lo = key[0] | 0x100;
		unsigned int lfsr1_hi = key[1];

		unsigned int lfsr0 = (key[4] << 17 | key[3] << 9 | key[2] << 1) + 8 - (key[2] & 7);
		lfsr0 = _DECRYPT_TAB4[lfsr0 & 0xff] << 24 | _DECRYPT_TAB4[lfsr0 >> 8 & 0xff] << 16 | _DECRYPT_TAB4[lfsr0 >> 16 & 0xff] << 8 | _DECRYPT_TAB4[lfsr0 >> 24 & 0xff];

		unsigned int combined = 0;
		for(unsigned int i = 0; i < _BLOCK_SIZE; ++i)
		{
			uint8_t lfsr1_o = _DECRYPT_TAB2[lfsr1_hi] ^ _DECRYPT_TAB3[lfsr1_lo % 8];
			lfsr1_hi = lfsr1_lo >> 1;
			lfsr1_lo = (lfsr1_lo & 1) << 8 ^ lfsr1_o;
			lfsr1_o = _DECRYPT_TAB4[lfsr1_o];

			uint8_t lfsr0_o = (((((lfsr0 >> 8 ^ lfsr0) >> 1) ^ lfsr0) >> 3) ^ lfsr0) >> 7;
			lfsr0 = lfsr0 >> 8 | lfsr0_o << 24;

			combined += (lfsr0_o ^ invert) + lfsr1_o;
			scratch[i] = combined & 0xff;
			combined >>= 8;
		}

		for(int i = _BLOCK_SIZE - 1; i >= 0; --i)
			decrypted_key[i] = scratch[i] ^ _DECRYPT_TAB1[encrypted_key[i]] ^ (i ? encrypted_key[i - 1] : decrypted_key[4]);

		for(int i = _BLOCK_SIZE - 1; i >= 0; --i)
			decrypted_key[i] = scratch[i] ^ _DECRYPT_TAB1[decrypted_key[i]] ^ (i ? decrypted_key[i - 1] : 0);

		return decrypted_key;
	}


	static void shuffle(uint8_t *data, uint32_t data_size, const uint8_t *key)
	{
		for(uint32_t i = 0; i < data_size; ++i)
			data[i] ^= key[(_BLOCK_SIZE - 1) - (i % _BLOCK_SIZE)];
	}


	static std::vector<uint8_t> attackPattern(const uint8_t *data)
	{
		std::vector<uint8_t> title_key;

		// for all cycle length from 2 to 48
		unsigned int best_plen = 0;
		unsigned int best_p = 0;
		for(unsigned int i = 2; i < 0x30; ++i)
		{
			// find the number of bytes that repeats in cycles
			for(unsigned int j = i + 1; j < 0x80 && data[0x7F - j % i] == data[0x7F - j]; ++j)
			{
				// we have found j repeating bytes with a cycle length i
				if(j > best_plen)
				{
					best_plen = j;
					best_p = i;
				}
			}
		}

		// we need at most 10 plain text bytes, make sure that we have at least 20 repeated bytes and that they have cycled at least one time
		if((best_plen > 3) && (best_plen / best_p >= 2))
			title_key = recoverTitleKey(&data[0x80], &data[0x80 - (best_plen / best_p) * best_p], &data[0x54]);

		return title_key;
	}


	static std::vector<uint8_t> recoverTitleKey(const uint8_t *encrypted, const uint8_t *decrypted, const uint8_t *sector_seed)
	{
		std::vector<uint8_t> title_key;

		uint8_t scratch[10];
		for(unsigned int i = 0; i < 10; ++i)
			scratch[i] = _DECRYPT_TAB1[encrypted[i]] ^ decrypted[i];

		for(unsigned int i_try = 0; i_try < 0x10000; ++i_try)
		{
			unsigned int t1 = i_try >> 8 | 0x100;
			unsigned int t2 = i_try & 0xff;
			unsigned int t3 = 0;
			unsigned int t5 = 0;

			// iterate cipher 4 times to reconstruct LFSR2
			unsigned int i;
			for(i = 0; i < 4; ++i)
			{
				// advance LFSR1 normally
				unsigned int t4 = _DECRYPT_TAB2[t2] ^ _DECRYPT_TAB3[t1 % 8];
				t2 = t1 >> 1;
				t1 = (t1 & 1) << 8 ^ t4;
				t4 = _DECRYPT_TAB5[t4];
				// deduce t6 & t5
				unsigned int t6 = scratch[i];
				if(t5)
					t6 = t6 + 0xff & 0x0ff;
				if(t6 < t4)
					t6 += 0x100;
				t6 -= t4;
				t5 += t6 + t4;
				t6 = _DECRYPT_TAB4[t6];
				// feed / advance t3 / t5
				t3 = t3 << 8 | t6;
				t5 >>= 8;
			}

			unsigned int candidate = t3;

			// iterate 6 more times to validate candidate key
			for(; i < 10; ++i)
			{
				unsigned int t4 = _DECRYPT_TAB2[t2] ^ _DECRYPT_TAB3[t1 % 8];
				t2 = t1 >> 1;
				t1 = (t1 & 1) << 8 ^ t4;
				t4 = _DECRYPT_TAB5[t4];
				unsigned int t6 = (((t3 >> 3 ^ t3) >> 1 ^ t3) >> 8 ^ t3) >> 5 & 0xff;
				t3 = t3 << 8 | t6;
				t6 = _DECRYPT_TAB4[t6];
				t5 += t6 + t4;
				if((t5 & 0xff) != scratch[i])
					break;

				t5 >>= 8;
			}

			if(i == 10)
			{
				// do 4 backwards steps of iterating t3 to deduce initial state
				t3 = candidate;
				for(i = 0; i < 4; ++i)
				{
					t1 = t3 & 0xff;
					t3 = t3 >> 8;
					// easy to code, and fast enough brute-force search for byte shifted in
					for(unsigned int j = 0; j < 256; ++j)
					{
						t3 = t3 & 0x1ffff | j << 17;
						unsigned int t6 = (((t3 >> 3 ^ t3) >> 1 ^ t3) >> 8 ^ t3) >> 5 & 0xff;
						if(t6 == t1)
							break;
					}
				}

				unsigned int t4 = (t3 >> 1) - 4;
				for(t5 = 0; t5 < 8; ++t5)
				{
					if((t4 + t5) * 2 + 8 - ((t4 + t5) & 7) == t3)
					{
						title_key.resize(_BLOCK_SIZE);

						title_key[0] = i_try >> 8;
						title_key[1] = i_try & 0xFF;
						title_key[2] = t4 + t5 >> 0 & 0xFF;
						title_key[3] = t4 + t5 >> 8 & 0xFF;
						title_key[4] = t4 + t5 >> 16 & 0xFF;
					}
				}
			}
		}

		if(!title_key.empty())
			for(unsigned int i = 0; i < _BLOCK_SIZE; ++i)
				title_key[i] ^= sector_seed[i];

		return title_key;
	}


	uint8_t reportAGID(bool cprm)
	{
		uint8_t agid;

		for(unsigned int i = 0; i < _AGID_COUNT + 1; ++i)
		{
			std::vector<uint8_t> response_data;
			auto status = cmd_report_key(_sptd, response_data, 0, REPORT_KEY_KeyClass::DVD_CSS_CPPM_CPRM, 0, cprm ? REPORT_KEY_KeyFormat::AGID_CPRM : REPORT_KEY_KeyFormat::AGID);
			if(!status.status_code)
			{
				strip_response_header(response_data);
				agid = ((REPORT_KEY_AGID *)response_data.data())->agid;
				break;
			}

			if(i == _AGID_COUNT)
				throw_line("failed to acquire AGID, SCSI ({})", SPTD::StatusMessage(status));
			else
				// incrementally invalidate all possible AGIDs before retrying
				cmd_report_key(_sptd, response_data, 0, REPORT_KEY_KeyClass::DVD_CSS_CPPM_CPRM, i, REPORT_KEY_KeyFormat::INVALIDATE_AGID);
		}

		return agid;
	}


	bool reportASF()
	{
		std::vector<uint8_t> response_data;
		auto status = cmd_report_key(_sptd, response_data, 0, REPORT_KEY_KeyClass::DVD_CSS_CPPM_CPRM, 0, REPORT_KEY_KeyFormat::ASF);
		if(status.status_code)
			throw_line("failed to read ASF, SCSI ({})", SPTD::StatusMessage(status));
		strip_response_header(response_data);

		return ((REPORT_KEY_ASF *)response_data.data())->asf;
	}


	std::vector<uint8_t> readDiscKey(uint8_t agid)
	{
		std::vector<uint8_t> disc_key;

		auto status = cmd_read_dvd_structure(_sptd, disc_key, 0, 0, READ_DVD_STRUCTURE_Format::DISC_KEY, agid);
		if(status.status_code)
			throw_line("failed to read disc key, SCSI ({})", SPTD::StatusMessage(status));
		strip_response_header(disc_key);

		return disc_key;
	}


	std::vector<uint8_t> reportKey1(uint8_t agid)
	{
		std::vector<uint8_t> response_data;
		auto status = cmd_report_key(_sptd, response_data, 0, REPORT_KEY_KeyClass::DVD_CSS_CPPM_CPRM, agid, REPORT_KEY_KeyFormat::KEY1);
		if(status.status_code)
			throw_line("failed to read key1, SCSI ({})", SPTD::StatusMessage(status));
		strip_response_header(response_data);
		auto k = (REPORT_KEY_Key *)response_data.data();

		std::vector<uint8_t> key1(k->key, k->key + sizeof(k->key));
		std::reverse(key1.begin(), key1.end());

		return key1;
	}


	std::vector<uint8_t> reportChallengeKey(uint8_t agid)
	{
		std::vector<uint8_t> response_data;
		auto status = cmd_report_key(_sptd, response_data, 0, REPORT_KEY_KeyClass::DVD_CSS_CPPM_CPRM, agid, REPORT_KEY_KeyFormat::CHALLENGE_KEY);
		if(status.status_code)
			throw_line("failed to read challenge key, SCSI ({})", SPTD::StatusMessage(status));
		strip_response_header(response_data);
		auto ck = (REPORT_KEY_ChallengeKey *)response_data.data();

		std::vector<uint8_t> challenge_key(ck->challenge, ck->challenge + sizeof(ck->challenge));
		std::reverse(challenge_key.begin(), challenge_key.end());

		return challenge_key;
	}


	void sendChallengeKey(uint8_t agid, const uint8_t *challenge)
	{
		REPORT_KEY_ChallengeKey challenge_key = {};
		for(unsigned int i = 0; i < _CHALLENGE_SIZE; ++i)
			challenge_key.challenge[i] = challenge[_CHALLENGE_SIZE - 1 - i];

		auto status = cmd_send_key(_sptd, (uint8_t *)&challenge_key, sizeof(challenge_key), SEND_KEY_KeyFormat::CHALLENGE_KEY, agid);
		if(status.status_code)
			throw_line("failed to send challenge key, SCSI ({})", SPTD::StatusMessage(status));
	}


	void sendKey2(uint8_t agid, const uint8_t *key)
	{
		REPORT_KEY_Key key2 = {};
		for(unsigned int i = 0; i < _BLOCK_SIZE; ++i)
			key2.key[i] = key[_BLOCK_SIZE - 1 - i];

		auto status = cmd_send_key(_sptd, (uint8_t *)&key2, sizeof(key2), SEND_KEY_KeyFormat::KEY2, agid);
		if(status.status_code)
			throw_line("failed to send key2, SCSI ({})", SPTD::StatusMessage(status));
	}


	std::vector<uint8_t> reportTitleKey(uint8_t agid, uint32_t lba)
	{
		std::vector<uint8_t> title_key;

		std::vector<uint8_t> response_data;
		auto status = cmd_report_key(_sptd, response_data, lba, REPORT_KEY_KeyClass::DVD_CSS_CPPM_CPRM, agid, REPORT_KEY_KeyFormat::TITLE_KEY);
		if(!status.status_code)
		{
			strip_response_header(response_data);
			auto key = (REPORT_KEY_TitleKey *)response_data.data();

			title_key.assign(key->title_key, key->title_key + sizeof(key->title_key));
		}

		return title_key;
	}
};

}
