#pragma once

#include "ime_state_manager.h"
#include "sta_thread.h"
#include <msctf.h>
#include <string>
#include <vector>

#ifndef GUID_COMPARTMENT_KEYBOARD_INPUTMODE
static const GUID GUID_COMPARTMENT_KEYBOARD_INPUTMODE =
{ 0x5147C989, 0x49A1, 0x46EC, { 0x8F, 0x15, 0x75, 0x9C, 0x45, 0x7D, 0x12, 0x4D } };
#endif

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

class TsfMonitor : public ITfTextEditSink,
                   public ITfKeyEventSink,
                   public ITfInputProcessorProfileActivationSink,
                   public ITfCompartmentEventSink {
public:
    TsfMonitor();
    virtual ~TsfMonitor();

    bool initialize(IUnknown* pThreadMgr);
    void shutdown();
    void refreshState();

    bool isChineseMode() const { return chineseMode_; }
    InputMethodType getInputMethodType() const { return currentInputMethod_; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) override;

    // ITfKeyEventSink
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;

    // ITfInputProcessorProfileActivationSink
    STDMETHODIMP OnActivated(DWORD dwProfileType, LANGID langid, REFCLSID clsid,
                             REFGUID guidProfile, REFGUID guidCat, HKL hkl, DWORD dwFlags) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;

private:
    void updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile);
    bool detectChineseMode();
    void updateCache();
    void notifyStateChanges(const IMEState& oldState, const IMEState& newState);
    bool registerSinks(ITfContext* pic);
    void unregisterSinks();
    ITfContext* getCurrentContext();
    bool getCompositionString(ITfContext* pic, std::wstring& result);
    bool getCandidateList(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);
    bool getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);

    InputMethodType currentInputMethod_ = InputMethodType::UNKNOWN;
    LONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    ITfDocumentMgr* docMgr_ = nullptr;
    ITfContext* context_ = nullptr;
    ITfSource* contextSource_ = nullptr;
    ITfSource* threadMgrSource_ = nullptr;
    ITfCompartment* openCloseCompartment_ = nullptr;

    DWORD editSinkCookie_ = TF_INVALID_COOKIE;
    DWORD keyEventSinkCookie_ = TF_INVALID_COOKIE;
    DWORD profileSinkCookie_ = TF_INVALID_COOKIE;
    DWORD compartmentSinkCookie_ = TF_INVALID_COOKIE;

    bool chineseMode_ = false;
    TfEditCookie ecReadOnly_ = 0;
};

} // namespace chineseime