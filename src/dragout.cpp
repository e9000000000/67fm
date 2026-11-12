#include "dragout.h"

#include <objidl.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace {

constexpr wchar_t kPreferredDropEffect[] = L"Preferred DropEffect";

// Simple IEnumFORMATETC over a fixed array of formats.
class FormatEnumerator final : public IEnumFORMATETC {
public:
    FormatEnumerator(const FORMATETC* formats, UINT count, UINT index)
        : formats_(formats), count_(count), index_(index) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = static_cast<ULONG>(InterlockedDecrement(&ref_));
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* out, ULONG* fetched) override {
        ULONG copied = 0;
        while (copied < count && index_ < count_) {
            out[copied] = formats_[index_];
            ++copied;
            ++index_;
        }
        if (fetched) {
            *fetched = copied;
        }
        return copied == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        index_ = std::min<UINT>(count_, index_ + count);
        return index_ < count_ ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override {
        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) {
            return E_POINTER;
        }
        *out = new (std::nothrow) FormatEnumerator(formats_, count_, index_);
        return *out ? S_OK : E_OUTOFMEMORY;
    }

private:
    ~FormatEnumerator() = default;

    const FORMATETC* formats_ = nullptr;
    UINT count_ = 0;
    UINT index_ = 0;
    LONG ref_ = 1;
};

HGLOBAL CreateDropFiles(const std::vector<std::wstring>& paths) {
    size_t bytes = sizeof(DROPFILES) + sizeof(wchar_t);
    for (const auto& path : paths) {
        bytes += (path.size() + 1) * sizeof(wchar_t);
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!memory) {
        return nullptr;
    }
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    if (!drop) {
        GlobalFree(memory);
        return nullptr;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    for (const auto& path : paths) {
        memcpy(cursor, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
        cursor += path.size() + 1;
    }
    *cursor = L'\0';
    GlobalUnlock(memory);
    return memory;
}

class FileDataObject final : public IDataObject {
public:
    explicit FileDataObject(std::vector<std::wstring> paths) : paths_(std::move(paths)) {
        formats_[0].cfFormat = static_cast<CLIPFORMAT>(CF_HDROP);
        formats_[1].cfFormat =
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(kPreferredDropEffect));
        formats_[2].cfFormat = static_cast<CLIPFORMAT>(CF_UNICODETEXT);
        for (auto& format : formats_) {
            format.ptd = nullptr;
            format.dwAspect = DVASPECT_CONTENT;
            format.lindex = -1;
            format.tymed = TYMED_HGLOBAL;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = static_cast<ULONG>(InterlockedDecrement(&ref_));
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (!format || !medium) {
            return E_INVALIDARG;
        }
        if (format->lindex != -1 || (format->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_FORMATETC;
        }
        if (format->cfFormat == CF_HDROP) {
            HGLOBAL memory = CreateDropFiles(paths_);
            if (!memory) {
                return E_OUTOFMEMORY;
            }
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = memory;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (format->cfFormat == CF_UNICODETEXT) {
            std::wstring text;
            for (size_t i = 0; i < paths_.size(); ++i) {
                if (i) {
                    text += L"\r\n";
                }
                text += paths_[i];
            }
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!memory) {
                return E_OUTOFMEMORY;
            }
            if (void* target = GlobalLock(memory)) {
                memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);
            }
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = memory;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (format->cfFormat == formats_[1].cfFormat) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
            if (!memory) {
                return E_OUTOFMEMORY;
            }
            if (auto* effect = static_cast<DWORD*>(GlobalLock(memory))) {
                *effect = DROPEFFECT_COPY;
                GlobalUnlock(memory);
            }
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = memory;
            medium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (!format) {
            return E_INVALIDARG;
        }
        if (format->lindex != -1 || (format->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_FORMATETC;
        }
        if (format->cfFormat == CF_HDROP || format->cfFormat == CF_UNICODETEXT ||
            format->cfFormat == formats_[1].cfFormat) {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) {
            *out = FORMATETC{};
            out->ptd = nullptr;
            out->dwAspect = DVASPECT_CONTENT;
            out->lindex = -1;
            out->tymed = TYMED_HGLOBAL;
        }
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        if (direction != DATADIR_GET) {
            return E_NOTIMPL;
        }
        *out = new (std::nothrow) FormatEnumerator(formats_, 3, 0);
        return *out ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    ~FileDataObject() = default;

    std::vector<std::wstring> paths_;
    FORMATETC formats_[3]{};
    LONG ref_ = 1;
};

class FileDropSource final : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = static_cast<ULONG>(InterlockedDecrement(&ref_));
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keys) override {
        if (escapePressed) {
            return DRAGDROP_S_CANCEL;
        }
        if ((keys & (MK_LBUTTON | MK_RBUTTON)) == 0) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    ~FileDropSource() = default;

    LONG ref_ = 1;
};

}  // namespace

IDataObject* CreateFileDataObject(const std::vector<std::wstring>& paths) {
    return new (std::nothrow) FileDataObject(paths);
}

DWORD BeginFileDrag(const std::vector<std::wstring>& paths) {
    if (paths.empty()) {
        return DROPEFFECT_NONE;
    }
    IDataObject* data = CreateFileDataObject(paths);
    if (!data) {
        return DROPEFFECT_NONE;
    }
    auto* source = new (std::nothrow) FileDropSource();
    if (!source) {
        data->Release();
        return DROPEFFECT_NONE;
    }
    DWORD effect = DROPEFFECT_NONE;
    const DWORD allowed = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    const HRESULT status = DoDragDrop(data, source, allowed, &effect);
    source->Release();
    data->Release();
    if (FAILED(status)) {
        return DROPEFFECT_NONE;
    }
    return effect;
}
