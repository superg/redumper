module dvd.css;



namespace gpsxre
{

constexpr uint8_t CSS::_DECRYPT_TAB1[] =
{
	0x33, 0x73, 0x3B, 0x26, 0x63, 0x23, 0x6B, 0x76,
	0x3E, 0x7E, 0x36, 0x2B, 0x6E, 0x2E, 0x66, 0x7B,
	0xD3, 0x93, 0xDB, 0x06, 0x43, 0x03, 0x4B, 0x96,
	0xDE, 0x9E, 0xD6, 0x0B, 0x4E, 0x0E, 0x46, 0x9B,
	0x57, 0x17, 0x5F, 0x82, 0xC7, 0x87, 0xCF, 0x12,
	0x5A, 0x1A, 0x52, 0x8F, 0xCA, 0x8A, 0xC2, 0x1F,
	0xD9, 0x99, 0xD1, 0x00, 0x49, 0x09, 0x41, 0x90,
	0xD8, 0x98, 0xD0, 0x01, 0x48, 0x08, 0x40, 0x91,
	0x3D, 0x7D, 0x35, 0x24, 0x6D, 0x2D, 0x65, 0x74,
	0x3C, 0x7C, 0x34, 0x25, 0x6C, 0x2C, 0x64, 0x75,
	0xDD, 0x9D, 0xD5, 0x04, 0x4D, 0x0D, 0x45, 0x94,
	0xDC, 0x9C, 0xD4, 0x05, 0x4C, 0x0C, 0x44, 0x95,
	0x59, 0x19, 0x51, 0x80, 0xC9, 0x89, 0xC1, 0x10,
	0x58, 0x18, 0x50, 0x81, 0xC8, 0x88, 0xC0, 0x11,
	0xD7, 0x97, 0xDF, 0x02, 0x47, 0x07, 0x4F, 0x92,
	0xDA, 0x9A, 0xD2, 0x0F, 0x4A, 0x0A, 0x42, 0x9F,
	0x53, 0x13, 0x5B, 0x86, 0xC3, 0x83, 0xCB, 0x16,
	0x5E, 0x1E, 0x56, 0x8B, 0xCE, 0x8E, 0xC6, 0x1B,
	0xB3, 0xF3, 0xBB, 0xA6, 0xE3, 0xA3, 0xEB, 0xF6,
	0xBE, 0xFE, 0xB6, 0xAB, 0xEE, 0xAE, 0xE6, 0xFB,
	0x37, 0x77, 0x3F, 0x22, 0x67, 0x27, 0x6F, 0x72,
	0x3A, 0x7A, 0x32, 0x2F, 0x6A, 0x2A, 0x62, 0x7F,
	0xB9, 0xF9, 0xB1, 0xA0, 0xE9, 0xA9, 0xE1, 0xF0,
	0xB8, 0xF8, 0xB0, 0xA1, 0xE8, 0xA8, 0xE0, 0xF1,
	0x5D, 0x1D, 0x55, 0x84, 0xCD, 0x8D, 0xC5, 0x14,
	0x5C, 0x1C, 0x54, 0x85, 0xCC, 0x8C, 0xC4, 0x15,
	0xBD, 0xFD, 0xB5, 0xA4, 0xED, 0xAD, 0xE5, 0xF4,
	0xBC, 0xFC, 0xB4, 0xA5, 0xEC, 0xAC, 0xE4, 0xF5,
	0x39, 0x79, 0x31, 0x20, 0x69, 0x29, 0x61, 0x70,
	0x38, 0x78, 0x30, 0x21, 0x68, 0x28, 0x60, 0x71,
	0xB7, 0xF7, 0xBF, 0xA2, 0xE7, 0xA7, 0xEF, 0xF2,
	0xBA, 0xFA, 0xB2, 0xAF, 0xEA, 0xAA, 0xE2, 0xFF
};


constexpr uint8_t CSS::_DECRYPT_TAB2[] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x09, 0x08, 0x0B, 0x0A, 0x0D, 0x0C, 0x0F, 0x0E,
	0x12, 0x13, 0x10, 0x11, 0x16, 0x17, 0x14, 0x15,
	0x1B, 0x1A, 0x19, 0x18, 0x1F, 0x1E, 0x1D, 0x1C,
	0x24, 0x25, 0x26, 0x27, 0x20, 0x21, 0x22, 0x23,
	0x2D, 0x2C, 0x2F, 0x2E, 0x29, 0x28, 0x2B, 0x2A,
	0x36, 0x37, 0x34, 0x35, 0x32, 0x33, 0x30, 0x31,
	0x3F, 0x3E, 0x3D, 0x3C, 0x3B, 0x3A, 0x39, 0x38,
	0x49, 0x48, 0x4B, 0x4A, 0x4D, 0x4C, 0x4F, 0x4E,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x5B, 0x5A, 0x59, 0x58, 0x5F, 0x5E, 0x5D, 0x5C,
	0x52, 0x53, 0x50, 0x51, 0x56, 0x57, 0x54, 0x55,
	0x6D, 0x6C, 0x6F, 0x6E, 0x69, 0x68, 0x6B, 0x6A,
	0x64, 0x65, 0x66, 0x67, 0x60, 0x61, 0x62, 0x63,
	0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78,
	0x76, 0x77, 0x74, 0x75, 0x72, 0x73, 0x70, 0x71,
	0x92, 0x93, 0x90, 0x91, 0x96, 0x97, 0x94, 0x95,
	0x9B, 0x9A, 0x99, 0x98, 0x9F, 0x9E, 0x9D, 0x9C,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x89, 0x88, 0x8B, 0x8A, 0x8D, 0x8C, 0x8F, 0x8E,
	0xB6, 0xB7, 0xB4, 0xB5, 0xB2, 0xB3, 0xB0, 0xB1,
	0xBF, 0xBE, 0xBD, 0xBC, 0xBB, 0xBA, 0xB9, 0xB8,
	0xA4, 0xA5, 0xA6, 0xA7, 0xA0, 0xA1, 0xA2, 0xA3,
	0xAD, 0xAC, 0xAF, 0xAE, 0xA9, 0xA8, 0xAB, 0xAA,
	0xDB, 0xDA, 0xD9, 0xD8, 0xDF, 0xDE, 0xDD, 0xDC,
	0xD2, 0xD3, 0xD0, 0xD1, 0xD6, 0xD7, 0xD4, 0xD5,
	0xC9, 0xC8, 0xCB, 0xCA, 0xCD, 0xCC, 0xCF, 0xCE,
	0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
	0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8,
	0xF6, 0xF7, 0xF4, 0xF5, 0xF2, 0xF3, 0xF0, 0xF1,
	0xED, 0xEC, 0xEF, 0xEE, 0xE9, 0xE8, 0xEB, 0xEA,
	0xE4, 0xE5, 0xE6, 0xE7, 0xE0, 0xE1, 0xE2, 0xE3
};


