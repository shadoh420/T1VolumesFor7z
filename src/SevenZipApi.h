#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#define TRIBESVOL_NOVTABLE __declspec(novtable)
#define TRIBESVOL_EXPORT STDAPI
#else
// Exact Linux ABI matching 7-Zip's CPP/Common/MyWindows.h, MyWindows.cpp, MyGuidDef.h, and C/7zTypes.h.
#include <stddef.h>

#define WINAPI
#define STDMETHODCALLTYPE
#define STDAPICALLTYPE
#define TRIBESVOL_NOVTABLE
#define TRIBESVOL_EXPORT extern "C" __attribute__((visibility("default"))) HRESULT

using CHAR = char;
using UCHAR = unsigned char;
using BYTE = unsigned char;
using SHORT = int16_t;
using USHORT = uint16_t;
using WORD = uint16_t;
using INT = int32_t;
using UINT = uint32_t;
using LONG = int32_t;   // Must be 32-bit for 7-Zip ABI compatibility on LP64 Linux
using ULONG = uint32_t;
using DWORD = uint32_t;
using HRESULT = int32_t;
using PROPID = uint32_t;
using VARTYPE = uint16_t;
using VARIANT_BOOL = int16_t;
using OLECHAR = wchar_t;
using BSTR = OLECHAR *;

constexpr VARIANT_BOOL VARIANT_TRUE  = -1;
constexpr VARIANT_BOOL VARIANT_FALSE = 0;

constexpr HRESULT S_OK                      = static_cast<HRESULT>(0x00000000);
constexpr HRESULT S_FALSE                   = static_cast<HRESULT>(0x00000001);
constexpr HRESULT E_NOTIMPL                 = static_cast<HRESULT>(0x80004001u);
constexpr HRESULT E_NOINTERFACE             = static_cast<HRESULT>(0x80004002u);
constexpr HRESULT E_POINTER                 = static_cast<HRESULT>(0x80004003u);
constexpr HRESULT E_ABORT                   = static_cast<HRESULT>(0x80004004u);
constexpr HRESULT E_FAIL                    = static_cast<HRESULT>(0x80004005u);
constexpr HRESULT E_OUTOFMEMORY             = static_cast<HRESULT>(0x8007000Eu);
constexpr HRESULT E_INVALIDARG              = static_cast<HRESULT>(0x80070057u);
constexpr HRESULT CLASS_E_CLASSNOTAVAILABLE = static_cast<HRESULT>(0x80040111u);

inline bool SUCCEEDED(HRESULT hr) noexcept { return hr >= 0; }
inline bool FAILED(HRESULT hr) noexcept { return hr < 0; }

struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
};

using REFGUID = const GUID &;
using REFIID  = const GUID &;

inline bool operator==(REFGUID a, REFGUID b) noexcept {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}
inline bool operator!=(REFGUID a, REFGUID b) noexcept {
    return !(a == b);
}

