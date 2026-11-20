#include "VolFormat.h"

#include <cstring>

namespace Darkstar {

namespace {

inline uint32_t ReadLE32(const uint8_t *p) noexcept {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

inline bool TagEquals(const uint8_t *p, const char tag[4]) noexcept {
    return std::memcmp(p, tag, 4) == 0;
}

// Unicode code points for Windows-1252 bytes 0x80..0x9F.
constexpr uint16_t kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

} // namespace

std::wstring Cp1252ToWide(const std::string &s) {
    std::wstring out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 0x80u && c <= 0x9Fu) {
            out.push_back(static_cast<wchar_t>(kCp1252High[c - 0x80u]));
        } else {
            out.push_back(static_cast<wchar_t>(c));
        }
    }
    return out;
}

std::string WideToCp1252(const std::wstring &s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t wc : s) {
        const uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80u || (cp >= 0xA0u && cp <= 0xFFu)) {
            out.push_back(static_cast<char>(cp));
            continue;
        }
        char mapped = '?';
        for (uint32_t i = 0; i < 32; ++i) {
            if (kCp1252High[i] == cp) {
                mapped = static_cast<char>(0x80u + i);
                break;
            }
        }
        out.push_back(mapped);
    }
    return out;
}

std::wstring NormalizeVolPath(const std::wstring &path) {
    std::wstring out;
    out.reserve(path.size());
    for (wchar_t ch : path) {
        if (ch == L'/') ch = L'\\';
        // Strip leading path separators.
        if (out.empty() && ch == L'\\') continue;
        // Collapse consecutive backslashes.
        if (ch == L'\\' && !out.empty() && out.back() == L'\\') continue;
        out.push_back(ch);
    }
    while (!out.empty() && out.back() == L'\\') {
        out.pop_back();
    }
    return out;
}

SevenZip::UInt32 CheckIsArc(const uint8_t *data, size_t size) noexcept {
    if (!data || size < 4) {
        return SevenZip::NIsArc::kNeedMore;
    }
    const bool isPvol = TagEquals(data, "PVOL");
    const bool isSsVol = TagEquals(data, " VOL");
    if (!isPvol && !isSsVol) {
        return SevenZip::NIsArc::kNo;
    }
    if (size < 8) {
        return SevenZip::NIsArc::kNeedMore;
    }
    const uint32_t namesOffset = ReadLE32(data + 4);
    if (namesOffset < 8) {
        return SevenZip::NIsArc::kNo;
    }
    return SevenZip::NIsArc::kYes;
}

