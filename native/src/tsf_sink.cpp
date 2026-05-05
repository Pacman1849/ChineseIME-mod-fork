#include "tsf_sink.h"
#include "common.h"
#include "sta_thread.h"
#include <comdef.h>
#include <algorithm>
#include <msctf.h>
#include <windows.h>
#include <imm.h>

#pragma comment(lib, "imm32.lib")

#ifdef CHINESEIME_DEBUG
#define DEBUG_LOG(format, ...) do { \
    wchar_t buf[512]; \
    swprintf_s(buf, format, __VA_ARGS__); \
    OutputDebugStringW(buf); \
} while(0)
#define DEBUG_LOG_SIMPLE(msg) OutputDebugStringW(msg)
#else
#define DEBUG_LOG(format, ...)
#define DEBUG_LOG_SIMPLE(msg)
#endif

#ifndef GUID_COMPARTMENT_KEYBOARD_INPUTMODE
static const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE =
{ 0x5147C989, 0x49A1, 0x46EC, { 0x8F, 0x15, 0x75, 0x9C, 0x45, 0x7D, 0x12, 0x4D } };
#endif

// Microsoft Input Method GUIDs
static const GUID GUID_MS_PINYIN =
{ 0x81d4e9c9, 0x1d3b, 0x41bc, { 0x9e, 0x6c, 0x4b, 0x40, 0xbf, 0x79, 0xe3, 0x5e } };

static const GUID GUID_MS_ZHUYIN =
{ 0xb115690a, 0xea02, 0x48d5, { 0xa2, 0x31, 0xe3, 0x57, 0x8d, 0x2f, 0x0c, 0x52 } };

static const GUID GUID_MS_CANGJIE =
{ 0x4bdf9f03, 0xc7d3, 0x11d4, { 0xb2, 0xab, 0x00, 0x80, 0xc8, 0x82, 0x68, 0x7e } };

static const GUID GUID_MS_WUBI =
{ 0x82590c13, 0xf4dd, 0x44f4, { 0xba, 0x1d, 0x86, 0x67, 0x24, 0x6f, 0xdf, 0x8e } };

static const GUID GUID_MS_SUCHENG =
{ 0x6024b45f, 0x5c54, 0x11d4, { 0xb9, 0x21, 0x00, 0x80, 0xc8, 0x82, 0x68, 0x7e } };

#ifndef GUID_PROP_CANDIDATE
static const GUID GUID_PROP_CANDIDATE =
{ 0xf3a465f7, 0x6be7, 0x4dfb, { 0x82, 0x3a, 0xcf, 0x59, 0x47, 0x16, 0x86, 0x1c } };
#endif

namespace chineseime {

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    if (langId != 0x0804 && langId != 0x0404 && langId != 0x0C04 && langId != 0x1404) {
        return InputMethodType::ENGLISH;
    }
    switch (imeId) {
        case 0x0001: case 0x0010: case 0xE010: case 0xE020: return InputMethodType::PINYIN;
        case 0x0002: case 0xE011: return InputMethodType::WUBI;
        case 0x0003: case 0xE001: return InputMethodType::ZHUYIN;
        case 0x0004: case 0xE002: return InputMethodType::CANGJIE;
        case 0x0005: case 0xE003: return InputMethodType::SUCHENG;
        default: return InputMethodType::OTHER_CHINESE;
    }
}

TsfSink::TsfSink() {
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfSink created\n");
}

TsfSink::~TsfSink() {
    shutdown();
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfSink destroyed\n");
}

bool TsfSink::initialize(IUnknown* pThreadMgr) {
    if (!pThreadMgr) return false;

    HRESULT hr = pThreadMgr->QueryInterface(IID_ITfThreadMgr, (void**)&threadMgr_);
    if (FAILED(hr)) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Failed to get ITfThreadMgr\n");
        return false;
    }

    hr = threadMgr_->GetFocus(&docMgr_);
    if (FAILED(hr) || !docMgr_) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] No focused document manager\n");
    }

    ITfSource* source = nullptr;
    hr = threadMgr_->QueryInterface(IID_ITfSource, (void**)&source);
    if (SUCCEEDED(hr) && source) {
        hr = source->AdviseSink(IID_ITfInputProcessorProfileActivationSink,
            static_cast<ITfInputProcessorProfileActivationSink*>(this), &profileSinkCookie_);
        if (SUCCEEDED(hr)) {
            threadMgrSource_ = source;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] Profile sink registered\n");
        } else {
            source->Release();
        }
    }

    if (docMgr_) {
        ITfContext* ctx = nullptr;
        hr = docMgr_->GetTop(&ctx);
        if (SUCCEEDED(hr) && ctx) {
            registerSinks(ctx);
            ctx->Release();
        }
    }

    updateCache();
    return true;
}