constexpr uint8_t CSS::_DECRYPT_TAB3[] =
{
	0x00, 0x24, 0x49, 0x6D, 0x92, 0xB6, 0xDB, 0xFF
};


constexpr uint8_t CSS::_DECRYPT_TAB4[] =
{
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0,
	0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
	0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
	0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC,
	0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
	0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
	0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6,
	0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
	0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
	0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
	0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9,
	0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
	0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
	0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
	0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
	0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
	0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
	0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
	0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
	0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
	0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF,
	0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};


constexpr uint8_t CSS::_DECRYPT_TAB5[] =
{
	0xFF, 0x7F, 0xBF, 0x3F, 0xDF, 0x5F, 0x9F, 0x1F,
	0xEF, 0x6F, 0xAF, 0x2F, 0xCF, 0x4F, 0x8F, 0x0F,
	0xF7, 0x77, 0xB7, 0x37, 0xD7, 0x57, 0x97, 0x17,
	0xE7, 0x67, 0xA7, 0x27, 0xC7, 0x47, 0x87, 0x07,
	0xFB, 0x7B, 0xBB, 0x3B, 0xDB, 0x5B, 0x9B, 0x1B,
	0xEB, 0x6B, 0xAB, 0x2B, 0xCB, 0x4B, 0x8B, 0x0B,
	0xF3, 0x73, 0xB3, 0x33, 0xD3, 0x53, 0x93, 0x13,
	0xE3, 0x63, 0xA3, 0x23, 0xC3, 0x43, 0x83, 0x03,
	0xFD, 0x7D, 0xBD, 0x3D, 0xDD, 0x5D, 0x9D, 0x1D,
	0xED, 0x6D, 0xAD, 0x2D, 0xCD, 0x4D, 0x8D, 0x0D,
	0xF5, 0x75, 0xB5, 0x35, 0xD5, 0x55, 0x95, 0x15,
	0xE5, 0x65, 0xA5, 0x25, 0xC5, 0x45, 0x85, 0x05,
	0xF9, 0x79, 0xB9, 0x39, 0xD9, 0x59, 0x99, 0x19,
	0xE9, 0x69, 0xA9, 0x29, 0xC9, 0x49, 0x89, 0x09,
	0xF1, 0x71, 0xB1, 0x31, 0xD1, 0x51, 0x91, 0x11,
	0xE1, 0x61, 0xA1, 0x21, 0xC1, 0x41, 0x81, 0x01,
	0xFE, 0x7E, 0xBE, 0x3E, 0xDE, 0x5E, 0x9E, 0x1E,
	0xEE, 0x6E, 0xAE, 0x2E, 0xCE, 0x4E, 0x8E, 0x0E,
	0xF6, 0x76, 0xB6, 0x36, 0xD6, 0x56, 0x96, 0x16,
	0xE6, 0x66, 0xA6, 0x26, 0xC6, 0x46, 0x86, 0x06,
	0xFA, 0x7A, 0xBA, 0x3A, 0xDA, 0x5A, 0x9A, 0x1A,
	0xEA, 0x6A, 0xAA, 0x2A, 0xCA, 0x4A, 0x8A, 0x0A,
	0xF2, 0x72, 0xB2, 0x32, 0xD2, 0x52, 0x92, 0x12,
	0xE2, 0x62, 0xA2, 0x22, 0xC2, 0x42, 0x82, 0x02,
	0xFC, 0x7C, 0xBC, 0x3C, 0xDC, 0x5C, 0x9C, 0x1C,
	0xEC, 0x6C, 0xAC, 0x2C, 0xCC, 0x4C, 0x8C, 0x0C,
	0xF4, 0x74, 0xB4, 0x34, 0xD4, 0x54, 0x94, 0x14,
	0xE4, 0x64, 0xA4, 0x24, 0xC4, 0x44, 0x84, 0x04,
	0xF8, 0x78, 0xB8, 0x38, 0xD8, 0x58, 0x98, 0x18,
	0xE8, 0x68, 0xA8, 0x28, 0xC8, 0x48, 0x88, 0x08,
	0xF0, 0x70, 0xB0, 0x30, 0xD0, 0x50, 0x90, 0x10,
	0xE0, 0x60, 0xA0, 0x20, 0xC0, 0x40, 0x80, 0x00
};


