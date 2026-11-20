#pragma once

#include "DarkstarCodec.h"
#include "SevenZipApi.h"
#include "VolFormat.h"

#include <atomic>

namespace Darkstar {

class VolHandler final
    : public SevenZip::IInArchive
    , public SevenZip::IOutArchive
    , public SevenZip::ISetProperties
{
public:
    VolHandler() noexcept = default;
    ~VolHandler() noexcept = default;

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept override;
    STDMETHOD_(ULONG, AddRef)() noexcept override;
    STDMETHOD_(ULONG, Release)() noexcept override;

    // IInArchive
    STDMETHOD(Open)(SevenZip::IInStream *stream, const SevenZip::UInt64 *maxCheckStartPosition, SevenZip::IArchiveOpenCallback *openCallback) noexcept override;
    STDMETHOD(Close)() noexcept override;
    STDMETHOD(GetNumberOfItems)(SevenZip::UInt32 *numItems) noexcept override;
    STDMETHOD(GetProperty)(SevenZip::UInt32 index, PROPID propID, PROPVARIANT *value) noexcept override;
    STDMETHOD(Extract)(const SevenZip::UInt32 *indices, SevenZip::UInt32 numItems, SevenZip::Int32 testMode, SevenZip::IArchiveExtractCallback *extractCallback) noexcept override;
    STDMETHOD(GetArchiveProperty)(PROPID propID, PROPVARIANT *value) noexcept override;
    STDMETHOD(GetNumberOfProperties)(SevenZip::UInt32 *numProps) noexcept override;
    STDMETHOD(GetPropertyInfo)(SevenZip::UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType) noexcept override;
    STDMETHOD(GetNumberOfArchiveProperties)(SevenZip::UInt32 *numProps) noexcept override;
    STDMETHOD(GetArchivePropertyInfo)(SevenZip::UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType) noexcept override;

    // IOutArchive
    STDMETHOD(UpdateItems)(SevenZip::ISequentialOutStream *outStream, SevenZip::UInt32 numItems, SevenZip::IArchiveUpdateCallback *updateCallback) noexcept override;
    STDMETHOD(GetFileTimeType)(SevenZip::UInt32 *type) noexcept override;

    // ISetProperties
    STDMETHOD(SetProperties)(const wchar_t *const *names, const PROPVARIANT *values, SevenZip::UInt32 numProps) noexcept override;

private:
    std::atomic<LONG> refCount_{0};
    SevenZip::ComPtr<SevenZip::IInStream> stream_;
    VolArchive arc_;
    uint8_t writeCompression_ = kNone; // Default: Stored (canonical Tribes 1 format)
};

} // namespace Darkstar