void TsfSink::shutdown() {
    unregisterSinks();

    if (threadMgrSource_) {
        if (profileSinkCookie_ != TF_INVALID_COOKIE) {
            threadMgrSource_->UnadviseSink(profileSinkCookie_);
            profileSinkCookie_ = TF_INVALID_COOKIE;
        }
        threadMgrSource_->Release();
        threadMgrSource_ = nullptr;
    }
    if (docMgr_) {
        docMgr_->Release();
        docMgr_ = nullptr;
    }
    if (threadMgr_) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }

    ecReadOnly_ = 0;
    lastHkl_ = 0;
    lastChineseMode_ = false;
    currentInputMethod_ = InputMethodType::UNKNOWN;
}

void TsfSink::refreshState() {
    updateCache();
}

// IUnknown
STDMETHODIMP TsfSink::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfInputProcessorProfileActivationSink) {
        *ppv = static_cast<ITfInputProcessorProfileActivationSink*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TsfSink::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) TsfSink::Release() {
    LONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

// ITfTextEditSink
STDMETHODIMP TsfSink::OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) {
    if (!pic) return E_INVALIDARG;
    ecReadOnly_ = ecReadOnly;

    auto oldState = g_cache.snapshot();
    updateCache();
    auto newState = g_cache.snapshot();
    notifyStateChanges(oldState, newState);
    return S_OK;
}

// ITfKeyEventSink
STDMETHODIMP TsfSink::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfSink::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfSink::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfSink::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfSink::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfSink::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        updateCache();
    }
    return S_OK;
}

// ITfInputProcessorProfileActivationSink
STDMETHODIMP TsfSink::OnActivated(DWORD dwProfileType, LANGID langid, REFCLSID clsid,
                                   REFGUID guidProfile, REFGUID guidCat, HKL hkl, DWORD dwFlags) {
    updateInputMethodType(langid, clsid, guidProfile);

    DEBUG_LOG(L"[ChineseIME] OnActivated: type=%d, langid=0x%04x, hkl=0x%08X, flags=0x%x\n",
              (int)currentInputMethod_, langid, (DWORD)(DWORD_PTR)hkl, dwFlags);

    if (dwFlags & TF_IPSINK_FLAG_ACTIVE) {
        auto state = g_cache.snapshot();
        state.imeOpen = true;
        state.layoutChangeCount++;

        DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
        LANGID hklLangId = LOWORD(hklValue);
        WORD imeId = HIWORD(hklValue);

        state.hkl = static_cast<long>(hklValue);

        // Use GUID detection result first, fallback to HKL detection
        if (currentInputMethod_ != InputMethodType::UNKNOWN && currentInputMethod_ != InputMethodType::ENGLISH) {
            state.inputMethodType = currentInputMethod_;
        } else {
            state.inputMethodType = detectInputMethodTypeFromImeId(imeId, hklLangId);
        }

        bool isChineseLang = (hklLangId == 0x0804 || hklLangId == 0x0404 ||
                             hklLangId == 0x0C04 || hklLangId == 0x1404);
        state.chineseMode = isChineseLang;
        chineseMode_ = isChineseLang;

        g_cache.update(state);

        // Always notify layout change when activated
        if (g_onLayoutChange) {
            g_onLayoutChange(static_cast<int>(state.inputMethodType));
        }
    }

    return S_OK;
}

