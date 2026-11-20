#include "DarkstarCodec.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace Darkstar {

const wchar_t *CompressionName(uint8_t compression) noexcept {
    switch (compression) {
        case kNone: return L"Store";
        case kRLE:  return L"RLE";
        case kLZ:   return L"LZ";
        case kLZH:  return L"LZH";
        default:    return L"Unknown";
    }
}

namespace {

DecodeResult DecodeRle(
    const uint8_t *src,
    size_t srcSize,
    uint32_t expectedSize,
    std::vector<uint8_t> &out)
{
    out.clear();
    out.reserve(expectedSize);

    size_t pos = 0;
    while (out.size() < expectedSize) {
        if (pos >= srcSize) {
            return DecodeResult::kUnexpectedEnd;
        }
        const uint8_t control = src[pos++];
        const size_t count = control & 0x7Fu;
        if (count == 0) {
            // Avoid infinite loop on corrupt zero-length packet.
            return DecodeResult::kDataError;
        }
        if (control & 0x80u) {
            if (pos >= srcSize) {
                return DecodeResult::kUnexpectedEnd;
            }
            const uint8_t val = src[pos++];
            const size_t toWrite = std::min(count, static_cast<size_t>(expectedSize) - out.size());
            out.insert(out.end(), toWrite, val);
        } else {
            if (pos + count > srcSize) {
                return DecodeResult::kUnexpectedEnd;
            }
            const size_t toWrite = std::min(count, static_cast<size_t>(expectedSize) - out.size());
            out.insert(out.end(), src + pos, src + pos + toWrite);
            pos += count;
        }
    }
    return DecodeResult::kOK;
}

// DarkStar LZHUF decoder: 4096-byte ring window (filled with ' '), lookahead 60, threshold 2,
// adaptive Huffman tree (314 symbols) and static position prefix codes.
DecodeResult DecodeLzh(
    const uint8_t *src,
    size_t srcSize,
    uint32_t expectedSize,
    std::vector<uint8_t> &out)
{
    constexpr uint32_t kWindowSize = 4096;
    constexpr uint32_t kWindowMask = kWindowSize - 1;
    constexpr uint32_t kLookahead  = 60;
    constexpr uint32_t kThreshold  = 2;
    constexpr uint32_t kSymbols    = 256 - kThreshold + kLookahead; // 314
    constexpr uint32_t kTableSize  = kSymbols * 2 - 1;              // 627
    constexpr uint32_t kRoot       = kTableSize - 1;                // 626

    std::array<uint16_t, kTableSize + 1> freq{};
    std::array<uint16_t, kTableSize> child{};
    std::array<uint16_t, kTableSize + kSymbols> parent{};

    for (uint32_t i = 0; i < kSymbols; ++i) {
        freq[i] = 1;
        child[i] = static_cast<uint16_t>(i + kTableSize);
        parent[i + kTableSize] = static_cast<uint16_t>(i);
    }

    uint32_t leaf = 0;
    for (uint32_t node = kSymbols; node < kTableSize; ++node) {
        freq[node] = static_cast<uint16_t>(freq[leaf] + freq[leaf + 1]);
        child[node] = static_cast<uint16_t>(leaf);
        parent[leaf] = static_cast<uint16_t>(node);
        parent[leaf + 1] = static_cast<uint16_t>(node);
        leaf += 2;
    }
    freq[kTableSize] = 0xFFFFu;
    parent[kRoot] = 0;

    size_t pos = 0;
    uint16_t bitBuf = 0;
    int bitCount = 0;

    auto nextBit = [&](uint32_t &bitOut) -> bool {
        while (bitCount <= 8 && pos < srcSize) {
            const uint8_t nextByte = src[pos++];
            bitBuf = static_cast<uint16_t>(bitBuf | (static_cast<uint16_t>(nextByte) << (8 - bitCount)));
            bitCount += 8;
        }
        if (bitCount == 0) {
            return false;
        }
        bitOut = static_cast<uint32_t>(bitBuf >> 15);
        bitBuf = static_cast<uint16_t>(bitBuf << 1);
        bitCount -= 1;
        return true;
    };

    auto rebuild = [&]() {
        uint32_t count = 0;
        for (uint32_t i = 0; i < kTableSize; ++i) {
            if (child[i] >= kTableSize) {
                freq[count] = static_cast<uint16_t>((freq[i] + 1) / 2);
                child[count] = child[i];
                ++count;
            }
        }
        uint32_t lf = 0;
        for (uint32_t node = kSymbols; node < kTableSize; ++node) {
            const uint16_t total = static_cast<uint16_t>(freq[lf] + freq[lf + 1]);
            uint32_t insert = node;
            while (insert > 0 && total < freq[insert - 1]) {
                --insert;
            }
            for (uint32_t k = node; k > insert; --k) {
                freq[k] = freq[k - 1];
                child[k] = child[k - 1];
            }
            freq[insert] = total;
            child[insert] = static_cast<uint16_t>(lf);
            lf += 2;
        }
        for (uint32_t node = 0; node < kTableSize; ++node) {
            const uint16_t c = child[node];
            parent[c] = static_cast<uint16_t>(node);
            if (c < kTableSize) {
                parent[c + 1] = static_cast<uint16_t>(node);
            }
        }
    };

    out.clear();
    out.reserve(expectedSize);

    std::array<uint8_t, kWindowSize> window;
    window.fill(static_cast<uint8_t>(' '));
    uint32_t writePos = kWindowSize - kLookahead;

    while (out.size() < expectedSize) {
        uint32_t node = child[kRoot];
        while (node < kTableSize) {
            uint32_t b = 0;
            if (!nextBit(b)) {
                return DecodeResult::kUnexpectedEnd;
            }
            node = child[node + b];
        }
        const uint32_t symbol = node - kTableSize;

        if (freq[kRoot] == 0x8000u) {
            rebuild();
        }
        uint32_t cur = parent[symbol + kTableSize];
        do {
            freq[cur] = static_cast<uint16_t>(freq[cur] + 1);
            const uint16_t val = freq[cur];
            uint32_t swap = cur + 1;
            if (val > freq[swap]) {
                ++swap;
                while (val > freq[swap]) {
                    ++swap;
                }
                --swap;
                freq[cur] = freq[swap];
                freq[swap] = val;

                const uint16_t first = child[cur];
                parent[first] = static_cast<uint16_t>(swap);
                if (first < kTableSize) {
                    parent[first + 1] = static_cast<uint16_t>(swap);
                }

                const uint16_t second = child[swap];
                child[swap] = first;
                parent[second] = static_cast<uint16_t>(cur);
                if (second < kTableSize) {
                    parent[second + 1] = static_cast<uint16_t>(cur);
                }
                child[cur] = second;
                cur = swap;
            }
            cur = parent[cur];
        } while (cur != 0);

        if (symbol < 256) {
            const uint8_t ch = static_cast<uint8_t>(symbol);
            out.push_back(ch);
            window[writePos] = ch;
            writePos = (writePos + 1) & kWindowMask;
            continue;
        }

        // Static position prefix codes:
        // len 3: 0..0 (sub 0)
        // len 4: 2..4 (sub 1)
        // len 5: 10..17 (sub 6)
        // len 6: 36..47 (sub 24)
        // len 7: 96..119 (sub 72)
        // len 8: 240..255 (sub 192)
        uint32_t code = 0;
        uint32_t upper = 0;
        bool matched = false;
        for (int len = 1; len <= 8; ++len) {
            uint32_t b = 0;
            if (!nextBit(b)) {
                return DecodeResult::kUnexpectedEnd;
            }
            code = (code << 1) | b;
            switch (len) {
                case 3:
                    if (code == 0) { upper = 0; matched = true; }
                    break;
                case 4:
                    if (code >= 2 && code <= 4) { upper = code - 1; matched = true; }
                    break;
                case 5:
                    if (code >= 10 && code <= 17) { upper = code - 6; matched = true; }
                    break;
                case 6:
                    if (code >= 36 && code <= 47) { upper = code - 24; matched = true; }
                    break;
                case 7:
                    if (code >= 96 && code <= 119) { upper = code - 72; matched = true; }
                    break;
                case 8:
                    if (code >= 240 && code <= 255) { upper = code - 192; matched = true; }
                    break;
                default:
                    break;
            }
            if (matched) break;
        }
        if (!matched) {
            return DecodeResult::kDataError;
        }

        for (int i = 0; i < 6; ++i) {
            uint32_t b = 0;
            if (!nextBit(b)) {
                return DecodeResult::kUnexpectedEnd;
            }
            upper = (upper << 1) | b;
        }

        const uint32_t readBase = writePos - upper - 1;
        const uint32_t matchLen = symbol - 255 + kThreshold;
        for (uint32_t i = 0; i < matchLen && out.size() < expectedSize; ++i) {
            const uint8_t val = window[(readBase + i) & kWindowMask];
            out.push_back(val);
            window[writePos] = val;
            writePos = (writePos + 1) & kWindowMask;
        }
    }

    return DecodeResult::kOK;
}

} // namespace