constexpr uint8_t CSS::_CRYPT_TAB0[] =
{
	0xB7, 0xF4, 0x82, 0x57, 0xDA, 0x4D, 0xDB, 0xE2,
	0x2F, 0x52, 0x1A, 0xA8, 0x68, 0x5A, 0x8A, 0xFF,
	0xFB, 0x0E, 0x6D, 0x35, 0xF7, 0x5C, 0x76, 0x12,
	0xCE, 0x25, 0x79, 0x29, 0x39, 0x62, 0x08, 0x24,
	0xA5, 0x85, 0x7B, 0x56, 0x01, 0x23, 0x68, 0xCF,
	0x0A, 0xE2, 0x5A, 0xED, 0x3D, 0x59, 0xB0, 0xA9,
	0xB0, 0x2C, 0xF2, 0xB8, 0xEF, 0x32, 0xA9, 0x40,
	0x80, 0x71, 0xAF, 0x1E, 0xDE, 0x8F, 0x58, 0x88,
	0xB8, 0x3A, 0xD0, 0xFC, 0xC4, 0x1E, 0xB5, 0xA0,
	0xBB, 0x3B, 0x0F, 0x01, 0x7E, 0x1F, 0x9F, 0xD9,
	0xAA, 0xB8, 0x3D, 0x9D, 0x74, 0x1E, 0x25, 0xDB,
	0x37, 0x56, 0x8F, 0x16, 0xBA, 0x49, 0x2B, 0xAC,
	0xD0, 0xBD, 0x95, 0x20, 0xBE, 0x7A, 0x28, 0xD0,
	0x51, 0x64, 0x63, 0x1C, 0x7F, 0x66, 0x10, 0xBB,
	0xC4, 0x56, 0x1A, 0x04, 0x6E, 0x0A, 0xEC, 0x9C,
	0xD6, 0xE8, 0x9A, 0x7A, 0xCF, 0x8C, 0xDB, 0xB1,
	0xEF, 0x71, 0xDE, 0x31, 0xFF, 0x54, 0x3E, 0x5E,
	0x07, 0x69, 0x96, 0xB0, 0xCF, 0xDD, 0x9E, 0x47,
	0xC7, 0x96, 0x8F, 0xE4, 0x2B, 0x59, 0xC6, 0xEE,
	0xB9, 0x86, 0x9A, 0x64, 0x84, 0x72, 0xE2, 0x5B,
	0xA2, 0x96, 0x58, 0x99, 0x50, 0x03, 0xF5, 0x38,
	0x4D, 0x02, 0x7D, 0xE7, 0x7D, 0x75, 0xA7, 0xB8,
	0x67, 0x87, 0x84, 0x3F, 0x1D, 0x11, 0xE5, 0xFC,
	0x1E, 0xD3, 0x83, 0x16, 0xA5, 0x29, 0xF6, 0xC7,
	0x15, 0x61, 0x29, 0x1A, 0x43, 0x4F, 0x9B, 0xAF,
	0xC5, 0x87, 0x34, 0x6C, 0x0F, 0x3B, 0xA8, 0x1D,
	0x45, 0x58, 0x25, 0xDC, 0xA8, 0xA3, 0x3B, 0xD1,
	0x79, 0x1B, 0x48, 0xF2, 0xE9, 0x93, 0x1F, 0xFC,
	0xDB, 0x2A, 0x90, 0xA9, 0x8A, 0x3D, 0x39, 0x18,
	0xA3, 0x8E, 0x58, 0x6C, 0xE0, 0x12, 0xBB, 0x25,
	0xCD, 0x71, 0x22, 0xA2, 0x64, 0xC6, 0xE7, 0xFB,
	0xAD, 0x94, 0x77, 0x04, 0x9A, 0x39, 0xCF, 0x7C
};


