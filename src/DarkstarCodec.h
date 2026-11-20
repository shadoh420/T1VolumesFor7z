#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Darkstar {

enum Compression : uint8_t {
    kNone = 0,
    kRLE  = 1,
    kLZ   = 2, // Unused in Tribes 1 / Starsiege
    kLZH  = 3
};

enum class DecodeResult {
    kOK = 0,
    kUnsupportedMethod,
    kDataError,
    kUnexpectedEnd
};

const wchar_t *CompressionName(uint8_t compression) noexcept;

// Decode a DarkStar VBLK payload into `out` (resized to `expectedSize`).
DecodeResult DecodeBlock(
    const uint8_t *src,
    size_t srcSize,
    uint32_t expectedSize,
    uint8_t compression,
    std::vector<uint8_t> &out);

// Encode raw bytes using DarkStar RLE (compression = 1).
void EncodeRle(const uint8_t *src, size_t srcSize, std::vector<uint8_t> &out);

} // namespace Darkstar
