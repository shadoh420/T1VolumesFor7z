#include "SevenZipApi.h"
#include "VolFormat.h"
#include "VolHandler.h"

#include <cstring>

using namespace SevenZip;

namespace {

// MultiSignature format in 7-Zip: sequence of [len, bytes...].
// Matches Tribes 1 ("PVOL") and Starsiege (" VOL") volumes at offset 0.
constexpr uint8_t kVolMultiSignature[] = {
    4, 'P', 'V', 'O', 'L',
    4, ' ', 'V', 'O', 'L'
};

UInt32 WINAPI IsTribesVolArc(const Byte *p, size_t size) {
    return Darkstar::CheckIsArc(p, size);
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/, DWORD /*fdwReason*/, LPVOID /*lpvReserved*/) {
    return TRUE;
}

extern "C" {

STDAPI CreateObject(const GUID *clsID, const GUID *iid, void **outObject) {
    if (!clsID || !iid || !outObject) {
        return E_INVALIDARG;
    }
    *outObject = nullptr;

    if (*clsID != CLSID_TribesVolHandler) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto *handler = new (std::nothrow) Darkstar::VolHandler();
    if (!handler) {
        return E_OUTOFMEMORY;
    }

    handler->AddRef();
    const HRESULT hr = handler->QueryInterface(*iid, outObject);
    handler->Release();
    return hr;
}

STDAPI GetNumberOfFormats(UInt32 *numFormats) {
    if (!numFormats) return E_POINTER;
    *numFormats = 1;
    return S_OK;
}

STDAPI GetHandlerProperty2(UInt32 index, PROPID propID, PROPVARIANT *value) {
    if (!value) return E_POINTER;
    ClearPropVariant(value);
    if (index != 0) return E_INVALIDARG;

    switch (propID) {
        case NHandlerPropID::kName:
            return SetPropString(value, L"TribesVol");

        case NHandlerPropID::kClassID:
            return SetPropBinary(value, &CLSID_TribesVolHandler, sizeof(GUID));

        case NHandlerPropID::kExtension:
            return SetPropString(value, L"vol");

        case NHandlerPropID::kUpdate:
            return SetPropBool(value, true);

        case NHandlerPropID::kKeepName:
            return SetPropBool(value, false);

        case NHandlerPropID::kMultiSignature:
            return SetPropBinary(value, kVolMultiSignature, sizeof(kVolMultiSignature));

        case NHandlerPropID::kSignatureOffset:
            return SetPropUInt32(value, 0);

        case NHandlerPropID::kFlags:
            return SetPropUInt32(value, NArcInfoFlags::kMultiSignature | NArcInfoFlags::kPureStartOpen);

        default:
            return S_OK;
    }
}

STDAPI GetHandlerProperty(PROPID propID, PROPVARIANT *value) {
    return GetHandlerProperty2(0, propID, value);
}

STDAPI GetIsArc(UInt32 formatIndex, Func_IsArc *isArc) {
    if (!isArc) return E_POINTER;
    *isArc = nullptr;
    if (formatIndex != 0) return E_INVALIDARG;
    *isArc = &IsTribesVolArc;
    return S_OK;
}

STDAPI GetModuleProp(PROPID propID, PROPVARIANT *value) {
    if (!value) return E_POINTER;
    ClearPropVariant(value);

    switch (propID) {
        case NModulePropID::kInterfaceType:
            return SetPropUInt32(value, NModuleInterfaceType::k_IUnknown_VirtDestructor_No);

        case NModulePropID::kVersion: {
            constexpr UInt32 kVer = (static_cast<UInt32>(TRIBESVOL_VER_MAJOR) << 16)
                                  | static_cast<UInt32>(TRIBESVOL_VER_MINOR);
            return SetPropUInt32(value, kVer);
        }

        default:
            return S_OK;
    }
}

} // extern "C"