constexpr uint8_t CSS::_CRYPT_TAB1[] =
{
	0x8C, 0x47, 0xB0, 0xE1, 0xEB, 0xFC, 0xEB, 0x56,
	0x10, 0xE5, 0x2C, 0x1A, 0x5D, 0xEF, 0xBE, 0x4F,
	0x08, 0x75, 0x97, 0x4B, 0x0E, 0x25, 0x8E, 0x6E,
	0x39, 0x5A, 0x87, 0x53, 0xC4, 0x1F, 0xF4, 0x5C,
	0x4E, 0xE6, 0x99, 0x30, 0xE0, 0x42, 0x88, 0xAB,
	0xE5, 0x85, 0xBC, 0x8F, 0xD8, 0x3C, 0x54, 0xC9,
	0x53, 0x47, 0x18, 0xD6, 0x06, 0x5B, 0x41, 0x2C,
	0x67, 0x1E, 0x41, 0x74, 0x33, 0xE2, 0xB4, 0xE0,
	0x23, 0x29, 0x42, 0xEA, 0x55, 0x0F, 0x25, 0xB4,
	0x24, 0x2C, 0x99, 0x13, 0xEB, 0x0A, 0x0B, 0xC9,
	0xF9, 0x63, 0x67, 0x43, 0x2D, 0xC7, 0x7D, 0x07,
	0x60, 0x89, 0xD1, 0xCC, 0xE7, 0x94, 0x77, 0x74,
	0x9B, 0x7E, 0xD7, 0xE6, 0xFF, 0xBB, 0x68, 0x14,
	0x1E, 0xA3, 0x25, 0xDE, 0x3A, 0xA3, 0x54, 0x7B,
	0x87, 0x9D, 0x50, 0xCA, 0x27, 0xC3, 0xA4, 0x50,
	0x91, 0x27, 0xD4, 0xB0, 0x82, 0x41, 0x97, 0x79,
	0x94, 0x82, 0xAC, 0xC7, 0x8E, 0xA5, 0x4E, 0xAA,
	0x78, 0x9E, 0xE0, 0x42, 0xBA, 0x28, 0xEA, 0xB7,
	0x74, 0xAD, 0x35, 0xDA, 0x92, 0x60, 0x7E, 0xD2,
	0x0E, 0xB9, 0x24, 0x5E, 0x39, 0x4F, 0x5E, 0x63,
	0x09, 0xB5, 0xFA, 0xBF, 0xF1, 0x22, 0x55, 0x1C,
	0xE2, 0x25, 0xDB, 0xC5, 0xD8, 0x50, 0x03, 0x98,
	0xC4, 0xAC, 0x2E, 0x11, 0xB4, 0x38, 0x4D, 0xD0,
	0xB9, 0xFC, 0x2D, 0x3C, 0x08, 0x04, 0x5A, 0xEF,
	0xCE, 0x32, 0xFB, 0x4C, 0x92, 0x1E, 0x4B, 0xFB,
	0x1A, 0xD0, 0xE2, 0x3E, 0xDA, 0x6E, 0x7C, 0x4D,
	0x56, 0xC3, 0x3F, 0x42, 0xB1, 0x3A, 0x23, 0x4D,
	0x6E, 0x84, 0x56, 0x68, 0xF4, 0x0E, 0x03, 0x64,
	0xD0, 0xA9, 0x92, 0x2F, 0x8B, 0xBC, 0x39, 0x9C,
	0xAC, 0x09, 0x5E, 0xEE, 0xE5, 0x97, 0xBF, 0xA5,
	0xCE, 0xFA, 0x28, 0x2C, 0x6D, 0x4F, 0xEF, 0x77,
	0xAA, 0x1B, 0x79, 0x8E, 0x97, 0xB4, 0xC3, 0xF4
};


