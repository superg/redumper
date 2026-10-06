module;
#include <algorithm>
#include <array>
#include <cstdint>

export module hash.cmac;

import hash.aes128;



namespace gpsxre
{

export class CMAC
{
public:
    explicit CMAC(const std::array<uint8_t, AES128::BLOCK_SIZE> &key)
        : _aes(key)
    {
        std::array<uint8_t, AES128::BLOCK_SIZE> zero_block;
        zero_block.fill(0);
        auto l = _aes.encryptBlock(zero_block);
        _subkey1 = doubleBlock(l);
        _subkey2 = doubleBlock(_subkey1);
        reset();
    }

    CMAC &reset()
    {
        _chain.fill(0);
        _tail.fill(0);
        _tailSize = 0;
        return *this;
    }

    CMAC &update(const uint8_t *data, uint64_t size)
    {
        while(size != 0)
        {
            if(_tailSize == AES128::BLOCK_SIZE)
            {
                updateBlock(_tail);
                _tail.fill(0);
                _tailSize = 0;
            }

            uint32_t count = (uint32_t)std::min<uint64_t>(AES128::BLOCK_SIZE - _tailSize, size);
            std::copy_n(data, count, _tail.begin() + _tailSize);
            data += count;
            size -= count;
            _tailSize += count;
        }
        return *this;
    }

    std::array<uint8_t, AES128::BLOCK_SIZE> final()
    {
        auto last = _tail;
        if(_tailSize == AES128::BLOCK_SIZE)
        {
            for(uint32_t i = 0; i < AES128::BLOCK_SIZE; ++i)
                last[i] ^= _subkey1[i];
        }
        else
        {
            last[_tailSize] = 0x80;
            for(uint32_t i = 0; i < AES128::BLOCK_SIZE; ++i)
                last[i] ^= _subkey2[i];
        }

        for(uint32_t i = 0; i < AES128::BLOCK_SIZE; ++i)
            last[i] ^= _chain[i];

        auto result = _aes.encryptBlock(last);
        reset();
        return result;
    }

private:
    AES128 _aes;
    std::array<uint8_t, AES128::BLOCK_SIZE> _subkey1;
    std::array<uint8_t, AES128::BLOCK_SIZE> _subkey2;
    std::array<uint8_t, AES128::BLOCK_SIZE> _chain;
    std::array<uint8_t, AES128::BLOCK_SIZE> _tail;
    uint32_t _tailSize;

    static std::array<uint8_t, AES128::BLOCK_SIZE> doubleBlock(const std::array<uint8_t, AES128::BLOCK_SIZE> &block)
    {
        std::array<uint8_t, AES128::BLOCK_SIZE> doubled;
        uint8_t carry = 0;
        for(uint32_t i = AES128::BLOCK_SIZE; i > 0; --i)
        {
            doubled[i - 1] = (uint8_t)((block[i - 1] << 1) | carry);
            carry = block[i - 1] >> 7;
        }
        if(carry)
            doubled.back() ^= 0x87;
        return doubled;
    }

    void updateBlock(const std::array<uint8_t, AES128::BLOCK_SIZE> &block)
    {
        auto input = block;
        for(uint32_t i = 0; i < AES128::BLOCK_SIZE; ++i)
            input[i] ^= _chain[i];
        _chain = _aes.encryptBlock(input);
    }
};

}
