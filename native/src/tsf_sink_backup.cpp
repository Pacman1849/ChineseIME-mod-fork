#include "tsf_sink.h"
#include "common.h"
#include "sta_thread.h"
#include <comdef.h>
#include <algorithm>
#include <msctf.h>
#include <windows.h>

// 调试宏，可以通过编译选项控制调试级别
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

// Define GUIDs that may not be available in all SDK versions
#ifndef GUID_COMPARTMENT_KEYBOARD_INPUTMODE
static const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE = 
    { 0x5147C989, 0x49A1, 0x46EC, { 0x8F, 0x15, 0x75, 0x9C, 0x45, 0x7D, 0x12, 0x4D } };
#endif

// Microsoft Input Method GUIDs - 使用正确的GUID
// 微软拼音: {81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}
static const GUID GUID_MS_PINYIN = 
    { 0x81d4e9c9, 0x1d3b, 0x41bc, { 0x9e, 0x6c, 0x4b, 0x40, 0xbf, 0x79, 0xe3, 0x5e } };

// 微软速成: {6024B45F-5C54-11D4-B921-0080C882687E}
static const GUID GUID_MS_SUCHENG = 
    { 0x6024b45f, 0x5c54, 0x11d4, { 0xb9, 0x21, 0x00, 0x80, 0xc8, 0x82, 0x68, 0x7e } };

// 微软注音: {B115690A-EA02-48D5-A231-E3578D2F0C52}
static const GUID GUID_MS_ZHUYIN = 
    { 0xb115690a, 0xea02, 0x48d5, { 0xa2, 0x31, 0xe3, 0x57, 0x8d, 0x2f, 0x0c, 0x52 } };

// 微软仓颉: {4BDF9F03-C7D3-11D4-B2AB-0080C882687E}
static const GUID GUID_MS_CANGJIE = 
    { 0x4bdf9f03, 0xc7d3, 0x11d4, { 0xb2, 0xab, 0x00, 0x80, 0xc8, 0x82, 0x68, 0x7e } };

// 微软五笔: {82590C13-F4DD-44f4-BA1D-8667246FDF8E}
static const GUID GUID_MS_WUBI = 
    { 0x82590c13, 0xf4dd, 0x44f4, { 0xba, 0x1d, 0x86, 0x67, 0x24, 0x6f, 0xdf, 0x8e } };

namespace chineseime {

// 注册表检测函数
GUID GetMicrosoftPinyinGUID() {
    // 微软拼音的GUID
    GUID guid = {0x81d4e9c9, 0x1d3b, 0x41bc, {0x9e, 0x6c, 0x4b, 0x40, 0xbf, 0x79, 0xe3, 0x5e}};
    return guid;
}

GUID GetMicrosoftZhuyinGUID() {
    // 微软注音的GUID
    GUID guid = {0x529a9e6b, 0x6587, 0x4f23, {0x9b, 0x71, 0x8c, 0x4f, 0x1f, 0x6a, 0x7c, 0x8c}};
    return guid;
}

GUID GetMicrosoftCangjieGUID() {
    // 微软仓颉的GUID
    GUID guid = {0x4bdf9f83, 0x7f63, 0x4c13, {0xad, 0xfe, 0x6a, 0x9c, 0x7f, 0x8e, 0x9d, 0x6a}};
    return guid;
}

GUID GetMicrosoftWubiGUID() {
    // 微软五笔的GUID
    GUID guid = {0x82590c13, 0xf4dd, 0x44f4, {0xba, 0x1d, 0x86, 0x7c, 0x7c, 0x8a, 0x9d, 0x6a}};
    return guid;
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
                               static_cast<ITfInputProcessorProfileActivationSink*>(this), 
                               &profileSinkCookie_);
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
    
    // 清理COM对象，按照依赖关系反向释放
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
    
    // 重置状态
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
    
