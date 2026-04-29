#pragma once

#include "common.h"
#include <msctf.h>

namespace chineseime {

class StaThread;

class TsfSink : public ITfTextEditSink,
                public ITfKeyEventSink,
                public ITfInputProcessorProfileActivationSink {
public:
    TsfSink();
    virtual ~TsfSink();

    bool initialize(IUnknown* pThreadMgr);
    void shutdown();
    void refreshState();
    bool isChineseMode() const { return chineseMode_; }
    void forceUpdateChineseMode();
    InputMethodType getInputMethodType() const { return currentInputMethod_; }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext* pic,
                           TfEditCookie ecReadOnly,
                           ITfEditRecord* pEditRecord) override;

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

private:
    bool getCompositionString(ITfContext* pic, std::wstring& result);
    bool getCandidateList(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);
    bool getCandidateListFromProperty(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);
    bool getCandidateListFromDisplayAttribute(ITfContext* pic, std::vector<std::wstring>& candidates, int& selectedIndex);
    bool detectChineseMode();
    void updateCache();
    void notifyStateChanges(const IMEState& oldState, const IMEState& newState);
    bool registerSinks(ITfContext* pic);
    void unregisterSinks();
    ITfThreadMgr* getThreadMgr();
    ITfContext* getCurrentContext();

    void updateInputMethodType(LANGID langid, REFCLSID clsid, REFGUID guidProfile);
    InputMethodType currentInputMethod_ = InputMethodType::UNKNOWN;

    LONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    ITfDocumentMgr* docMgr_ = nullptr;
    ITfContext* context_ = nullptr;
    ITfSource* contextSource_ = nullptr;
    ITfSource* threadMgrSource_ = nullptr;

    DWORD editSinkCookie_ = TF_INVALID_COOKIE;
    DWORD keyEventSinkCookie_ = TF_INVALID_COOKIE;
    DWORD profileSinkCookie_ = TF_INVALID_COOKIE;

    DWORD lastHkl_ = 0;
    bool lastChineseMode_ = false;
    bool chineseMode_ = false;
    TfEditCookie ecReadOnly_ = 0;
};

} // namespace chineseime