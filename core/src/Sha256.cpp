#include "sirius/Sha256.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace sirius
{
namespace
{

/// First 32 bits of the fractional parts of the cube roots of the first 64
/// primes — the SHA-256 round constants (FIPS 180-4 §4.2.2).
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };

constexpr std::array<std::uint32_t, 8> kInitialHash = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };

constexpr std::uint32_t rotr (std::uint32_t x, std::uint32_t n) noexcept
{
    return (x >> n) | (x << (32u - n));
}

void compress (std::array<std::uint32_t, 8>& h,
               const std::uint8_t (&block)[64]) noexcept
{
    std::array<std::uint32_t, 64> w {};
    for (int i = 0; i < 16; ++i)
        w[std::size_t (i)] = (std::uint32_t (block[i * 4    ]) << 24)
                           | (std::uint32_t (block[i * 4 + 1]) << 16)
                           | (std::uint32_t (block[i * 4 + 2]) <<  8)
                           |  std::uint32_t (block[i * 4 + 3]);

    for (int i = 16; i < 64; ++i)
    {
        const auto s0 = rotr (w[std::size_t (i - 15)],  7)
                      ^ rotr (w[std::size_t (i - 15)], 18)
                      ^      (w[std::size_t (i - 15)] >>  3);
        const auto s1 = rotr (w[std::size_t (i -  2)], 17)
                      ^ rotr (w[std::size_t (i -  2)], 19)
                      ^      (w[std::size_t (i -  2)] >> 10);
        w[std::size_t (i)] = w[std::size_t (i - 16)] + s0
                           + w[std::size_t (i -  7)] + s1;
    }

    auto a = h[0]; auto b = h[1]; auto c = h[2]; auto d = h[3];
    auto e = h[4]; auto f = h[5]; auto g = h[6]; auto hh = h[7];

    for (int i = 0; i < 64; ++i)
    {
        const auto S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
        const auto ch = (e & f) ^ (~e & g);
        const auto t1 = hh + S1 + ch + kRoundConstants[std::size_t (i)]
                      + w[std::size_t (i)];
        const auto S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
        const auto mj = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = S0 + mj;

        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace

std::string sha256Hex (std::span<const std::byte> data)
{
    auto h = kInitialHash;

    const auto* bytes = reinterpret_cast<const std::uint8_t*> (data.data());
    const auto length = data.size();

    // Process full 64-byte blocks.
    std::size_t offset = 0;
    while (offset + 64 <= length)
    {
        std::uint8_t block[64];
        std::memcpy (block, bytes + offset, 64);
        compress (h, block);
        offset += 64;
    }

    // Final block(s): pad with 0x80, then zeros, then 64-bit big-endian
    // length in bits. The padded message may need two final blocks if
    // the residual + 0x80 + 8-byte length exceeds 64.
    std::uint8_t pad[128] {};
    const std::size_t residual = length - offset;
    std::memcpy (pad, bytes + offset, residual);
    pad[residual] = 0x80u;

    const std::size_t padBlocks = (residual + 1 + 8 <= 64) ? 1u : 2u;
    const std::uint64_t bitLength = std::uint64_t (length) * 8u;
    const std::size_t lengthOffset = padBlocks * 64u - 8u;
    for (int i = 0; i < 8; ++i)
        pad[lengthOffset + std::size_t (i)] =
            std::uint8_t ((bitLength >> ((7 - i) * 8)) & 0xffu);

    {
        std::uint8_t block[64];
        std::memcpy (block, pad, 64);
        compress (h, block);
    }
    if (padBlocks == 2)
    {
        std::uint8_t block[64];
        std::memcpy (block, pad + 64, 64);
        compress (h, block);
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize (64);
    for (int i = 0; i < 8; ++i)
    {
        const auto word = h[std::size_t (i)];
        out[std::size_t (i * 8 + 0)] = kHex[(word >> 28) & 0xfu];
        out[std::size_t (i * 8 + 1)] = kHex[(word >> 24) & 0xfu];
        out[std::size_t (i * 8 + 2)] = kHex[(word >> 20) & 0xfu];
        out[std::size_t (i * 8 + 3)] = kHex[(word >> 16) & 0xfu];
        out[std::size_t (i * 8 + 4)] = kHex[(word >> 12) & 0xfu];
        out[std::size_t (i * 8 + 5)] = kHex[(word >>  8) & 0xfu];
        out[std::size_t (i * 8 + 6)] = kHex[(word >>  4) & 0xfu];
        out[std::size_t (i * 8 + 7)] = kHex[ word        & 0xfu];
    }
    return out;
}

} // namespace sirius