bool TsfSink::detectChineseMode() {
    // Try TSF compartment first
    if (threadMgr_) {
        ITfDocumentMgr* docMgr = nullptr;
        HRESULT hr = threadMgr_->GetFocus(&docMgr);
        if (SUCCEEDED(hr) && docMgr) {
            ITfContext* ctx = nullptr;
            hr = docMgr->GetTop(&ctx);
            docMgr->Release();
            if (SUCCEEDED(hr) && ctx) {
                ITfCompartmentMgr* compMgr = nullptr;
                hr = ctx->QueryInterface(IID_ITfCompartmentMgr, (void**)&compMgr);
                ctx->Release();
                if (SUCCEEDED(hr) && compMgr) {
                    ITfCompartment* comp = nullptr;
                    hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE, &comp);
                    compMgr->Release();
                    if (SUCCEEDED(hr) && comp) {
                        VARIANT var;
                        VariantInit(&var);
                        hr = comp->GetValue(&var);
                        comp->Release();
                        if (SUCCEEDED(hr) && var.vt == VT_I4) {
                            chineseMode_ = (var.lVal & 0x0001) != 0;
                            VariantClear(&var);
                            return chineseMode_;
                        }
                        VariantClear(&var);
                    }
                }
            }
        }
    }

    // Fallback: Use ImmGetConversionStatus
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd) {
        HIMC himc = ImmGetContext(fgWnd);
        if (himc) {
            DWORD conversion = 0;
            DWORD sentence = 0;
            if (ImmGetConversionStatus(himc, &conversion, &sentence)) {
                ImmReleaseContext(fgWnd, himc);
                chineseMode_ = (conversion & IME_CMODE_NATIVE) != 0;
                return chineseMode_;
            }
            ImmReleaseContext(fgWnd, himc);
        }
    }

    return chineseMode_;
}

void TsfSink::forceUpdateChineseMode() {
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd) {
        HIMC himc = ImmGetContext(fgWnd);
        if (himc) {
            DWORD conversion = 0;
            DWORD sentence = 0;
            if (ImmGetConversionStatus(himc, &conversion, &sentence)) {
                ImmReleaseContext(fgWnd, himc);
                chineseMode_ = (conversion & IME_CMODE_NATIVE) != 0;
                return;
            }
            ImmReleaseContext(fgWnd, himc);
        }
    }
    chineseMode_ = true;
}

void TsfSink::updateCache() {
    auto state = g_cache.snapshot();
    state.isValid = true;

    ITfContext* ctx = getCurrentContext();
    bool gotCandidatesFromTsf = false;
    if (ctx) {
        getCompositionString(ctx, state.composition);
        if (getCandidateList(ctx, state.candidates, state.selectedIndex)) {
            gotCandidatesFromTsf = true;
        }
        ctx->Release();
    }

    if (!gotCandidatesFromTsf) {
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) {
            HIMC himc = ImmGetContext(fgWnd);
            if (himc) {
                LONG compLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, nullptr, 0);
                if (compLen > 0 && state.composition.empty()) {
                    wchar_t* compBuf = new wchar_t[compLen + 1];
                    ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf, compLen);
                    compBuf[compLen] = 0;
                    state.composition = compBuf;
                    delete[] compBuf;
                }

                size_t bufSize = ImmGetCandidateList(himc, 0, nullptr, 0);
                if (bufSize > 0) {
                    CANDIDATELIST* candList = (CANDIDATELIST*)new char[bufSize];
                    ImmGetCandidateList(himc, 0, candList, bufSize);
                    DWORD count = candList->dwCount;
                    state.selectedIndex = candList->dwSelection;
                    if (count > 10) count = 10;
                    for (DWORD j = 0; j < count; j++) {
                        wchar_t* pStr = (wchar_t*)((char*)candList + candList->dwOffset[j]);
                        state.candidates.push_back(pStr);
                    }
                    delete[] (char*)candList;
                }
                ImmReleaseContext(fgWnd, himc);
            }
        }
    }

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        bool isChineseIME = (langId == 0x0804 || langId == 0x0404 ||
                            langId == 0x0C04 || langId == 0x1404);
        state.hkl = static_cast<long>(reinterpret_cast<DWORD_PTR>(hkl));
        state.imeOpen = isChineseIME;

        DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
        WORD imeId = HIWORD(hklValue);

        // GUID detection takes priority
        if (currentInputMethod_ != InputMethodType::UNKNOWN &&
            currentInputMethod_ != InputMethodType::ENGLISH) {
            state.inputMethodType = currentInputMethod_;
        } else {
            state.inputMethodType = detectInputMethodTypeFromImeId(imeId, langId);
        }

        if (isChineseIME) {
            bool tsfMode = detectChineseMode();
            chineseMode_ = tsfMode;
            state.chineseMode = tsfMode;
        } else {
            chineseMode_ = false;
            state.chineseMode = false;
            state.imeOpen = false;
        }
    }

    g_cache.update(state);
}