constexpr uint8_t CSS::_CRYPT_TAB2[] =
{
	0xB7, 0x75, 0x81, 0xD5, 0xDC, 0xCA, 0xDE, 0x66,
	0x23, 0xDF, 0x15, 0x26, 0x62, 0xD1, 0x83, 0x77,
	0xE3, 0x97, 0x76, 0xAF, 0xE9, 0xC3, 0x6B, 0x8E,
	0xDA, 0xB0, 0x6E, 0xBF, 0x2B, 0xF1, 0x19, 0xB4,
	0x95, 0x34, 0x48, 0xE4, 0x37, 0x94, 0x5D, 0x7B,
	0x36, 0x5F, 0x65, 0x53, 0x07, 0xE2, 0x89, 0x11,
	0x98, 0x85, 0xD9, 0x12, 0xC1, 0x9D, 0x84, 0xEC,
	0xA4, 0xD4, 0x88, 0xB8, 0xFC, 0x2C, 0x79, 0x28,
	0xD8, 0xDB, 0xB3, 0x1E, 0xA2, 0xF9, 0xD0, 0x44,
	0xD7, 0xD6, 0x60, 0xEF, 0x14, 0xF4, 0xF6, 0x31,
	0xD2, 0x41, 0x46, 0x67, 0x0A, 0xE1, 0x58, 0x27,
	0x43, 0xA3, 0xF8, 0xE0, 0xC8, 0xBA, 0x5A, 0x5C,
	0x80, 0x6C, 0xC6, 0xF2, 0xE8, 0xAD, 0x7D, 0x04,
	0x0D, 0xB9, 0x3C, 0xC2, 0x25, 0xBD, 0x49, 0x63,
	0x8C, 0x9F, 0x51, 0xCE, 0x20, 0xC5, 0xA1, 0x50,
	0x92, 0x2D, 0xDD, 0xBC, 0x8D, 0x4F, 0x9A, 0x71,
	0x2F, 0x30, 0x1D, 0x73, 0x39, 0x13, 0xFB, 0x1A,
	0xCB, 0x24, 0x59, 0xFE, 0x05, 0x96, 0x57, 0x0F,
	0x1F, 0xCF, 0x54, 0xBE, 0xF5, 0x06, 0x1B, 0xB2,
	0x6D, 0xD3, 0x4D, 0x32, 0x56, 0x21, 0x33, 0x0B,
	0x52, 0xE7, 0xAB, 0xEB, 0xA6, 0x74, 0x00, 0x4C,
	0xB1, 0x7F, 0x82, 0x99, 0x87, 0x0E, 0x5E, 0xC0,
	0x8F, 0xEE, 0x6F, 0x55, 0xF3, 0x7E, 0x08, 0x90,
	0xFA, 0xB6, 0x64, 0x70, 0x47, 0x4A, 0x17, 0xA7,
	0xB5, 0x40, 0x8A, 0x38, 0xE5, 0x68, 0x3E, 0x8B,
	0x69, 0xAA, 0x9B, 0x42, 0xA5, 0x10, 0x01, 0x35,
	0xFD, 0x61, 0x9E, 0xE6, 0x16, 0x9C, 0x86, 0xED,
	0xCD, 0x2E, 0xFF, 0xC4, 0x5B, 0xA0, 0xAE, 0xCC,
	0x4B, 0x3B, 0x03, 0xBB, 0x1C, 0x2A, 0xAC, 0x0C,
	0x3F, 0x93, 0xC7, 0x72, 0x7A, 0x09, 0x22, 0x3D,
	0x45, 0x78, 0xA9, 0xA8, 0xEA, 0xC9, 0x6A, 0xF7,
	0x29, 0x91, 0xF0, 0x02, 0x18, 0x3A, 0x4E, 0x7C
};