DecodeResult DecodeBlock(
    const uint8_t *src,
    size_t srcSize,
    uint32_t expectedSize,
    uint8_t compression,
    std::vector<uint8_t> &out)
{
    if (expectedSize == 0) {
        out.clear();
        return DecodeResult::kOK;
    }
    if (!src && srcSize > 0) {
        return DecodeResult::kDataError;
    }

    switch (compression) {
        case kNone:
            if (srcSize < expectedSize) {
                return DecodeResult::kUnexpectedEnd;
            }
            out.assign(src, src + expectedSize);
            return DecodeResult::kOK;

        case kRLE:
            return DecodeRle(src, srcSize, expectedSize, out);

        case kLZH:
            return DecodeLzh(src, srcSize, expectedSize, out);

        default:
            return DecodeResult::kUnsupportedMethod;
    }
}

void EncodeRle(const uint8_t *src, size_t srcSize, std::vector<uint8_t> &out) {
    out.clear();
    if (!src || srcSize == 0) return;

    size_t i = 0;
    while (i < srcSize) {
        // Count run of identical bytes (up to 127).
        size_t run = 1;
        while (i + run < srcSize && run < 127 && src[i + run] == src[i]) {
            ++run;
        }
        if (run >= 3) {
            out.push_back(static_cast<uint8_t>(0x80u | run));
            out.push_back(src[i]);
            i += run;
            continue;
        }

        // Gather literal span (up to 127) stopping before a run of >= 3 identical bytes.
        const size_t litStart = i;
        while (i < srcSize && (i - litStart) < 127) {
            if (i + 2 < srcSize && src[i] == src[i + 1] && src[i] == src[i + 2]) {
                break;
            }
            ++i;
        }
        const size_t litLen = i - litStart;
        out.push_back(static_cast<uint8_t>(litLen));
        out.insert(out.end(), src + litStart, src + i);
    }
}

} // namespace Darkstar