bool TsfSink::registerSinks(ITfContext* pic) {
    if (!pic || context_ == pic) return false;
    if (context_) {
        unregisterSinks();
    }
    context_ = pic;
    context_->AddRef();

    HRESULT hr = pic->QueryInterface(IID_ITfSource, (void**)&contextSource_);
    if (FAILED(hr) || !contextSource_) {
        return false;
    }

    hr = contextSource_->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &editSinkCookie_);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[ChineseIME] Failed to register edit sink\n");
    } else {
        OutputDebugStringW(L"[ChineseIME] Edit sink registered\n");
    }

    return true;
}

void TsfSink::unregisterSinks() {
    if (contextSource_) {
        if (editSinkCookie_ != TF_INVALID_COOKIE) {
            contextSource_->UnadviseSink(editSinkCookie_);
            editSinkCookie_ = TF_INVALID_COOKIE;
        }
        contextSource_->Release();
        contextSource_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
}

ITfThreadMgr* TsfSink::getThreadMgr() {
    return threadMgr_;
}

ITfContext* TsfSink::getCurrentContext() {
    if (!threadMgr_) return nullptr;
    ITfDocumentMgr* docMgr = nullptr;
    HRESULT hr = threadMgr_->GetFocus(&docMgr);
    if (FAILED(hr) || !docMgr) {
        return nullptr;
    }
    ITfContext* ctx = nullptr;
    hr = docMgr->GetTop(&ctx);
    docMgr->Release();
    return ctx;
}

void TsfSink::notifyStateChanges(const IMEState& oldState, const IMEState& newState) {
    if (oldState.composition != newState.composition || oldState.candidates != newState.candidates) {
        std::vector<const wchar_t*> candidatePtrs;
        for (const auto& cand : newState.candidates) {
            candidatePtrs.push_back(cand.c_str());
        }
        if (g_onCandidateUpdate) {
            g_onCandidateUpdate(
                newState.composition.c_str(),
                candidatePtrs.empty() ? nullptr : candidatePtrs.data(),
                static_cast<int>(newState.candidates.size()),
                newState.selectedIndex
            );
        }
    }

    if (oldState.inputMethodType != newState.inputMethodType) {
        if (g_onLayoutChange) {
            g_onLayoutChange(static_cast<int>(newState.inputMethodType));
        }
    }

    if (oldState.chineseMode != newState.chineseMode) {
        if (g_onModeChange) {
            g_onModeChange(newState.chineseMode);
        }
    }
}

bool TsfSink::getCompositionString(ITfContext* pic, std::wstring& result) {
    result.clear();
    if (!pic) return false;

    ITfProperty* prop = nullptr;
    HRESULT hr = pic->GetProperty(GUID_PROP_COMPOSING, &prop);
    if (FAILED(hr) || !prop) {
        return false;
    }

    IEnumTfRanges* enumRanges = nullptr;
    hr = prop->EnumRanges(ecReadOnly_, &enumRanges, nullptr);
    prop->Release();
    if (FAILED(hr) || !enumRanges) {
        return false;
    }

    ITfRange* range = nullptr;
    std::wstring composition;

    while (enumRanges->Next(1, &range, nullptr) == S_OK) {
        if (range) {
            wchar_t buffer[64];
            ULONG fetched = 0;
            hr = range->GetText(ecReadOnly_, 0, buffer, 63, &fetched);
            if (SUCCEEDED(hr) && fetched > 0) {
                buffer[fetched] = 0;
                composition += buffer;
            }
            range->Release();
        }
    }
    enumRanges->Release();

    if (!composition.empty()) {
        result = composition;
        return true;
    }
    return false;
}

bool TsfSink::getCandidateList(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;
    if (getCandidateListFromProperty(pic, candidates, selectedIndex)) {
        return true;
    }
    return getCandidateListFromDisplayAttribute(pic, candidates, selectedIndex);
}

bool TsfSink::getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;

    ITfProperty* prop = nullptr;
    HRESULT hr = pic->GetProperty(GUID_PROP_CANDIDATE, &prop);
    if (FAILED(hr) || !prop) {
        return false;
    }

    IEnumTfRanges* enumRanges = nullptr;
    hr = prop->EnumRanges(ecReadOnly_, &enumRanges, nullptr);
    prop->Release();
    if (FAILED(hr) || !enumRanges) {
        return false;
    }

    ITfRange* range = nullptr;
    while (enumRanges->Next(1, &range, nullptr) == S_OK) {
        if (range) {
            wchar_t buffer[64];
            ULONG fetched = 0;
            hr = range->GetText(ecReadOnly_, 0, buffer, 63, &fetched);
            if (SUCCEEDED(hr) && fetched > 0) {
                buffer[fetched] = 0;
                candidates.push_back(buffer);
            }
            range->Release();
        }
    }
    enumRanges->Release();
    return !candidates.empty();
}