constexpr uint8_t CSS::_CRYPT_TAB3[] =
{
	0x73, 0x51, 0x95, 0xE1, 0x12, 0xE4, 0xC0, 0x58,
	0xEE, 0xF2, 0x08, 0x1B, 0xA9, 0xFA, 0x98, 0x4C,
	0xA7, 0x33, 0xE2, 0x1B, 0xA7, 0x6D, 0xF5, 0x30,
	0x97, 0x1D, 0xF3, 0x02, 0x60, 0x5A, 0x82, 0x0F,
	0x91, 0xD0, 0x9C, 0x10, 0x39, 0x7A, 0x83, 0x85,
	0x3B, 0xB2, 0xB8, 0xAE, 0x0C, 0x09, 0x52, 0xEA,
	0x1C, 0xE1, 0x8D, 0x66, 0x4F, 0xF3, 0xDA, 0x92,
	0x29, 0xB9, 0xD5, 0xC5, 0x77, 0x47, 0x22, 0x53,
	0x14, 0xF7, 0xAF, 0x22, 0x64, 0xDF, 0xC6, 0x72,
	0x12, 0xF3, 0x75, 0xDA, 0xD7, 0xD7, 0xE5, 0x02,
	0x9E, 0xED, 0xDA, 0xDB, 0x4C, 0x47, 0xCE, 0x91,
	0x06, 0x06, 0x6D, 0x55, 0x8B, 0x19, 0xC9, 0xEF,
	0x8C, 0x80, 0x1A, 0x0E, 0xEE, 0x4B, 0xAB, 0xF2,
	0x08, 0x5C, 0xE9, 0x37, 0x26, 0x5E, 0x9A, 0x90,
	0x00, 0xF3, 0x0D, 0xB2, 0xA6, 0xA3, 0xF7, 0x26,
	0x17, 0x48, 0x88, 0xC9, 0x0E, 0x2C, 0xC9, 0x02,
	0xE7, 0x18, 0x05, 0x4B, 0xF3, 0x39, 0xE1, 0x20,
	0x02, 0x0D, 0x40, 0xC7, 0xCA, 0xB9, 0x48, 0x30,
	0x57, 0x67, 0xCC, 0x06, 0xBF, 0xAC, 0x81, 0x08,
	0x24, 0x7A, 0xD4, 0x8B, 0x19, 0x8E, 0xAC, 0xB4,
	0x5A, 0x0F, 0x73, 0x13, 0xAC, 0x9E, 0xDA, 0xB6,
	0xB8, 0x96, 0x5B, 0x60, 0x88, 0xE1, 0x81, 0x3F,
	0x07, 0x86, 0x37, 0x2D, 0x79, 0x14, 0x52, 0xEA,
	0x73, 0xDF, 0x3D, 0x09, 0xC8, 0x25, 0x48, 0xD8,
	0x75, 0x60, 0x9A, 0x08, 0x27, 0x4A, 0x2C, 0xB9,
	0xA8, 0x8B, 0x8A, 0x73, 0x62, 0x37, 0x16, 0x02,
	0xBD, 0xC1, 0x0E, 0x56, 0x54, 0x3E, 0x14, 0x5F,
	0x8C, 0x8F, 0x6E, 0x75, 0x1C, 0x07, 0x39, 0x7B,
	0x4B, 0xDB, 0xD3, 0x4B, 0x1E, 0xC8, 0x7E, 0xFE,
	0x3E, 0x72, 0x16, 0x83, 0x7D, 0xEE, 0xF5, 0xCA,
	0xC5, 0x18, 0xF9, 0xD8, 0x68, 0xAB, 0x38, 0x85,
	0xA8, 0xF0, 0xA1, 0x73, 0x9F, 0x5D, 0x19, 0x0B,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x33, 0x72, 0x39, 0x25, 0x67, 0x26, 0x6D, 0x71,
	0x36, 0x77, 0x3C, 0x20, 0x62, 0x23, 0x68, 0x74,
	0xC3, 0x82, 0xC9, 0x15, 0x57, 0x16, 0x5D, 0x81
};


