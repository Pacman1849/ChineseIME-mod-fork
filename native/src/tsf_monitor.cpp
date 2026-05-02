#include "tsf_monitor.h"
#include "ime_state_manager.h"
#include "sta_thread.h"
#include "jni_callback.h"
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

namespace chineseime {

TsfMonitor::TsfMonitor() {
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor created\n");
}

TsfMonitor::~TsfMonitor() {
    shutdown();
    DEBUG_LOG_SIMPLE(L"[ChineseIME] TsfMonitor destroyed\n");
}

bool TsfMonitor::initialize(IUnknown* pThreadMgr) {
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

void TsfMonitor::shutdown() {
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

    currentInputMethod_ = InputMethodType::UNKNOWN;
    chineseMode_ = false;
}

void TsfMonitor::refreshState() {
    updateCache();
}

STDMETHODIMP TsfMonitor::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfInputProcessorProfileActivationSink) {
        *ppv = static_cast<ITfInputProcessorProfileActivationSink*>(this);
    } else if (riid == IID_ITfCompartmentEventSink) {
        *ppv = static_cast<ITfCompartmentEventSink*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TsfMonitor::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) TsfMonitor::Release() {
    LONG count = InterlockedDecrement(&refCount_);
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP TsfMonitor::OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) {
    if (!pic) return E_INVALIDARG;
    ecReadOnly_ = ecReadOnly;

    auto oldState = ImeStateManager::get().getSnapshot();
    updateCache();
    auto newState = ImeStateManager::get().getSnapshot();
    notifyStateChanges(oldState, newState);
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        updateCache();
    }
    return S_OK;
}

STDMETHODIMP TsfMonitor::OnActivated(DWORD dwProfileType, LANGID langid, REFCLSID clsid,
                                     REFGUID guidProfile, REFGUID guidCat, HKL hkl, DWORD dwFlags) {
    updateInputMethodType(langid, clsid, guidProfile);

    DEBUG_LOG(L"[ChineseIME] OnActivated: type=%d, langid=0x%04x, hkl=0x%08X, flags=0x%x\n",
        (int)currentInputMethod_, langid, (DWORD)(DWORD_PTR)hkl, dwFlags);

    if (dwFlags & TF_IPSINK_FLAG_ACTIVE) {
        ImeStateManager::get().updateInputMethod(currentInputMethod_);

        bool isChineseLang = (langid == 0x0804 || langid == 0x0404 ||
                             langid == 0x0C04 || langid == 0x1404);
        chineseMode_ = isChineseLang;
        ImeStateManager::get().updateChineseMode(isChineseLang);
        ImeStateManager::get().updateImeOpen(true);

        onImeStateChanged(static_cast<int>(currentInputMethod_), isChineseLang);
    }

    return S_OK;
}

STDMETHODIMP TsfMonitor::OnChange(REFGUID rguid) {
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        bool newChineseMode = detectChineseMode();
        if (chineseMode_ != newChineseMode) {
            chineseMode_ = newChineseMode;
            ImeStateManager::get().updateChineseMode(newChineseMode);
            onImeStateChanged(static_cast<int>(currentInputMethod_), newChineseMode);
        }
    }
    return S_OK;
}

bool TsfMonitor::detectChineseMode() {
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
                    hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &comp);
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

void TsfMonitor::updateCache() {
    ImeStateManager& mgr = ImeStateManager::get();

    if (currentInputMethod_ != InputMethodType::UNKNOWN) {
        mgr.updateInputMethod(currentInputMethod_);
    }
    mgr.updateChineseMode(chineseMode_);

    ITfContext* ctx = getCurrentContext();
    bool gotCandidatesFromTsf = false;
    if (ctx) {
        std::wstring composition;
        getCompositionString(ctx, composition);
        std::vector<std::wstring> candidates;
        int selectedIndex = 0;
        if (getCandidateList(ctx, candidates, selectedIndex)) {
            mgr.updateCandidates(composition, candidates, selectedIndex);
            gotCandidatesFromTsf = true;
        } else {
            mgr.updateComposition(composition);
        }
        ctx->Release();
    }

    if (!gotCandidatesFromTsf) {
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) {
            HIMC himc = ImmGetContext(fgWnd);
            if (himc) {
                std::wstring composition;
                LONG compLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, nullptr, 0);
                if (compLen > 0) {
                    int wcharLen = compLen / sizeof(wchar_t);
                    wchar_t* compBuf = new wchar_t[wcharLen + 1];
                    ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf, compLen);
                    compBuf[wcharLen] = 0;
                    composition = compBuf;
                    delete[] compBuf;
                }

                std::vector<std::wstring> candidates;
                int selectedIndex = 0;
                size_t bufSize = ImmGetCandidateList(himc, 0, nullptr, 0);
                if (bufSize > 0) {
                    CANDIDATELIST* candList = (CANDIDATELIST*)new char[bufSize];
                    ImmGetCandidateList(himc, 0, candList, bufSize);
                    DWORD count = candList->dwCount;
                    selectedIndex = candList->dwSelection;
                    if (count > 10) count = 10;
                    for (DWORD j = 0; j < count; j++) {
                        wchar_t* pStr = (wchar_t*)((char*)candList + candList->dwOffset[j]);
                        candidates.push_back(pStr);
                    }
                    delete[] (char*)candList;
                }

                if (!candidates.empty()) {
                    mgr.updateCandidates(composition, candidates, selectedIndex);
                } else {
                    mgr.updateComposition(composition);
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
        mgr.updateImeOpen(isChineseIME);
    }
}

bool TsfMonitor::registerSinks(ITfContext* pic) {
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

void TsfMonitor::unregisterSinks() {
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

ITfContext* TsfMonitor::getCurrentContext() {
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

void TsfMonitor::notifyStateChanges(const IMEState& oldState, const IMEState& newState) {
    if (oldState.composition != newState.composition || oldState.candidates != newState.candidates) {
        std::vector<const wchar_t*> candidatePtrs;
        for (const auto& cand : newState.candidates) {
            candidatePtrs.push_back(cand.c_str());
        }
        onCandidateChanged(
            newState.composition.c_str(),
            candidatePtrs.empty() ? nullptr : candidatePtrs.data(),
            static_cast<int>(newState.candidates.size()),
            newState.selectedIndex
        );
    }

if (oldState.inputMethodType != newState.inputMethodType || oldState.chineseMode != newState.chineseMode) {
        onImeStateChanged(static_cast<int>(newState.inputMethodType), newState.chineseMode);
    }
}

bool TsfMonitor::getCompositionString(ITfContext* pic, std::wstring& result) {
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

bool TsfMonitor::getCandidateList(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
    candidates.clear();
    selectedIndex = 0;
    return getCandidateListFromProperty(pic, candidates, selectedIndex);
}

bool TsfMonitor::getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex) {
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

void TsfMonitor::updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile) {
    DEBUG_LOG(L"[ChineseIME] updateInputMethodType: langid=0x%04x\n", langid);

    if (langid == 0x0804 || langid == 0x0404 || langid == 0x0C04 || langid == 0x1404) {
        bool detected = false;

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