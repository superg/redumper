#pragma once



#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#include "subcode.hh"



namespace gpsxre
{

struct TOC
{
	enum class DiscType : uint8_t
	{
		CD_DA = 0x00,
		CD_I  = 0x10,
		CD_XA = 0x20,

		UNKNOWN = 0xFF
	};

	struct CDText
	{
		std::string title;
		std::string performer;
		std::string songwriter;
		std::string composer;
		std::string arranger;
		std::string message;
		std::string closed_info;
		std::string mcn_isrc;
	};

	struct Session
	{
		uint32_t session_number;
		struct Track
		{
			uint32_t track_number;
			int32_t lba_start;
			int32_t lba_end;
			uint8_t control;

			std::vector<int32_t> indices;
			std::string isrc;
			std::vector<CDText> cd_text;

			// supplemental
			uint8_t data_mode;
			bool cdi;
		};
		std::vector<Track> tracks;
	};

	DiscType disc_type;
	std::vector<Session> sessions;

	// supplemental
	std::string mcn;
	std::vector<CDText> cd_text;
	std::vector<uint8_t> cd_text_lang;

	TOC(const std::vector<uint8_t> &toc_buffer, bool full_toc);
	TOC(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start);

	void DeriveINDEX(const TOC &toc);
	void UpdateQ(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start);
	void UpdateMCN(const ChannelQ *subq, uint32_t sectors_count);
	bool UpdateCDTEXT(const std::vector<uint8_t> &cdtext_buffer);
	void GenerateIndex0();

	void Print() const;
	std::ostream &PrintCUE(std::ostream &os, const std::string &image_name, uint32_t cd_text_index = 0) const;

private:
	enum class PackType : uint8_t
	{
		TITLE       = 0x80,
		PERFORMER   = 0x81,
		SONGWRITER  = 0x82,
		COMPOSER    = 0x83,
		ARRANGER    = 0x84,
		MESSAGE     = 0x85,
		DISC_ID     = 0x86,
		GENRE_ID    = 0x87,
		TOC         = 0x88,
		TOC2        = 0x89,
		RESERVED1   = 0x8A,
		RESERVED2   = 0x8B,
		RESERVED3   = 0x8C,
		CLOSED_INFO = 0x8D,
		MCN_ISRC    = 0x8E,
		SIZE_INFO   = 0x8F
	};

	enum class CharacterCode
	{
		ISO_8859_1 = 0x00,
		ASCII      = 0x01,
		MS_JIS     = 0x80  // (Japanese Kanji, double byte characters)
	};

	struct BlockSizeInfo
	{
		uint8_t character_code;
		uint8_t first_track;
		uint8_t last_track;
		uint8_t copyright;
		uint8_t pack_count[16];
		uint8_t sequence_number[8];
		uint8_t language_code[8];
	};

	static const char _ISRC_TABLE[];

	ChannelQ _q_empty;

	void InitTOC(const std::vector<uint8_t> &toc_buffer);
	void InitFullTOC(const std::vector<uint8_t> &toc_buffer);
	void UpdateINDEX(const ChannelQ *subq, uint32_t sectors_count, int32_t lba_start);
	static std::ostream &PrintCDTextCUE(std::ostream &os, const CDText &cd_text, uint32_t indent_level);
	static bool IsTextPack(PackType pack_type);
	CDText *GetCDText(uint8_t index, uint8_t track_number);
};

}
