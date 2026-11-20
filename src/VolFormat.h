#pragma once

#include "SevenZipApi.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Darkstar {

struct VolEntry {
    uint32_t id = 0;
    std::string rawName;       // Original CP1252 path inside `vols`
    std::wstring wideName;     // UTF-16 path for 7-Zip
    uint32_t blockOffset = 0;  // File offset of `VBLK` header
    uint32_t packedSize = 0;   // Payload size from `VBLK` header (raw & 0x7FFFFFFF)
    uint32_t unpackedSize = 0; // Uncompressed size from `voli`
    uint8_t compression = 0;   // 0 = Store, 1 = RLE, 3 = LZH
};

struct VolArchive {
    bool isStarsiege = false;  // true if magic was " VOL", false if "PVOL"
    uint32_t namesOffset = 0;
    uint64_t phySize = 0;
    uint64_t headersSize = 0;
    std::vector<VolEntry> entries;

    void Clear() noexcept {
        isStarsiege = false;
        namesOffset = 0;
        phySize = 0;
        headersSize = 0;
        entries.clear();
    }
};

// Fast signature probe for 7-Zip's GetIsArc.
SevenZip::UInt32 CheckIsArc(const uint8_t *data, size_t size) noexcept;

// Parse PVOL / " VOL" header, vols, voli, and validate each VBLK header.
// Returns S_OK on valid volume, S_FALSE if not a valid PVOL volume, or stream error HRESULT.
HRESULT ParseVolume(SevenZip::IInStream *stream, VolArchive &arc);

// Character encoding helpers (DarkStar volumes store CP1252 paths).
std::wstring Cp1252ToWide(const std::string &s);
std::string WideToCp1252(const std::wstring &s);

// Normalize path for storage inside a Tribes 1 volume (forward slashes -> backslashes, strip leading slashes).
std::wstring NormalizeVolPath(const std::wstring &path);

} // namespace Darkstar