bool TsfSink::getCandidateListFromDisplayAttribute(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;

    ITfProperty* prop = nullptr;
    HRESULT hr = pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop);
    if (FAILED(hr) || !prop) {
        return false;
    }

    IEnumTfRanges* enumRanges = nullptr;
    hr = prop->EnumRanges(ecReadOnly_, &enumRanges, nullptr);
    prop->Release();
    if (FAILED(hr) || !enumRanges) {
        return false;
    }

    ITfRange* range = nullptr;
    while (enumRanges->Next(1, &range, nullptr) == S_OK) {
        if (range) {
            VARIANT var;
            VariantInit(&var);
            hr = prop->GetValue(ecReadOnly_, range, &var);
            if (SUCCEEDED(hr) && var.vt == VT_I4) {
                wchar_t buffer[64];
                ULONG fetched = 0;
                hr = range->GetText(ecReadOnly_, 0, buffer, 63, &fetched);
                if (SUCCEEDED(hr) && fetched > 0) {
                    buffer[fetched] = 0;
                    candidates.push_back(buffer);
                }
            }
            VariantClear(&var);
            range->Release();
        }
    }
    enumRanges->Release();
    return !candidates.empty();
}

void TsfSink::updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile) {
    DEBUG_LOG(L"[ChineseIME] updateInputMethodType: langid=0x%04x\n", langid);

    if (langid == 0x0804 || langid == 0x0404 || langid == 0x0C04 || langid == 0x1404) {
        bool detected = false;

        // Try GUID matching first (most reliable)
        if (IsEqualGUID(guidProfile, GUID_MS_PINYIN) || IsEqualGUID(clsid, GUID_MS_PINYIN)) {
            currentInputMethod_ = InputMethodType::PINYIN;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> PINYIN (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_ZHUYIN) || IsEqualGUID(clsid, GUID_MS_ZHUYIN)) {
            currentInputMethod_ = InputMethodType::ZHUYIN;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> ZHUYIN (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_CANGJIE) || IsEqualGUID(clsid, GUID_MS_CANGJIE)) {
            currentInputMethod_ = InputMethodType::CANGJIE;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> CANGJIE (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_WUBI) || IsEqualGUID(clsid, GUID_MS_WUBI)) {
            currentInputMethod_ = InputMethodType::WUBI;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> WUBI (GUID match)\n");
        } else if (IsEqualGUID(guidProfile, GUID_MS_SUCHENG) || IsEqualGUID(clsid, GUID_MS_SUCHENG)) {
            currentInputMethod_ = InputMethodType::SUCHENG;
            detected = true;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> SUCHENG (GUID match)\n");
        }

        if (!detected) {
            currentInputMethod_ = InputMethodType::OTHER_CHINESE;
            DEBUG_LOG_SIMPLE(L"[ChineseIME] -> OTHER_CHINESE (no GUID match)\n");
        }
    } else {
        currentInputMethod_ = InputMethodType::ENGLISH;
        DEBUG_LOG_SIMPLE(L"[ChineseIME] -> ENGLISH\n");
    }
}

} // namespace chineseime