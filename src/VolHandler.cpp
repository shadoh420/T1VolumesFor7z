#include "VolHandler.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <limits>
#include <string>
#include <vector>

namespace Darkstar {

using namespace SevenZip;

namespace {

struct PropSpec {
    PROPID id;
    VARTYPE vt;
};

constexpr PropSpec kItemProps[] = {
    { kpidPath,     VT_BSTR },
    { kpidIsDir,    VT_BOOL },
    { kpidSize,     VT_UI8  },
    { kpidPackSize, VT_UI8  },
    { kpidMethod,   VT_BSTR },
};

constexpr PropSpec kArcProps[] = {
    { kpidHeadersSize, VT_UI8  },
    { kpidMethod,      VT_BSTR },
};

inline void WriteLE32(uint8_t *dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

inline void AppendLE32(std::vector<uint8_t> &buf, uint32_t v) {
    uint8_t tmp[4];
    WriteLE32(tmp, v);
    buf.insert(buf.end(), tmp, tmp + 4);
}

bool EqualsIgnoreCase(const wchar_t *a, const wchar_t *b) noexcept {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::towlower(*a) != std::towlower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

// Abstracts writing either directly to a seekable IOutStream or into an in-memory buffer
// when only ISequentialOutStream is provided.
class VolWriter {
public:
    VolWriter(ISequentialOutStream *seqOut) noexcept : seqOut_(seqOut) {
        if (seqOut_) {
            seqOut_->QueryInterface(IID_IOutStream, reinterpret_cast<void **>(seekOut_.AddressOf()));
            seqOut_->QueryInterface(IID_IStreamSetRestriction, reinterpret_cast<void **>(restrict_.AddressOf()));
        }
    }

    HRESULT Init() noexcept {
        if (seekOut_) {
            HRESULT hr = seekOut_->Seek(0, STREAM_SEEK_CUR, &basePos_);
            if (FAILED(hr)) return hr;
            if (restrict_) {
                // Keep [basePos_, inf) unrestricted from premature flush so we can patch header/VBLK sizes.
                hr = restrict_->SetRestriction(basePos_, static_cast<UInt64>(-1));
                if (FAILED(hr)) return hr;
            }
        }
        relPos_ = 0;
        return S_OK;
    }

    uint64_t Position() const noexcept { return relPos_; }

    HRESULT Write(const void *data, size_t size) noexcept {
        if (size == 0) return S_OK;
        if (seekOut_) {
            HRESULT hr = WriteAll(seqOut_, data, size);
            if (FAILED(hr)) return hr;
            relPos_ += size;
            return S_OK;
        }
        const auto *bytes = static_cast<const uint8_t *>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + size);
        relPos_ += size;
        return S_OK;
    }

    HRESULT WriteZeros(size_t count) noexcept {
        static constexpr uint8_t kZeros[16]{};
        while (count > 0) {
            const size_t chunk = count > sizeof(kZeros) ? sizeof(kZeros) : count;
            HRESULT hr = Write(kZeros, chunk);
            if (FAILED(hr)) return hr;
            count -= chunk;
        }
        return S_OK;
    }

    HRESULT PatchAt(uint64_t relOffset, const void *data, size_t size) noexcept {
        if (seekOut_) {
            HRESULT hr = seekOut_->Seek(static_cast<Int64>(basePos_ + relOffset), STREAM_SEEK_SET, nullptr);
            if (FAILED(hr)) return hr;
            hr = WriteAll(seqOut_, data, size);
            if (FAILED(hr)) return hr;
            hr = seekOut_->Seek(static_cast<Int64>(basePos_ + relPos_), STREAM_SEEK_SET, nullptr);
            return hr;
        }
        if (relOffset + size > buffer_.size()) return E_FAIL;
        std::memcpy(buffer_.data() + relOffset, data, size);
        return S_OK;
    }

    HRESULT Finish() noexcept {
        if (seekOut_) {
            if (restrict_) {
                return restrict_->SetRestriction(0, 0);
            }
            return S_OK;
        }
        if (!buffer_.empty()) {
            return WriteAll(seqOut_, buffer_.data(), buffer_.size());
        }
        return S_OK;
    }

private:
    ISequentialOutStream *seqOut_ = nullptr;
    ComPtr<IOutStream> seekOut_;
    ComPtr<IStreamSetRestriction> restrict_;
    uint64_t basePos_ = 0;
    uint64_t relPos_ = 0;
    std::vector<uint8_t> buffer_;
};

struct WrittenRecord {
    uint32_t id = 0;
    uint32_t nameOffset = 0;
    uint32_t blockOffset = 0;
    uint32_t unpackedSize = 0;
    uint8_t compression = 0;
};

struct UpdateItemInfo {
    bool newData = false;
    bool newProps = false;
    bool isDir = false;
    UInt32 indexInArc = 0;
    UInt32 indexInClient = 0;
    UInt64 size = 0;
    std::wstring path;
};

} // namespace

// ---------- IUnknown ----------

STDMETHODIMP VolHandler::QueryInterface(REFIID iid, void **outObject) noexcept {
    if (!outObject) return E_POINTER;
    *outObject = nullptr;

    if (iid == IID_IUnknown || iid == IID_IInArchive) {
        *outObject = static_cast<IInArchive *>(this);
    } else if (iid == IID_IOutArchive) {
        *outObject = static_cast<IOutArchive *>(this);
    } else if (iid == IID_ISetProperties) {
        *outObject = static_cast<ISetProperties *>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VolHandler::AddRef() noexcept {
    return static_cast<ULONG>(++refCount_);
}

STDMETHODIMP_(ULONG) VolHandler::Release() noexcept {
    const LONG remaining = --refCount_;
    if (remaining == 0) {
        delete this;
    }
    return static_cast<ULONG>(remaining);
}

// ---------- IInArchive ----------

STDMETHODIMP VolHandler::Open(
    IInStream *stream,
    const UInt64 * /*maxCheckStartPosition*/,
    IArchiveOpenCallback *openCallback) noexcept
{
    try {
        Close();
        if (!stream) return E_INVALIDARG;

        HRESULT hr = ParseVolume(stream, arc_);
        if (hr != S_OK) {
            arc_.Clear();
            return hr;
        }

        stream_ = ComPtr<IInStream>(stream);

        if (openCallback) {
            const UInt64 files = arc_.entries.size();
            const UInt64 bytes = arc_.phySize;
            openCallback->SetTotal(&files, &bytes);
            openCallback->SetCompleted(&files, &bytes);
        }

        return S_OK;
    } catch (...) {
        Close();
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP VolHandler::Close() noexcept {
    stream_.Release();
    arc_.Clear();
    return S_OK;
}

STDMETHODIMP VolHandler::GetNumberOfItems(UInt32 *numItems) noexcept {
    if (!numItems) return E_POINTER;
    *numItems = static_cast<UInt32>(arc_.entries.size());
    return S_OK;
}

STDMETHODIMP VolHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value) noexcept {
    if (!value) return E_POINTER;
    ClearPropVariant(value);
    if (index >= arc_.entries.size()) return E_INVALIDARG;

    try {
        const VolEntry &e = arc_.entries[index];
        switch (propID) {
            case kpidPath:
                return SetPropString(value, e.wideName.c_str());
            case kpidIsDir:
                return SetPropBool(value, false);
            case kpidSize:
                return SetPropUInt64(value, e.unpackedSize);
            case kpidPackSize:
                return SetPropUInt64(value, e.packedSize);
            case kpidMethod:
                return SetPropString(value, CompressionName(e.compression));
            default:
                return S_OK;
        }
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP VolHandler::Extract(
    const UInt32 *indices,
    UInt32 numItems,
    Int32 testMode,
    IArchiveExtractCallback *extractCallback) noexcept
{
    if (!extractCallback) return E_INVALIDARG;
    if (!stream_) return E_FAIL;

    try {
        const bool allFiles = (numItems == static_cast<UInt32>(-1));
        const UInt32 count = allFiles ? static_cast<UInt32>(arc_.entries.size()) : numItems;

        UInt64 totalSize = 0;
        for (UInt32 i = 0; i < count; ++i) {
            const UInt32 idx = allFiles ? i : indices[i];
            if (idx >= arc_.entries.size()) return E_INVALIDARG;
            totalSize += arc_.entries[idx].unpackedSize;
        }

        HRESULT hr = extractCallback->SetTotal(totalSize);
        if (FAILED(hr)) return hr;

        const Int32 askMode = testMode ? NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
        UInt64 completed = 0;
        std::vector<uint8_t> copyBuf(65536);
        std::vector<uint8_t> packedBuf;
        std::vector<uint8_t> decodedBuf;

        for (UInt32 i = 0; i < count; ++i) {
            const UInt32 idx = allFiles ? i : indices[i];
            const VolEntry &e = arc_.entries[idx];

            ComPtr<ISequentialOutStream> outStream;
            hr = extractCallback->GetStream(idx, outStream.AddressOf(), askMode);
            if (FAILED(hr)) return hr;

            if (!testMode && !outStream) {
                completed += e.unpackedSize;
                hr = extractCallback->SetCompleted(&completed);
                if (FAILED(hr)) return hr;
                continue;
            }

            hr = extractCallback->PrepareOperation(askMode);
            if (FAILED(hr)) return hr;

            Int32 opRes = NExtract::NOperationResult::kOK;

            hr = stream_->Seek(static_cast<Int64>(e.blockOffset) + 8, STREAM_SEEK_SET, nullptr);
            if (FAILED(hr)) return hr;

            if (e.compression == kNone) {
                if (e.packedSize < e.unpackedSize) {
                    opRes = NExtract::NOperationResult::kUnexpectedEnd;
                } else {
                    uint32_t remaining = e.unpackedSize;
                    while (remaining > 0) {
                        const uint32_t chunk = remaining > copyBuf.size()
                            ? static_cast<uint32_t>(copyBuf.size())
                            : remaining;
                        hr = ReadExact(stream_.Get(), copyBuf.data(), chunk);
                        if (FAILED(hr)) return hr;
                        if (hr == S_FALSE) {
                            opRes = NExtract::NOperationResult::kUnexpectedEnd;
                            break;
                        }
                        if (!testMode && outStream) {
                            hr = WriteAll(outStream.Get(), copyBuf.data(), chunk);
                            if (FAILED(hr)) return hr;
                        }
                        remaining -= chunk;
                        completed += chunk;
                        hr = extractCallback->SetCompleted(&completed);
                        if (FAILED(hr)) return hr;
                    }
                }
            } else {
                packedBuf.resize(e.packedSize);
                if (e.packedSize > 0) {
                    hr = ReadExact(stream_.Get(), packedBuf.data(), e.packedSize);
                    if (FAILED(hr)) return hr;
                    if (hr == S_FALSE) {
                        opRes = NExtract::NOperationResult::kUnexpectedEnd;
                    }
                }
                if (opRes == NExtract::NOperationResult::kOK) {
                    const DecodeResult decRes = DecodeBlock(
                        packedBuf.data(),
                        packedBuf.size(),
                        e.unpackedSize,
                        e.compression,
                        decodedBuf);
                    switch (decRes) {
                        case DecodeResult::kOK:
                            if (!testMode && outStream && !decodedBuf.empty()) {
                                hr = WriteAll(outStream.Get(), decodedBuf.data(), decodedBuf.size());
                                if (FAILED(hr)) return hr;
                            }
                            completed += e.unpackedSize;
                            break;
                        case DecodeResult::kUnsupportedMethod:
                            opRes = NExtract::NOperationResult::kUnsupportedMethod;
                            break;
                        case DecodeResult::kUnexpectedEnd:
                            opRes = NExtract::NOperationResult::kUnexpectedEnd;
                            break;
                        case DecodeResult::kDataError:
                            opRes = NExtract::NOperationResult::kDataError;
                            break;
                    }
                }
            }

            // Release output stream before SetOperationResult so 7-Zip can finalize timestamps/attributes.
            outStream.Release();

            hr = extractCallback->SetOperationResult(opRes);
            if (FAILED(hr)) return hr;

            hr = extractCallback->SetCompleted(&completed);
            if (FAILED(hr)) return hr;
        }

        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP VolHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value) noexcept {
    if (!value) return E_POINTER;
    ClearPropVariant(value);

    try {
        switch (propID) {
            case kpidPhySize:
                return SetPropUInt64(value, arc_.phySize);
            case kpidHeadersSize:
                return SetPropUInt64(value, arc_.headersSize);
            case kpidMethod:
                return SetPropString(value, arc_.isStarsiege ? L"VOL (Starsiege)" : L"PVOL");
            default:
                return S_OK;
        }
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP VolHandler::GetNumberOfProperties(UInt32 *numProps) noexcept {
    if (!numProps) return E_POINTER;
    *numProps = static_cast<UInt32>(std::size(kItemProps));
    return S_OK;
}

STDMETHODIMP VolHandler::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType) noexcept {
    if (index >= std::size(kItemProps)) return E_INVALIDARG;
    if (name) *name = nullptr;
    if (propID) *propID = kItemProps[index].id;
    if (varType) *varType = kItemProps[index].vt;
    return S_OK;
}

STDMETHODIMP VolHandler::GetNumberOfArchiveProperties(UInt32 *numProps) noexcept {
    if (!numProps) return E_POINTER;
    *numProps = static_cast<UInt32>(std::size(kArcProps));
    return S_OK;
}

STDMETHODIMP VolHandler::GetArchivePropertyInfo(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType) noexcept {
    if (index >= std::size(kArcProps)) return E_INVALIDARG;
    if (name) *name = nullptr;
    if (propID) *propID = kArcProps[index].id;
    if (varType) *varType = kArcProps[index].vt;
    return S_OK;
}

// ---------- IOutArchive ----------

STDMETHODIMP VolHandler::GetFileTimeType(UInt32 *type) noexcept {
    if (!type) return E_POINTER;
    *type = NFileTimeType::kWindows;
    return S_OK;
}

STDMETHODIMP VolHandler::UpdateItems(
    ISequentialOutStream *outStream,
    UInt32 numItems,
    IArchiveUpdateCallback *updateCallback) noexcept
{
    if (!outStream || !updateCallback) return E_INVALIDARG;

    try {
        // 1. Collect update item metadata from 7-Zip.
        std::vector<UpdateItemInfo> items;
        items.reserve(numItems);

        UInt64 totalComplexity = 0;
        for (UInt32 i = 0; i < numItems; ++i) {
            UpdateItemInfo ui;
            Int32 newData = 0;
            Int32 newProps = 0;
            UInt32 indexInArc = static_cast<UInt32>(-1);

            HRESULT hr = updateCallback->GetUpdateItemInfo(i, &newData, &newProps, &indexInArc);
            if (FAILED(hr)) return hr;

            ui.newData = (newData != 0);
            ui.newProps = (newProps != 0);
            ui.indexInArc = indexInArc;
            ui.indexInClient = i;

            if (!ui.newData || !ui.newProps) {
                if (!stream_ || indexInArc >= arc_.entries.size()) {
                    return E_INVALIDARG;
                }
            }

            if (ui.newProps) {
                PropVariantGuard isDirProp;
                hr = updateCallback->GetProperty(i, kpidIsDir, &isDirProp.value);
                if (FAILED(hr)) return hr;
                if (isDirProp.value.vt == VT_BOOL) {
                    ui.isDir = (isDirProp.value.boolVal != VARIANT_FALSE);
                } else if (isDirProp.value.vt != VT_EMPTY) {
                    return E_INVALIDARG;
                }

                PropVariantGuard pathProp;
                hr = updateCallback->GetProperty(i, kpidPath, &pathProp.value);
                if (FAILED(hr)) return hr;
                if (pathProp.value.vt == VT_BSTR && pathProp.value.bstrVal) {
                    ui.path = NormalizeVolPath(pathProp.value.bstrVal);
                } else if (pathProp.value.vt != VT_EMPTY) {
                    return E_INVALIDARG;
                }
            } else {
                ui.isDir = false;
                ui.path = arc_.entries[indexInArc].wideName;
            }

            if (ui.newData) {
                if (!ui.isDir) {
                    PropVariantGuard sizeProp;
                    hr = updateCallback->GetProperty(i, kpidSize, &sizeProp.value);
                    if (FAILED(hr)) return hr;
                    if (sizeProp.value.vt == VT_UI8) {
                        ui.size = sizeProp.value.uhVal.QuadPart;
                    } else if (sizeProp.value.vt == VT_UI4) {
                        ui.size = sizeProp.value.ulVal;
                    } else if (sizeProp.value.vt != VT_EMPTY) {
                        return E_INVALIDARG;
                    }
                    totalComplexity += ui.size;
                }
            } else {
                totalComplexity += arc_.entries[indexInArc].packedSize;
            }

            items.push_back(std::move(ui));
        }

        HRESULT hr = updateCallback->SetTotal(totalComplexity);
        if (FAILED(hr)) return hr;

        // 2. Write 8-byte volume header placeholder ("PVOL" or " VOL" + 0x00000000).
        VolWriter writer(outStream);
        hr = writer.Init();
        if (FAILED(hr)) return hr;

        uint8_t header[8]{};
        if (stream_ && arc_.isStarsiege) {
            std::memcpy(header, " VOL", 4);
        } else {
            std::memcpy(header, "PVOL", 4);
        }
        hr = writer.Write(header, sizeof(header));
        if (FAILED(hr)) return hr;

        // 3. Write VBLK blocks for all file entries.
        std::vector<uint8_t> namesBlob;
        std::vector<WrittenRecord> records;
        records.reserve(items.size());

        std::vector<uint8_t> ioBuf(65536);
        std::vector<uint8_t> rawBuf;
        std::vector<uint8_t> rleBuf;
        UInt64 completed = 0;

        constexpr uint64_t kMaxBlockPayload = 0x7FFFFFFFu;
        constexpr uint64_t kMaxVolOffset = std::numeric_limits<uint32_t>::max();

        for (const auto &ui : items) {
            if (ui.newData) {
                ComPtr<ISequentialInStream> fileInStream;
                hr = updateCallback->GetStream(ui.indexInClient, fileInStream.AddressOf());
                if (hr == S_FALSE) {
                    // Client requested to skip this file.
                    continue;
                }
                if (FAILED(hr)) return hr;

                if (ui.isDir || !fileInStream) {
                    // PVOL does not store directory-only entries, but we still complete the 7-Zip callback.
                    fileInStream.Release();
                    hr = updateCallback->SetOperationResult(NUpdate::NOperationResult::kOK);
                    if (FAILED(hr)) return hr;
                    continue;
                }

                // Align VBLK start to 4-byte boundary (canonical DarkStar layout).
                const size_t alignPad = static_cast<size_t>((4u - (writer.Position() & 3u)) & 3u);
                if (alignPad > 0) {
                    hr = writer.WriteZeros(alignPad);
                    if (FAILED(hr)) return hr;
                }

                const uint64_t blockOffset = writer.Position();
                if (blockOffset > kMaxVolOffset) return E_FAIL;

                uint32_t unpackedSize = 0;
                uint32_t packedSize = 0;
                uint8_t comp = kNone;

                if (writeCompression_ == kRLE) {
                    rawBuf.clear();
                    if (ui.size <= 64u * 1024u * 1024u) {
                        rawBuf.reserve(static_cast<size_t>(ui.size));
                    }
                    while (true) {
                        UInt32 got = 0;
                        hr = fileInStream->Read(ioBuf.data(), static_cast<UInt32>(ioBuf.size()), &got);
                        if (FAILED(hr)) return hr;
                        if (got == 0) break;
                        if (rawBuf.size() + got > kMaxBlockPayload) return E_FAIL;
                        rawBuf.insert(rawBuf.end(), ioBuf.data(), ioBuf.data() + got);
                        completed += got;
                        hr = updateCallback->SetCompleted(&completed);
                        if (FAILED(hr)) return hr;
                    }

                    unpackedSize = static_cast<uint32_t>(rawBuf.size());
                    EncodeRle(rawBuf.data(), rawBuf.size(), rleBuf);

                    const uint8_t *payloadPtr = rawBuf.data();
                    if (!rawBuf.empty() && rleBuf.size() < rawBuf.size() && rleBuf.size() <= kMaxBlockPayload) {
                        comp = kRLE;
                        packedSize = static_cast<uint32_t>(rleBuf.size());
                        payloadPtr = rleBuf.data();
                    } else {
                        comp = kNone;
                        packedSize = unpackedSize;
                    }

                    uint8_t vblkHdr[8];
                    std::memcpy(vblkHdr, "VBLK", 4);
                    WriteLE32(vblkHdr + 4, packedSize | 0x80000000u);
                    hr = writer.Write(vblkHdr, sizeof(vblkHdr));
                    if (FAILED(hr)) return hr;
                    if (packedSize > 0) {
                        hr = writer.Write(payloadPtr, packedSize);
                        if (FAILED(hr)) return hr;
                    }
                } else {
                    // Stream Stored (compression = 0) directly in 64 KB chunks; patch exact size after EOF.
                    uint8_t vblkHdr[8];
                    std::memcpy(vblkHdr, "VBLK", 4);
                    WriteLE32(vblkHdr + 4, 0x80000000u);
                    hr = writer.Write(vblkHdr, sizeof(vblkHdr));
                    if (FAILED(hr)) return hr;

                    uint64_t totalRead = 0;
                    while (true) {
                        UInt32 got = 0;
                        hr = fileInStream->Read(ioBuf.data(), static_cast<UInt32>(ioBuf.size()), &got);
                        if (FAILED(hr)) return hr;
                        if (got == 0) break;
                        if (totalRead + got > kMaxBlockPayload) return E_FAIL;
                        hr = writer.Write(ioBuf.data(), got);
                        if (FAILED(hr)) return hr;
                        totalRead += got;
                        completed += got;
                        hr = updateCallback->SetCompleted(&completed);
                        if (FAILED(hr)) return hr;
                    }

                    unpackedSize = static_cast<uint32_t>(totalRead);
                    packedSize = unpackedSize;
                    comp = kNone;

                    uint8_t sizeField[4];
                    WriteLE32(sizeField, packedSize | 0x80000000u);
                    hr = writer.PatchAt(blockOffset + 4, sizeField, sizeof(sizeField));
                    if (FAILED(hr)) return hr;
                }

                fileInStream.Release();
                hr = updateCallback->SetOperationResult(NUpdate::NOperationResult::kOK);
                if (FAILED(hr)) return hr;

                const std::string rawName = WideToCp1252(ui.path);
                if (namesBlob.size() > kMaxVolOffset) return E_FAIL;

                WrittenRecord rec;
                rec.id = 0;
                rec.nameOffset = static_cast<uint32_t>(namesBlob.size());
                rec.blockOffset = static_cast<uint32_t>(blockOffset);
                rec.unpackedSize = unpackedSize;
                rec.compression = comp;
                records.push_back(rec);

                namesBlob.insert(namesBlob.end(), rawName.begin(), rawName.end());
                namesBlob.push_back('\0');
            } else {
                // Copy unmodified item verbatim from original archive (preserves LZH/RLE/Store).
                const VolEntry &orig = arc_.entries[ui.indexInArc];

                const size_t alignPad = static_cast<size_t>((4u - (writer.Position() & 3u)) & 3u);
                if (alignPad > 0) {
                    hr = writer.WriteZeros(alignPad);
                    if (FAILED(hr)) return hr;
                }

                const uint64_t blockOffset = writer.Position();
                if (blockOffset > kMaxVolOffset) return E_FAIL;

                uint8_t vblkHdr[8];
                std::memcpy(vblkHdr, "VBLK", 4);
                WriteLE32(vblkHdr + 4, orig.packedSize | 0x80000000u);
                hr = writer.Write(vblkHdr, sizeof(vblkHdr));
                if (FAILED(hr)) return hr;

                hr = stream_->Seek(static_cast<Int64>(orig.blockOffset) + 8, STREAM_SEEK_SET, nullptr);
                if (FAILED(hr)) return hr;

                uint32_t remaining = orig.packedSize;
                while (remaining > 0) {
                    const uint32_t chunk = remaining > ioBuf.size()
                        ? static_cast<uint32_t>(ioBuf.size())
                        : remaining;
                    hr = ReadExact(stream_.Get(), ioBuf.data(), chunk);
                    if (hr != S_OK) return FAILED(hr) ? hr : E_FAIL;
                    hr = writer.Write(ioBuf.data(), chunk);
                    if (FAILED(hr)) return hr;
                    remaining -= chunk;
                    completed += chunk;
                    hr = updateCallback->SetCompleted(&completed);
                    if (FAILED(hr)) return hr;
                }

                const std::string rawName = ui.newProps ? WideToCp1252(ui.path) : orig.rawName;
                if (namesBlob.size() > kMaxVolOffset) return E_FAIL;

                WrittenRecord rec;
                rec.id = orig.id;
                rec.nameOffset = static_cast<uint32_t>(namesBlob.size());
                rec.blockOffset = static_cast<uint32_t>(blockOffset);
                rec.unpackedSize = orig.unpackedSize;
                rec.compression = orig.compression;
                records.push_back(rec);

                namesBlob.insert(namesBlob.end(), rawName.begin(), rawName.end());
                namesBlob.push_back('\0');
            }
        }

        // Align `vols` chunk to 4-byte boundary.
        const size_t volsAlignPad = static_cast<size_t>((4u - (writer.Position() & 3u)) & 3u);
        if (volsAlignPad > 0) {
            hr = writer.WriteZeros(volsAlignPad);
            if (FAILED(hr)) return hr;
        }

        const uint64_t namesOffset = writer.Position();
        if (namesOffset > kMaxVolOffset || namesBlob.size() > kMaxVolOffset) {
            return E_FAIL;
        }

        // 4. Write `vols` chunk (+ 1 zero pad byte if odd length).
        uint8_t volsHdr[8];
        std::memcpy(volsHdr, "vols", 4);
        WriteLE32(volsHdr + 4, static_cast<uint32_t>(namesBlob.size()));
        hr = writer.Write(volsHdr, sizeof(volsHdr));
        if (FAILED(hr)) return hr;

        if (!namesBlob.empty()) {
            hr = writer.Write(namesBlob.data(), namesBlob.size());
            if (FAILED(hr)) return hr;
        }
        if (namesBlob.size() & 1u) {
            hr = writer.WriteZeros(1);
            if (FAILED(hr)) return hr;
        }

        // 5. Write `voli` chunk (17 bytes per entry).
        const uint64_t indexSize64 = static_cast<uint64_t>(records.size()) * 17u;
        if (indexSize64 > kMaxVolOffset) return E_FAIL;

        uint8_t voliHdr[8];
        std::memcpy(voliHdr, "voli", 4);
        WriteLE32(voliHdr + 4, static_cast<uint32_t>(indexSize64));
        hr = writer.Write(voliHdr, sizeof(voliHdr));
        if (FAILED(hr)) return hr;

        std::vector<uint8_t> indexBlob;
        indexBlob.reserve(static_cast<size_t>(indexSize64));
        for (const auto &rec : records) {
            AppendLE32(indexBlob, rec.id);
            AppendLE32(indexBlob, rec.nameOffset);
            AppendLE32(indexBlob, rec.blockOffset);
            AppendLE32(indexBlob, rec.unpackedSize);
            indexBlob.push_back(rec.compression);
        }
        if (!indexBlob.empty()) {
            hr = writer.Write(indexBlob.data(), indexBlob.size());
            if (FAILED(hr)) return hr;
        }

        // 6. Patch `namesOffset` into bytes [4..7] of the volume header.
        uint8_t namesOffField[4];
        WriteLE32(namesOffField, static_cast<uint32_t>(namesOffset));
        hr = writer.PatchAt(4, namesOffField, sizeof(namesOffField));
        if (FAILED(hr)) return hr;

        hr = writer.Finish();
        if (FAILED(hr)) return hr;

        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

// ---------- ISetProperties ----------

STDMETHODIMP VolHandler::SetProperties(
    const wchar_t *const *names,
    const PROPVARIANT *values,
    UInt32 numProps) noexcept
{
    writeCompression_ = kNone;
    if (!names || !values) return S_OK;

    for (UInt32 i = 0; i < numProps; ++i) {
        const wchar_t *name = names[i];
        if (!name) continue;
        const PROPVARIANT &val = values[i];

        if (EqualsIgnoreCase(name, L"0") || EqualsIgnoreCase(name, L"m") || EqualsIgnoreCase(name, L"m0")) {
            if (val.vt == VT_BSTR && val.bstrVal) {
                if (EqualsIgnoreCase(val.bstrVal, L"RLE")) {
                    writeCompression_ = kRLE;
                } else if (EqualsIgnoreCase(val.bstrVal, L"Store") || EqualsIgnoreCase(val.bstrVal, L"Copy")) {
                    writeCompression_ = kNone;
                } else {
                    return E_INVALIDARG;
                }
            }
        } else if (EqualsIgnoreCase(name, L"RLE")) {
            writeCompression_ = kRLE;
        } else if (EqualsIgnoreCase(name, L"Store") || EqualsIgnoreCase(name, L"Copy")) {
            writeCompression_ = kNone;
        }
        // Standard 7-Zip GUI properties (e.g., x, mt, s, f) are accepted silently so
        // creating .vol files from 7-Zip File Manager works at any slider position.
    }
    return S_OK;
}

} // namespace Darkstar