HRESULT ParseVolume(SevenZip::IInStream *stream, VolArchive &arc) {
    arc.Clear();
    if (!stream) return E_INVALIDARG;

    uint64_t fileSize = 0;
    HRESULT hr = stream->Seek(0, STREAM_SEEK_END, &fileSize);
    if (FAILED(hr)) return hr;
    if (fileSize < 24) {
        return S_FALSE;
    }

    hr = stream->Seek(0, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;

    uint8_t header[8]{};
    hr = SevenZip::ReadExact(stream, header, sizeof(header));
    if (hr != S_OK) return S_FALSE;

    const bool isPvol = TagEquals(header, "PVOL");
    const bool isSsVol = TagEquals(header, " VOL");
    if (!isPvol && !isSsVol) {
        return S_FALSE;
    }

    uint64_t namesAt = ReadLE32(header + 4);
    if (namesAt < 8 || namesAt + 8 > fileSize) {
        return S_FALSE;
    }

    // Starsiege (" VOL") can place an empty 16-byte "vols\0\0\0\0voli\0\0\0\0" pair before the real ones.
    if (isSsVol && namesAt + 16 <= fileSize) {
        hr = stream->Seek(static_cast<int64_t>(namesAt), STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) return hr;
        uint8_t probe[16]{};
        hr = SevenZip::ReadExact(stream, probe, sizeof(probe));
        if (hr != S_OK) return S_FALSE;
        static constexpr uint8_t kEmptyPair[16] = {
            'v','o','l','s', 0,0,0,0,
            'v','o','l','i', 0,0,0,0
        };
        if (std::memcmp(probe, kEmptyPair, 16) == 0 && namesAt + 16 + 8 <= fileSize) {
            namesAt += 16;
        }
    }

    // Read `vols` chunk header.
    hr = stream->Seek(static_cast<int64_t>(namesAt), STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;

    uint8_t chunkHdr[8]{};
    hr = SevenZip::ReadExact(stream, chunkHdr, sizeof(chunkHdr));
    if (hr != S_OK) return S_FALSE;
    if (!TagEquals(chunkHdr, "vols")) {
        return S_FALSE;
    }

    const uint32_t namesSize = ReadLE32(chunkHdr + 4);
    const uint64_t namesDataStart = namesAt + 8;
    const uint64_t padByte = namesSize & 1u;
    const uint64_t indexAt = namesDataStart + namesSize + padByte;
    if (indexAt + 8 > fileSize) {
        return S_FALSE;
    }

    std::vector<uint8_t> namesBlob(namesSize);
    if (namesSize > 0) {
        hr = SevenZip::ReadExact(stream, namesBlob.data(), namesSize);
        if (hr != S_OK) return S_FALSE;
    }
    if (padByte) {
        uint8_t dummy = 0;
        hr = SevenZip::ReadExact(stream, &dummy, 1);
        if (hr != S_OK) return S_FALSE;
    }

    // Read `voli` chunk header.
    hr = SevenZip::ReadExact(stream, chunkHdr, sizeof(chunkHdr));
    if (hr != S_OK) return S_FALSE;
    if (!TagEquals(chunkHdr, "voli")) {
        return S_FALSE;
    }

    const uint32_t indexSize = ReadLE32(chunkHdr + 4);
    if (indexSize % 17u != 0) {
        return S_FALSE;
    }
    const uint64_t indexEnd = indexAt + 8 + indexSize;
    if (indexEnd > fileSize) {
        return S_FALSE;
    }

    const uint32_t count = indexSize / 17u;
    std::vector<uint8_t> indexBlob(indexSize);
    if (indexSize > 0) {
        hr = SevenZip::ReadExact(stream, indexBlob.data(), indexSize);
        if (hr != S_OK) return S_FALSE;
    }

    arc.isStarsiege = isSsVol;
    arc.namesOffset = static_cast<uint32_t>(namesAt);
    arc.phySize = indexEnd;
    arc.headersSize = 8 + (indexEnd - namesAt) + static_cast<uint64_t>(count) * 8;
    arc.entries.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *rec = indexBlob.data() + static_cast<size_t>(i) * 17u;
        const uint32_t id          = ReadLE32(rec + 0);
        const uint32_t nameOffset  = ReadLE32(rec + 4);
        const uint32_t blockOffset = ReadLE32(rec + 8);
        const uint32_t unpackedSz  = ReadLE32(rec + 12);
        const uint8_t  compression = rec[16];

        if (nameOffset >= namesSize) {
            return S_FALSE;
        }
        const void *nulPtr = std::memchr(namesBlob.data() + nameOffset, '\0', namesSize - nameOffset);
        const size_t nameLen = nulPtr
            ? static_cast<size_t>(static_cast<const uint8_t *>(nulPtr) - (namesBlob.data() + nameOffset))
            : static_cast<size_t>(namesSize - nameOffset);

        if (static_cast<uint64_t>(blockOffset) + 8 > fileSize) {
            return S_FALSE;
        }
        hr = stream->Seek(static_cast<int64_t>(blockOffset), STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) return hr;
        uint8_t vblkHdr[8]{};
        hr = SevenZip::ReadExact(stream, vblkHdr, sizeof(vblkHdr));
        if (hr != S_OK || !TagEquals(vblkHdr, "VBLK")) {
            return S_FALSE;
        }

        const uint32_t rawBlockSize = ReadLE32(vblkHdr + 4);
        const uint32_t packedSize = rawBlockSize & 0x7FFFFFFFu;
        if (static_cast<uint64_t>(blockOffset) + 8 + packedSize > fileSize) {
            return S_FALSE;
        }

        VolEntry entry;
        entry.id = id;
        entry.rawName.assign(reinterpret_cast<const char *>(namesBlob.data() + nameOffset), nameLen);
        entry.wideName = Cp1252ToWide(entry.rawName);
        entry.blockOffset = blockOffset;
        entry.packedSize = packedSize;
        entry.unpackedSize = unpackedSz;
        entry.compression = compression;
        arc.entries.push_back(std::move(entry));
    }

    return S_OK;
}

} // namespace Darkstar