    if (riid == IID_IUnknown ||
        riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    }
    else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    }
    else if (riid == IID_ITfInputProcessorProfileActivationSink) {
        *ppv = static_cast<ITfInputProcessorProfileActivationSink*>(this);
    }
    else {
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

STDMETHODIMP TsfSink::OnEndEdit(ITfContext* pic, 
                                 TfEditCookie ecReadOnly, 
                                 ITfEditRecord* pEditRecord) {
    if (!pic) return E_INVALIDARG;
    
    ecReadOnly_ = ecReadOnly;
    
    // 保存旧状态用于比较
    auto oldState = g_cache.snapshot();
    
    // 更新缓存
    updateCache();
    
    // 获取新状态
    auto newState = g_cache.snapshot();
    
    // 检测变化并通知Java
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
    wchar_t buf[256];
            DEBUG_LOG(L"[ChineseIME] OnActivated: langid=0x%04x, hkl=0x%p, flags=0x%x\n", 
                       langid, hkl, dwFlags);
    
    // Update input method type based on GUID
    updateInputMethodType(langid, clsid, guidProfile);
    
    // Update state when input method is activated/deactivated
    if (dwFlags & TF_IPSINK_FLAG_ACTIVE) {
        auto state = g_cache.snapshot();
        state.imeOpen = true;
        state.layoutChangeCount++;
        state.hkl = reinterpret_cast<long>(hkl);
        state.inputMethodType = currentInputMethod_;
        
        // Detect Chinese mode from language ID
        if (langid == 0x0804 || langid == 0x0404) {
            state.chineseMode = true;
        } else {
            state.chineseMode = false;
        }
        
    g_cache.update(state);
}

bool TsfSink::detectChineseMode() {
    if (!threadMgr_) return false;
    
    ITfDocumentMgr* docMgr = nullptr;
    HRESULT hr = threadMgr_->GetFocus(&docMgr);
    if (FAILED(hr) || !docMgr) {
        return false;
    }
    
    ITfContext* ctx = nullptr;
    hr = docMgr->GetTop(&ctx);
    docMgr->Release();
    
    if (FAILED(hr) || !ctx) {
        return false;
    }
    
    // Try to get input mode from compartment
    ITfCompartmentMgr* compMgr = nullptr;
    hr = ctx->QueryInterface(IID_ITfCompartmentMgr, (void**)&compMgr);
    ctx->Release();
    
    if (FAILED(hr) || !compMgr) {
        return false;
    }
    
    ITfCompartment* comp = nullptr;
    hr = compMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE, &comp);
    compMgr->Release();
    
    if (FAILED(hr) || !comp) {
        return false;
    }
    
    VARIANT var;
    VariantInit(&var);
    hr = comp->GetValue(&var);
    comp->Release();
    
    if (SUCCEEDED(hr) && var.vt == VT_I4) {
        // The low bit indicates native mode (Chinese)
        bool isChinese = (var.lVal & 0x0001) != 0;
        VariantClear(&var);
        return isChinese;
    }
    
    VariantClear(&var);
    return false;
}

void TsfSink::updateCache() {
    // Get existing state to preserve fields like inputMethodType
    auto state = g_cache.snapshot();
    state.isValid = true;
    
    // Clear composition-related fields (they will be re-populated)
    state.composition.clear();
    state.candidates.clear();
    state.selectedIndex = 0;
    
    // Try to get TSF context
    ITfContext* ctx = getCurrentContext();
    if (ctx) {
        getCompositionString(ctx, state.composition);
        getCandidateList(ctx, state.candidates, state.selectedIndex);
        ctx->Release();
    }
    
    // Always check keyboard layout via GetKeyboardLayout
    // This works even without TSF context
    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        bool isChineseIME = (langId == 0x0804 || langId == 0x0404);
        state.hkl = static_cast<long>(reinterpret_cast<DWORD_PTR>(hkl));
        
        // Detect IME type from HKL
        state.imeOpen = isChineseIME;
        
        // For Chinese IME, try to detect Chinese mode via TSF compartment
        if (isChineseIME) {
            bool tsfMode = detectChineseMode();
            if (tsfMode) {
                state.chineseMode = true;
            } else {
                // TSF might not have context, default to Chinese mode when IME is active
                // User can toggle with Shift, which changes the conversion status
                state.chineseMode = true;
            }
        } else {
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
    
    hr = contextSource_->AdviseSink(IID_ITfTextEditSink, 
                                    static_cast<ITfTextEditSink*>(this), 
                                    &editSinkCookie_);
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

void TsfSink::notifyStateChanges(const CandidateCache::State& oldState, const CandidateCache::State& newState) {
    // 检查组合字符串变化
    if (oldState.composition != newState.composition) {
        DEBUG_LOG_SIMPLE(L"[ChineseIME] Composition changed\n");
        // 通知Java组合字符串变化
        extern void NotifyCandidateUpdate(const wchar_t* composition, const wchar_t** candidates, int count, int selectedIndex);
        
        // 准备候选词数组
        std::vector<const wchar_t*> candidatePtrs;
        for (const auto& cand : newState.candidates) {
            candidatePtrs.push_back(cand.c_str());
        }
        
        NotifyCandidateUpdate(
            newState.composition.c_str(),
            candidatePtrs.empty() ? nullptr : candidatePtrs.data(),
            static_cast<int>(newState.candidates.size()),
            newState.selectedIndex
        );
    }
    
    // 检查输入法类型变化
    if (oldState.inputMethodType != newState.inputMethodType) {
        DEBUG_LOG(L"[ChineseIME] Input method type changed: %d -> %d\n", 
                  static_cast<int>(oldState.inputMethodType), 
                  static_cast<int>(newState.inputMethodType));
        
        extern void NotifyLayoutChange(int inputMethodType);
        NotifyLayoutChange(static_cast<int>(newState.inputMethodType));
    }
    
    // 检查中文模式变化
    if (oldState.chineseMode != newState.chineseMode) {
        DEBUG_LOG(L"[ChineseIME] Chinese mode changed: %d -> %d\n", 
                  oldState.chineseMode ? 1 : 0, newState.chineseMode ? 1 : 0);
        
        extern void NotifyModeChange(bool chineseMode);
        NotifyModeChange(newState.chineseMode);
    }
}

} // namespace chineseime