inline constexpr GUID IID_IUnknown = {
    0x00000000u, 0x0000u, 0x0000u,
    { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

struct LARGE_INTEGER  { int64_t  QuadPart; };
struct ULARGE_INTEGER { uint64_t QuadPart; };
struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

enum VARENUM : VARTYPE {
    VT_EMPTY    = 0,
    VT_NULL     = 1,
    VT_I2       = 2,
    VT_I4       = 3,
    VT_BSTR     = 8,
    VT_BOOL     = 11,
    VT_UI1      = 17,
    VT_UI2      = 18,
    VT_UI4      = 19,
    VT_I8       = 20,
    VT_UI8      = 21,
    VT_FILETIME = 64
};

struct PROPVARIANT {
    VARTYPE vt;
    WORD wReserved1;
    WORD wReserved2;
    WORD wReserved3;
    union {
        CHAR cVal;
        UCHAR bVal;
        SHORT iVal;
        USHORT uiVal;
        LONG lVal;
        ULONG ulVal;
        INT intVal;
        UINT uintVal;
        LARGE_INTEGER hVal;
        ULARGE_INTEGER uhVal;
        VARIANT_BOOL boolVal;
        LONG scode;
        FILETIME filetime;
        BSTR bstrVal;
    };
};

enum : uint32_t {
    STREAM_SEEK_SET = 0,
    STREAM_SEEK_CUR = 1,
    STREAM_SEEK_END = 2
};

#define STDMETHOD(m)      virtual HRESULT m
#define STDMETHOD_(t, m)  virtual t m
#define STDMETHODIMP      HRESULT
#define STDMETHODIMP_(t)  t

struct IUnknown {
    STDMETHOD(QueryInterface)(REFIID iid, void **outObject) noexcept = 0;
    STDMETHOD_(ULONG, AddRef)() noexcept = 0;
    STDMETHOD_(ULONG, Release)() noexcept = 0;
#ifdef Z7_USE_VIRTUAL_DESTRUCTOR_IN_IUNKNOWN
    virtual ~IUnknown() noexcept {}
#endif
};

#endif // _WIN32

// Minimal, ABI-exact 7-Zip plugin COM interface definitions (compatible with 7-Zip 9.20 through 24.x+
// on Windows and modular 7-Zip 23+ on Linux).

namespace SevenZip {

using Byte = uint8_t;
using Int32 = int32_t;
using UInt32 = uint32_t;
using Int64 = int64_t;
using UInt64 = uint64_t;

// 7-Zip GUID base: {23170F69-40C1-278A-0000-00XX00YY0000}
constexpr GUID Make7zGuid(Byte groupId, Byte subId) noexcept {
    return GUID{
        0x23170F69u,
        0x40C1u,
        0x278Au,
        { 0x00, 0x00, 0x00, groupId, 0x00, subId, 0x00, 0x00 }
    };
}

// Unique CLSID for our Tribes 1 / DarkStar PVOL archive handler:
// {7A4B91C2-3D5E-4F81-9E12-545249424556} ("TRIBEV")
inline constexpr GUID CLSID_TribesVolHandler = {
    0x7A4B91C2u, 0x3D5Eu, 0x4F81u,
    { 0x9E, 0x12, 0x54, 0x52, 0x49, 0x42, 0x45, 0x56 }
};

// Standard 7-Zip Interface IDs
inline constexpr GUID IID_IProgress                 = Make7zGuid(0x00, 0x05);
inline constexpr GUID IID_ISequentialInStream       = Make7zGuid(0x03, 0x01);
inline constexpr GUID IID_ISequentialOutStream      = Make7zGuid(0x03, 0x02);
inline constexpr GUID IID_IInStream                 = Make7zGuid(0x03, 0x03);
inline constexpr GUID IID_IOutStream                = Make7zGuid(0x03, 0x04);
inline constexpr GUID IID_IStreamGetSize            = Make7zGuid(0x03, 0x06);
inline constexpr GUID IID_IStreamSetRestriction     = Make7zGuid(0x03, 0x10);
inline constexpr GUID IID_ISetProperties            = Make7zGuid(0x06, 0x03);
inline constexpr GUID IID_IArchiveOpenCallback      = Make7zGuid(0x06, 0x10);
inline constexpr GUID IID_IArchiveExtractCallback   = Make7zGuid(0x06, 0x20);
inline constexpr GUID IID_IInArchiveGetStream       = Make7zGuid(0x06, 0x40);
inline constexpr GUID IID_IInArchive                = Make7zGuid(0x06, 0x60);
inline constexpr GUID IID_IArchiveUpdateCallback    = Make7zGuid(0x06, 0x80);
inline constexpr GUID IID_IArchiveUpdateCallbackFile= Make7zGuid(0x06, 0x83);
inline constexpr GUID IID_IOutArchive               = Make7zGuid(0x06, 0xA0);

// Property IDs (PropID.h)
enum PropID : PROPID {
    kpidNoProperty        = 0,
    kpidMainSubfile       = 1,
    kpidHandlerItemIndex  = 2,
    kpidPath              = 3,
    kpidName              = 4,
    kpidExtension         = 5,
    kpidIsDir             = 6,
    kpidSize              = 7,
    kpidPackSize          = 8,
    kpidAttrib            = 9,
    kpidCTime             = 10,
    kpidATime             = 11,
    kpidMTime             = 12,
    kpidSolid             = 13,
    kpidCommented         = 14,
    kpidEncrypted         = 15,
    kpidSplitBefore       = 16,
    kpidSplitAfter        = 17,
    kpidDictionarySize    = 18,
    kpidCRC               = 19,
    kpidType              = 20,
    kpidIsAnti            = 21,
    kpidMethod            = 22,
    kpidOffset            = 36,
    kpidPhySize           = 44,
    kpidHeadersSize       = 45,
    kpidErrorFlags        = 71,
    kpidWarningFlags      = 72,
    kpidWarning           = 73
};

namespace NFileTimeType {
    enum EEnum : UInt32 {
        kWindows = 0,
        kUnix    = 1,
        kDOS     = 2,
        k1ns     = 3
    };
}

namespace NArcInfoFlags {
    constexpr UInt32 kKeepName        = 1u << 0;
    constexpr UInt32 kAltStreams      = 1u << 1;
    constexpr UInt32 kNtSecure        = 1u << 2;
    constexpr UInt32 kFindSignature   = 1u << 3;
    constexpr UInt32 kMultiSignature  = 1u << 4;
    constexpr UInt32 kUseGlobalOffset = 1u << 5;
    constexpr UInt32 kStartOpen       = 1u << 6;
    constexpr UInt32 kPureStartOpen   = 1u << 7;
    constexpr UInt32 kBackwardOpen    = 1u << 8;
    constexpr UInt32 kPreArc          = 1u << 9;
    constexpr UInt32 kSymLinks        = 1u << 10;
    constexpr UInt32 kHardLinks       = 1u << 11;
    constexpr UInt32 kByExtOnlyOpen   = 1u << 12;
}

namespace NHandlerPropID {
    enum : PROPID {
        kName            = 0,
        kClassID         = 1,
        kExtension       = 2,
        kAddExtension    = 3,
        kUpdate          = 4,
        kKeepName        = 5,
        kSignature       = 6,
        kMultiSignature  = 7,
        kSignatureOffset = 8,
        kAltStreams      = 9,
        kNtSecure        = 10,
        kFlags           = 11,
        kTimeFlags       = 12
    };
}

namespace NModulePropID {
    enum : PROPID {
        kInterfaceType = 0,
        kVersion       = 1
    };
}

namespace NModuleInterfaceType {
    constexpr UInt32 k_IUnknown_VirtDestructor_No  = 0;
    constexpr UInt32 k_IUnknown_VirtDestructor_Yes = 1;
}

namespace NExtract {
    namespace NAskMode {
        enum : Int32 {
            kExtract      = 0,
            kTest         = 1,
            kSkip         = 2,
            kReadExternal = 3
        };
    }

    namespace NOperationResult {
        enum : Int32 {
            kOK                = 0,
            kUnsupportedMethod = 1,
            kDataError         = 2,
            kCRCError          = 3,
            kUnavailable       = 4,
            kUnexpectedEnd     = 5,
            kDataAfterEnd      = 6,
            kIsNotArc          = 7,
            kHeadersError      = 8,
            kWrongPassword     = 9
        };
    }
}

namespace NUpdate {
    namespace NOperationResult {
        enum : Int32 {
            kOK = 0
        };
    }
}

namespace NIsArc {
    constexpr UInt32 kNo       = 0;
    constexpr UInt32 kYes      = 1;
    constexpr UInt32 kNeedMore = 2;
}

// ---------- COM Interfaces ----------

struct TRIBESVOL_NOVTABLE IProgress : public IUnknown {
    STDMETHOD(SetTotal)(UInt64 total) noexcept = 0;
    STDMETHOD(SetCompleted)(const UInt64 *completeValue) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE ISequentialInStream : public IUnknown {
    STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE ISequentialOutStream : public IUnknown {
    STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IInStream : public ISequentialInStream {
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IOutStream : public ISequentialOutStream {
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition) noexcept = 0;
    STDMETHOD(SetSize)(UInt64 newSize) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IStreamGetSize : public IUnknown {
    STDMETHOD(GetSize)(UInt64 *size) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IStreamSetRestriction : public IUnknown {
    STDMETHOD(SetRestriction)(UInt64 begin, UInt64 end) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IArchiveOpenCallback : public IUnknown {
    STDMETHOD(SetTotal)(const UInt64 *files, const UInt64 *bytes) noexcept = 0;
    STDMETHOD(SetCompleted)(const UInt64 *files, const UInt64 *bytes) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IArchiveExtractCallback : public IProgress {
    STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode) noexcept = 0;
    STDMETHOD(PrepareOperation)(Int32 askExtractMode) noexcept = 0;
    STDMETHOD(SetOperationResult)(Int32 opRes) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IInArchiveGetStream : public IUnknown {
    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **stream) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IInArchive : public IUnknown {
    STDMETHOD(Open)(IInStream *stream, const UInt64 *maxCheckStartPosition, IArchiveOpenCallback *openCallback) noexcept = 0;
    STDMETHOD(Close)() noexcept = 0;
    STDMETHOD(GetNumberOfItems)(UInt32 *numItems) noexcept = 0;
    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value) noexcept = 0;
    STDMETHOD(Extract)(const UInt32 *indices, UInt32 numItems, Int32 testMode, IArchiveExtractCallback *extractCallback) noexcept = 0;
    STDMETHOD(GetArchiveProperty)(PROPID propID, PROPVARIANT *value) noexcept = 0;
    STDMETHOD(GetNumberOfProperties)(UInt32 *numProps) noexcept = 0;
    STDMETHOD(GetPropertyInfo)(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType) noexcept = 0;
    STDMETHOD(GetNumberOfArchiveProperties)(UInt32 *numProps) noexcept = 0;
    STDMETHOD(GetArchivePropertyInfo)(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IArchiveUpdateCallback : public IProgress {
    STDMETHOD(GetUpdateItemInfo)(UInt32 index, Int32 *newData, Int32 *newProps, UInt32 *indexInArchive) noexcept = 0;
    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value) noexcept = 0;
    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream) noexcept = 0;
    STDMETHOD(SetOperationResult)(Int32 operationResult) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE IOutArchive : public IUnknown {
    STDMETHOD(UpdateItems)(ISequentialOutStream *outStream, UInt32 numItems, IArchiveUpdateCallback *updateCallback) noexcept = 0;
    STDMETHOD(GetFileTimeType)(UInt32 *type) noexcept = 0;
};

struct TRIBESVOL_NOVTABLE ISetProperties : public IUnknown {
    STDMETHOD(SetProperties)(const wchar_t *const *names, const PROPVARIANT *values, UInt32 numProps) noexcept = 0;
};

// Exported IsArc callback type
typedef UInt32(WINAPI *Func_IsArc)(const Byte *p, size_t size);

// ---------- Cross-platform BSTR helpers ----------

inline BSTR AllocBstr(const wchar_t *str) noexcept {
    if (!str) return nullptr;
#ifdef _WIN32
    return ::SysAllocString(str);
#else
    const size_t len = std::wcslen(str);
    const uint32_t byteSize = static_cast<uint32_t>(len * sizeof(OLECHAR));
    void *p = std::malloc(static_cast<size_t>(byteSize) + sizeof(uint32_t) + sizeof(OLECHAR));
    if (!p) return nullptr;
    *static_cast<uint32_t *>(p) = byteSize;
    auto *bstr = reinterpret_cast<BSTR>(static_cast<uint32_t *>(p) + 1);
    if (byteSize > 0) {
        std::memcpy(bstr, str, byteSize);
    }
    bstr[len] = 0;
    return bstr;
#endif
}

inline BSTR AllocBstrBytes(const void *data, UINT byteLen) noexcept {
#ifdef _WIN32
    return ::SysAllocStringByteLen(static_cast<const char *>(data), byteLen);
#else
    const uint32_t aligned = (byteLen + static_cast<uint32_t>(sizeof(OLECHAR)) * 2u - 1u)
                           & ~(static_cast<uint32_t>(sizeof(OLECHAR)) - 1u);
    void *p = std::malloc(static_cast<size_t>(aligned) + sizeof(uint32_t));
    if (!p) return nullptr;
    *static_cast<uint32_t *>(p) = byteLen;
    auto *bstr = reinterpret_cast<BSTR>(static_cast<uint32_t *>(p) + 1);
    if (data && byteLen > 0) {
        std::memcpy(bstr, data, byteLen);
    }
    std::memset(reinterpret_cast<uint8_t *>(bstr) + byteLen, 0, aligned - byteLen);
    return bstr;
#endif
}

inline void FreeBstr(BSTR bstr) noexcept {
    if (!bstr) return;
#ifdef _WIN32
    ::SysFreeString(bstr);
#else
    std::free(reinterpret_cast<uint32_t *>(bstr) - 1);
#endif
}

// ---------- Small RAII helpers ----------

template <typename T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    explicit ComPtr(T *p) noexcept : ptr_(p) {
        if (ptr_) ptr_->AddRef();
    }
    ComPtr(const ComPtr &other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }
    ComPtr(ComPtr &&other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    ~ComPtr() noexcept { Release(); }

    ComPtr &operator=(const ComPtr &other) noexcept {
        if (this != &other) {
            Release();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->AddRef();
        }
        return *this;
    }
    ComPtr &operator=(ComPtr &&other) noexcept {
        if (this != &other) {
            Release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    void Release() noexcept {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T *Get() const noexcept { return ptr_; }
    T *operator->() const noexcept { return ptr_; }
    T **AddressOf() noexcept {
        Release();
        return &ptr_;
    }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T *ptr_ = nullptr;
};

inline void ClearPropVariant(PROPVARIANT *prop) noexcept {
    if (!prop) return;
    if (prop->vt == VT_BSTR && prop->bstrVal) {
        FreeBstr(prop->bstrVal);
    }
    std::memset(prop, 0, sizeof(PROPVARIANT));
    prop->vt = VT_EMPTY;
}

class PropVariantGuard {
public:
    PROPVARIANT value{};
    PropVariantGuard() noexcept { value.vt = VT_EMPTY; }
    ~PropVariantGuard() noexcept { ClearPropVariant(&value); }
    PropVariantGuard(const PropVariantGuard &) = delete;
    PropVariantGuard &operator=(const PropVariantGuard &) = delete;
};

inline HRESULT SetPropString(PROPVARIANT *out, const wchar_t *str) noexcept {
    ClearPropVariant(out);
    if (!str) return S_OK;
    BSTR b = AllocBstr(str);
    if (!b) return E_OUTOFMEMORY;
    out->vt = VT_BSTR;
    out->bstrVal = b;
    return S_OK;
}

inline HRESULT SetPropBinary(PROPVARIANT *out, const void *data, UINT byteLen) noexcept {
    ClearPropVariant(out);
    BSTR b = AllocBstrBytes(data, byteLen);
    if (!b) return E_OUTOFMEMORY;
    out->vt = VT_BSTR;
    out->bstrVal = b;
    return S_OK;
}

inline HRESULT SetPropBool(PROPVARIANT *out, bool v) noexcept {
    ClearPropVariant(out);
    out->vt = VT_BOOL;
    out->boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

inline HRESULT SetPropUInt32(PROPVARIANT *out, UInt32 v) noexcept {
    ClearPropVariant(out);
    out->vt = VT_UI4;
    out->ulVal = v;
    return S_OK;
}

inline HRESULT SetPropUInt64(PROPVARIANT *out, UInt64 v) noexcept {
    ClearPropVariant(out);
    out->vt = VT_UI8;
    out->uhVal.QuadPart = v;
    return S_OK;
}

// Stream I/O helpers handling partial reads/writes cleanly.
inline HRESULT ReadExact(ISequentialInStream *stream, void *buf, size_t size) noexcept {
    auto *dst = static_cast<Byte *>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        UInt32 chunk = remaining > 0x100000u ? 0x100000u : static_cast<UInt32>(remaining);
        UInt32 got = 0;
        HRESULT hr = stream->Read(dst, chunk, &got);
        if (FAILED(hr)) return hr;
        if (got == 0) return S_FALSE;
        dst += got;
        remaining -= got;
    }
    return S_OK;
}

inline HRESULT WriteAll(ISequentialOutStream *stream, const void *buf, size_t size) noexcept {
    const auto *src = static_cast<const Byte *>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        UInt32 chunk = remaining > 0x100000u ? 0x100000u : static_cast<UInt32>(remaining);
        UInt32 written = 0;
        HRESULT hr = stream->Write(src, chunk, &written);
        if (FAILED(hr)) return hr;
        if (written == 0) return E_FAIL;
        src += written;
        remaining -= written;
    }
    return S_OK;
}

} // namespace SevenZip
