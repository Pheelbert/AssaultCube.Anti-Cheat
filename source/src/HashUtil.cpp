#include "cube.h"

#include "HashUtil.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
#include <cstdint>

namespace PhantiCheat {

    class SHA256 {
    public:
        SHA256();
        void update(const unsigned char* data, size_t length);
        void update(std::istream& is);
        std::string final();

    private:
        static const size_t BlockSize = 64;
        void transform(const unsigned char* chunk);
        static uint32_t rotr(uint32_t x, uint32_t n);
        static uint32_t choose(uint32_t e, uint32_t f, uint32_t g);
        static uint32_t majority(uint32_t a, uint32_t b, uint32_t c);
        static uint32_t sig0(uint32_t x);
        static uint32_t sig1(uint32_t x);
        static uint32_t ep0(uint32_t x);
        static uint32_t ep1(uint32_t x);

        uint64_t bitLength;
        unsigned char buffer[BlockSize];
        size_t bufferSize;
        uint32_t state[8];
    };

    // Constants defined in the SHA-256 standard
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    SHA256::SHA256() : bitLength(0), bufferSize(0) {
        // Initial hash values
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
    }

    void SHA256::update(const unsigned char* data, size_t length) {
        size_t i = 0;

        if (bufferSize > 0) {
            while (i < length && bufferSize < BlockSize) {
                buffer[bufferSize++] = data[i++];
            }
            if (bufferSize == BlockSize) {
                transform(buffer);
                bitLength += BlockSize * 8;
                bufferSize = 0;
            }
        }

        for (; i + BlockSize <= length; i += BlockSize) {
            transform(&data[i]);
            bitLength += BlockSize * 8;
        }

        while (i < length) {
            buffer[bufferSize++] = data[i++];
        }
    }

    void SHA256::update(std::istream& is) {
        char chunk[BlockSize];
        while (is.read(chunk, BlockSize)) {
            update(reinterpret_cast<unsigned char*>(chunk), BlockSize);
        }
        update(reinterpret_cast<unsigned char*>(chunk), is.gcount());
    }

    std::string SHA256::final() {
        bitLength += bufferSize * 8;

        buffer[bufferSize++] = 0x80;

        if (bufferSize > 56) {
            while (bufferSize < BlockSize) {
                buffer[bufferSize++] = 0x00;
            }
            transform(buffer);
            bufferSize = 0;
        }

        while (bufferSize < 56) {
            buffer[bufferSize++] = 0x00;
        }

        for (int i = 7; i >= 0; --i) {
            buffer[bufferSize++] = (bitLength >> (i * 8)) & 0xFF;
        }

        transform(buffer);

        std::ostringstream result;
        for (int i = 0; i < 8; ++i) {
            result << std::hex << std::setw(8) << std::setfill('0') << state[i];
        }

        return result.str();
    }

    void SHA256::transform(const unsigned char* chunk) {
        uint32_t w[64];
        uint32_t a, b, c, d, e, f, g, h;

        for (int i = 0; i < 16; ++i) {
            w[i]  = (chunk[i * 4] << 24);
            w[i] |= (chunk[i * 4 + 1] << 16);
            w[i] |= (chunk[i * 4 + 2] << 8);
            w[i] |= (chunk[i * 4 + 3]);
        }

        for (int i = 16; i < 64; ++i) {
            w[i] = sig1(w[i - 2]) + w[i - 7] + sig0(w[i - 15]) + w[i - 16];
        }

        a = state[0];
        b = state[1];
        c = state[2];
        d = state[3];
        e = state[4];
        f = state[5];
        g = state[6];
        h = state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t temp1 = h + ep1(e) + choose(e, f, g) + K[i] + w[i];
            uint32_t temp2 = ep0(a) + majority(a, b, c);

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    uint32_t SHA256::rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    uint32_t SHA256::choose(uint32_t e, uint32_t f, uint32_t g) {
        return (e & f) ^ (~e & g);
    }

    uint32_t SHA256::majority(uint32_t a, uint32_t b, uint32_t c) {
        return (a & b) ^ (a & c) ^ (b & c);
    }

    uint32_t SHA256::sig0(uint32_t x) {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }

    uint32_t SHA256::sig1(uint32_t x) {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }

    uint32_t SHA256::ep0(uint32_t x) {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }

    uint32_t SHA256::ep1(uint32_t x) {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }

    std::string HashUtil::calculateHash(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Unable to open file: " + filePath);
        }

        SHA256 sha256;
        sha256.update(file);

        return sha256.final();
    }

    std::string HashUtil::calculateHash(const unsigned char* data, size_t length) {
        SHA256 sha256;
        sha256.update(data, length);

        return sha256.final();
    }

}