constexpr uint8_t CSS::_PERM_VARIANT[][32] =
{
	{
		 0,  1,  2,  3,  4,  5,  6,  7,
		 8,  9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31
	},
	{
		10,  8, 14, 12, 11,  9, 15, 13,
		26, 24, 30, 28, 27, 25, 31, 29,
		 2,  0,  6,  4,  3,  1,  7,  5,
		18, 16, 22, 20, 19, 17, 23, 21
	},
	{
		18, 26, 22, 30,  2, 10,  6, 14,
		16, 24, 20, 28,  0,  8,  4, 12,
		19, 27, 23, 31,  3, 11,  7, 15,
		17, 25, 21, 29,  1,  9,  5, 13
	}
};


constexpr uint8_t CSS::_PERM_CHALLENGE[][_CHALLENGE_SIZE] =
{
	{ 1, 3, 0, 7, 5, 2, 9, 6, 4, 8 },
	{ 6, 1, 9, 3, 8, 5, 7, 4, 0, 2 },
	{ 4, 0, 3, 5, 7, 2, 8, 6, 1, 9 }
};


constexpr uint8_t CSS::_VARIANTS[] =
{
	0xB7, 0x74, 0x85, 0xD0, 0xCC, 0xDB, 0xCA, 0x73,
	0x03, 0xFE, 0x31, 0x03, 0x52, 0xE0, 0xB7, 0x42,
	0x63, 0x16, 0xF2, 0x2A, 0x79, 0x52, 0xFF, 0x1B,
	0x7A, 0x11, 0xCA, 0x1A, 0x9B, 0x40, 0xAD, 0x01
};


constexpr uint8_t CSS::_SECRET[] =
{
	0x55, 0xD6, 0xC4, 0xC5, 0x28
};


constexpr uint8_t CSS::_PLAYER_KEYS[][_BLOCK_SIZE] =
{
	{ 0x01, 0xAF, 0xE3, 0x12, 0x80 },
	{ 0x12, 0x11, 0xCA, 0x04, 0x3B },
	{ 0x14, 0x0C, 0x9E, 0xD0, 0x09 },
	{ 0x14, 0x71, 0x35, 0xBA, 0xE2 },
	{ 0x1A, 0xA4, 0x33, 0x21, 0xA6 },
	{ 0x26, 0xEC, 0xC4, 0xA7, 0x4E },
	{ 0x2C, 0xB2, 0xC1, 0x09, 0xEE },
	{ 0x2F, 0x25, 0x9E, 0x96, 0xDD },
	{ 0x33, 0x2F, 0x49, 0x6C, 0xE0 },
	{ 0x35, 0x5B, 0xC1, 0x31, 0x0F },
	{ 0x36, 0x67, 0xB2, 0xE3, 0x85 },
	{ 0x39, 0x3D, 0xF1, 0xF1, 0xBD },
	{ 0x3B, 0x31, 0x34, 0x0D, 0x91 },
	{ 0x45, 0xED, 0x28, 0xEB, 0xD3 },
	{ 0x48, 0xB7, 0x6C, 0xCE, 0x69 },
	{ 0x4B, 0x65, 0x0D, 0xC1, 0xEE },
	{ 0x4C, 0xBB, 0xF5, 0x5B, 0x23 },
	{ 0x51, 0x67, 0x67, 0xC5, 0xE0 },
	{ 0x53, 0x94, 0xE1, 0x75, 0xBF },
	{ 0x57, 0x2C, 0x8B, 0x31, 0xAE },
	{ 0x63, 0xDB, 0x4C, 0x5B, 0x4A },
	{ 0x7B, 0x1E, 0x5E, 0x2B, 0x57 },
	{ 0x85, 0xF3, 0x85, 0xA0, 0xE0 },
	{ 0xAB, 0x1E, 0xE7, 0x7B, 0x72 },
	{ 0xAB, 0x36, 0xE3, 0xEB, 0x76 },
	{ 0xB1, 0xB8, 0xF9, 0x38, 0x03 },
	{ 0xB8, 0x5D, 0xD8, 0x53, 0xBD },
	{ 0xBF, 0x92, 0xC3, 0xB0, 0xE2 },
	{ 0xCF, 0x1A, 0xB2, 0xF8, 0x0A },
	{ 0xEC, 0xA0, 0xCF, 0xB3, 0xFF },
	{ 0xFC, 0x95, 0xA9, 0x87, 0x35 }
};

}
