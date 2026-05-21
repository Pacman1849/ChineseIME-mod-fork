// Simplified IME Bridge - CocoaInput-style event-driven architecture
// Based on libwincocoainput by Korea-Minecraft-Forum/CocoaInput-lib

#include <windows.h>
#include <imm.h>
#include <string>
#include <vector>
#include <comdef.h>
#include <msctf.h>
#include "ime_state_manager.h"
#include "win_event_bridge.h"

#ifdef min
#undef min
#endif

const char* VERSION = "3.0.0";

static HWND g_hwnd = NULL;
static HIMC g_himc = NULL;
static WNDPROC g_originalWndProc = NULL;
static bool g_compositionLocationNotified = false;
static bool g_hookInstalled = false; // tracks if we successfully installed the hook
static HHOOK g_messageHook = NULL; // WH_GETMESSAGE hook as fallback
static HHOOK g_callWndProcHook = NULL; // WH_CALLWNDPROC hook for SendMessage

// Java callbacks
static void (*g_javaPreedit)(const wchar_t* text, int cursor, int selLen) = NULL;
static void (*g_javaCommit)(const wchar_t* text) = NULL;
static void (*g_javaCandidates)(const wchar_t** cands, int count, int selIdx) = NULL;

// Callbacks for IME state (type, chinese mode)
static void (*g_javaImeChange)(int imeType, int chineseMode) = NULL;

void setJavaCallbacks(
    void (*preedit)(const wchar_t*, int, int),
    void (*commit)(const wchar_t*),
    void (*candidates)(const wchar_t**, int, int),
    void (*imeChange)(int, int)) {
    g_javaPreedit = preedit;
    g_javaCommit = commit;
    g_javaCandidates = candidates;
    g_javaImeChange = imeChange;
}

bool containsChinese(const wchar_t* str) {
    if (!str) return false;
    while (*str) {
        wchar_t c = *str++;
        if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)) {
            return true;
        }
    }
    return false;
}

bool compositionLocationNotify(HWND hWnd) {
    if (!g_hwnd) return false;

    HIMC imc = ImmGetContext(g_hwnd);
    if (!imc) return false;

    POINT pt = {0, 0};

    CANDIDATEFORM candForm = {};
    candForm.dwIndex = 0;
    candForm.dwStyle = CFS_POINT;
    candForm.ptCurrentPos = pt;
    candForm.rcArea.left = 0;
    candForm.rcArea.top = 0;
    candForm.rcArea.right = 0;
    candForm.rcArea.bottom = 0;

    ImmSetCandidateWindow(imc, &candForm);
    ImmReleaseContext(g_hwnd, imc);
    return true;
}

void readCandidates(HIMC himc);
static std::vector<std::wstring> getCandidatesFromWindowEnumeration();

void readCandidates(HIMC himc) {
    if (!himc || !g_hwnd) return;

    DWORD candBufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (candBufSize > 0) {
        std::vector<char> candBuf(candBufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)candBuf.data();
        if (ImmGetCandidateListW(himc, 0, candList, candBufSize) > 0 && candList->dwCount > 0) {
            std::vector<std::wstring> cands;
            DWORD count = candList->dwCount > 9 ? 9 : candList->dwCount;
            for (DWORD i = 0; i < count; i++) {
                wchar_t* pStr = (wchar_t*)(candBuf.data() + candList->dwOffset[i]);
                cands.push_back(pStr);
            }
            std::vector<const wchar_t*> ptrs;
            for (auto& c : cands) ptrs.push_back(c.c_str());
            if (g_javaCandidates) {
                g_javaCandidates(ptrs.data(), (int)cands.size(), (int)candList->dwSelection);
            }
            return;
        }
    }

    // FALLBACK: IMM32 returned no candidates, try window enumeration
    // This helps third-party IMEs like Sogou, Tencent, Baidu that may not expose
    // candidates via ImmGetCandidateList
    std::vector<std::wstring> windowCandidates = getCandidatesFromWindowEnumeration();
    if (!windowCandidates.empty()) {
        std::vector<const wchar_t*> ptrs;
        for (auto& c : windowCandidates) ptrs.push_back(c.c_str());
        if (g_javaCandidates) {
            g_javaCandidates(ptrs.data(), (int)ptrs.size(), 0);
        }
    }
}

// Window enumeration callback for finding IME candidate windows
static BOOL CALLBACK EnumCandidateWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* results = reinterpret_cast<std::vector<std::wstring>*>(lParam);

    wchar_t className[64] = {0};
    GetClassNameW(hwnd, className, 63);

    // Check for common IME candidate window class names
    bool isCandidateWindow = false;
    const wchar_t* classStr = className;

    if (wcsstr(classStr, L"Cicero") != nullptr ||
        wcsstr(classStr, L"IME") != nullptr ||
        wcsstr(classStr, L"MSWinCls") != nullptr ||
        wcsstr(classStr, L"IMJPCnd") != nullptr ||
        wcsstr(classStr, L"CnCand") != nullptr ||
        wcsstr(classStr, L"SHGJE") != nullptr ||   // Sogou
        wcsstr(classStr, L"TTEdit") != nullptr ||  // Tencent
        wcsstr(classStr, L"TTF") != nullptr ||     // Tencent
        wcsstr(classStr, L"QQPY") != nullptr ||    // QQ Pinyin
        wcsstr(classStr, L"Ba IME") != nullptr ||  // Baidu
        wcsstr(classStr, L"mscand") != nullptr ||  // Microsoft
        wcsstr(classStr, L"CandList") != nullptr ||
        wcsstr(classStr, L"Conv") != nullptr) {
        isCandidateWindow = true;
    }

    if (!isCandidateWindow) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;

    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen <= 0) return TRUE;

    std::wstring windowText;
    windowText.resize(textLen + 1);
    int actualLen = GetWindowTextW(hwnd, &windowText[0], textLen + 1);
    if (actualLen <= 0) return TRUE;
    windowText.resize(actualLen);

    // Check if text contains likely candidate content
    bool hasCandidateContent = false;
    for (wchar_t c : windowText) {
        if (c >= 0x4E00 && c <= 0x9FFF) {
            hasCandidateContent = true;
            break;
        }
        if ((c >= L'0' && c <= L'9') && !windowText.empty()) {
            hasCandidateContent = true;
            break;
        }
    }

    if (!hasCandidateContent) return TRUE;

    // Parse candidates from the text
    std::wstring current;
    for (wchar_t c : windowText) {
        if (c == L'\n' || c == L'\r') {
            if (!current.empty()) {
                size_t dotPos = current.find(L'.');
                if (dotPos != std::wstring::npos && dotPos < 4) {
                    current = current.substr(dotPos + 1);
                }
                while (!current.empty() && (current[0] == L' ' || current[0] == L'\t')) {
                    current = current.substr(1);
                }
                if (!current.empty()) {
                    bool hasChinese = false;
                    for (wchar_t ch : current) {
                        if (ch >= 0x4E00 && ch <= 0x9FFF) {
                            hasChinese = true;
                            break;
                        }
                    }
                    if (hasChinese && current.size() <= 20) {
                        results->push_back(current);
                    }
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty() && results->empty()) {
        size_t dotPos = current.find(L'.');
        if (dotPos != std::wstring::npos && dotPos < 4) {
            current = current.substr(dotPos + 1);
        }
        while (!current.empty() && (current[0] == L' ' || current[0] == L'\t')) {
            current = current.substr(1);
        }
        if (!current.empty()) {
            bool hasChinese = false;
            for (wchar_t ch : current) {
                if (ch >= 0x4E00 && ch <= 0x9FFF) {
                    hasChinese = true;
                    break;
                }
            }
            if (hasChinese && current.size() <= 20) {
                results->push_back(current);
            }
        }
    }

    return TRUE;
}

static std::vector<std::wstring> getCandidatesFromWindowEnumeration() {
    std::vector<std::wstring> candidates;
    EnumWindows(EnumCandidateWindowsProc, reinterpret_cast<LPARAM>(&candidates));
    return candidates;
}

// WH_CALLWNDPROC hook procedure - captures messages sent via SendMessage
// This can catch WM_INPUTLANGCHANGE which WH_GETMESSAGE cannot
static LRESULT CALLBACK MessageCallWndProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        CWPSTRUCT* cwp = (CWPSTRUCT*)lParam;
        if (cwp && cwp->message == WM_INPUTLANGCHANGE) {
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] CallWndProc hook: WM_INPUTLANGCHANGE, hwnd=0x%IX, lParam=0x%IX\n",
                (UINT64)cwp->hwnd, (UINT64)cwp->lParam);
            OutputDebugStringA(dbg);

            // Process WM_INPUTLANGCHANGE - this message is sent via SendMessage
            // and tells us the keyboard layout has changed
            HKL hkl = (HKL)cwp->lParam;
            DWORD_PTR hklVal = (DWORD_PTR)hkl;
            WORD imeId = HIWORD(hklVal);
            LANGID langId = LOWORD(hklVal);

            int imeType = 1; // ENGLISH default
            bool isChinese = false;
            if (langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404) {
                isChinese = true;
                if (imeId != langId) {
                    switch (imeId) {
                        case 0x01: case 0x10: case 0xE010: case 0xE020: imeType = 2; break;
                        case 0x02: case 0xE011: imeType = 5; break;
                        case 0x03: case 0xE001: imeType = 3; break;
                        case 0x04: case 0xE002: imeType = 4; break;
                        case 0x05: case 0xE003: imeType = 6; break;
                        default: imeType = (langId == 0x0804) ? 2 : 4;
                    }
                } else {
                    imeType = (langId == 0x0804) ? 2 : 4;
                }
            }

            // Update ImeStateManager
            chineseime::ImeStateManager::get().updateInputMethod(static_cast<chineseime::InputMethodType>(imeType));
            chineseime::ImeStateManager::get().updateChineseMode(isChinese);
            chineseime::ImeStateManager::get().updateImeOpen(isChinese);

            sprintf_s(dbg, "[ChineseIME] CallWndProc: IME type change: langId=0x%04X, imeId=0x%04X, type=%d\n",
                langId, imeId, imeType);
            OutputDebugStringA(dbg);

            // Notify Java callback if registered
            if (g_javaImeChange) {
                g_javaImeChange(imeType, isChinese ? 1 : 0);
            }
        }
    }
    return CallNextHookEx(g_callWndProcHook, code, wParam, lParam);
}

// WH_GETMESSAGE hook procedure - captures posted IME messages
static LRESULT CALLBACK MessageGetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        MSG* msg = (MSG*)lParam;
        if (msg && msg->hwnd) {
            // Process WM_INPUTLANGCHANGE for any window
            if (msg->message == WM_INPUTLANGCHANGE) {
                HKL hkl = (HKL)msg->lParam;
                DWORD_PTR hklVal = (DWORD_PTR)hkl;
                WORD imeId = HIWORD(hklVal);
                LANGID langId = LOWORD(hklVal);
                int imeType = 1;
                bool isChinese = false;
                if (langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404) {
                    isChinese = true;
                    if (imeId != langId) {
                        switch (imeId) {
                            case 0x01: case 0x10: case 0xE010: case 0xE020: imeType = 2; break;
                            case 0x02: case 0xE011: imeType = 5; break;
                            case 0x03: case 0xE001: imeType = 3; break;
                            case 0x04: case 0xE002: imeType = 4; break;
                            case 0x05: case 0xE003: imeType = 6; break;
                            default: imeType = (langId == 0x0804) ? 2 : 4;
                        }
                    } else {
                        imeType = (langId == 0x0804) ? 2 : 4;
                    }
                }
                chineseime::ImeStateManager::get().updateInputMethod(static_cast<chineseime::InputMethodType>(imeType));
                chineseime::ImeStateManager::get().updateChineseMode(isChinese);
                chineseime::ImeStateManager::get().updateImeOpen(isChinese);
                if (g_javaImeChange) g_javaImeChange(imeType, isChinese ? 1 : 0);
            }
            // Process WM_IME_* messages for hooked window
            else if (msg->hwnd == g_hwnd) {
                if (msg->message == WM_IME_STARTCOMPOSITION) {
                    g_compositionLocationNotified = false;
                    compositionLocationNotify(msg->hwnd);
                    if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                }
                else if (msg->message == WM_IME_COMPOSITION) {
                    HIMC himc = ImmGetContext(msg->hwnd);
                    if (himc) {
                        if (msg->lParam & GCS_RESULTSTR) {
                            LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
                            if (len > 0) {
                                std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                                ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
                                buf[len / sizeof(wchar_t)] = 0;
                                if (g_javaCommit) g_javaCommit(buf.data());
                            }
                        }
                        if (msg->lParam & GCS_COMPSTR) {
                            LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
                            int cursor = 0;
                            if (msg->lParam & GCS_CURSORPOS) {
                                cursor = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
                            }
                            if (len > 0) {
                                std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                                ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
                                buf[len / sizeof(wchar_t)] = 0;
                                int selLen = 0;
                                LONG attrLen = ImmGetCompositionStringW(himc, GCS_COMPATTR, NULL, 0);
                                if (attrLen > 0) {
                                    std::vector<char> attrs(attrLen);
                                    ImmGetCompositionStringW(himc, GCS_COMPATTR, attrs.data(), attrLen);
                                    for (int i = 0; i < attrLen; i++) {
                                        if (attrs[i] & ATTR_TARGET_CONVERTED) selLen++;
                                    }
                                }
                                if (g_javaPreedit) g_javaPreedit(buf.data(), cursor, selLen);
                                readCandidates(himc);
                            } else {
                                if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                            }
                        }
                        ImmReleaseContext(msg->hwnd, himc);
                    }
                }
                else if (msg->message == WM_IME_ENDCOMPOSITION) {
                    g_compositionLocationNotified = false;
                    if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                }
                else if (msg->message == WM_IME_NOTIFY) {
                    if (msg->wParam == IMN_OPENCANDIDATE || msg->wParam == IMN_CHANGECANDIDATE) {
                        compositionLocationNotify(msg->hwnd);
                        g_compositionLocationNotified = true;
                        HIMC himcTmp = ImmGetContext(msg->hwnd);
                        if (himcTmp) {
                            readCandidates(himcTmp);
                            ImmReleaseContext(msg->hwnd, himcTmp);
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_messageHook, code, wParam, lParam);
}

LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Debug: log all IME-related messages to verify hook is working
    if (msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_KEYLAST) {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] ImeWndProc received msg=0x%X (WM_IME_*)\n", msg);
        OutputDebugStringA(dbg);
    }
    switch (msg) {
        case WM_IME_STARTCOMPOSITION: {
            g_compositionLocationNotified = false;
            compositionLocationNotify(hWnd);
            if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
            return TRUE;
        }

        case WM_IME_COMPOSITION: {
            HIMC himc = ImmGetContext(hWnd);
            if (!himc) break;

            // Handle committed text (finalized)
            if (lParam & GCS_RESULTSTR) {
                LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
                if (len > 0) {
                    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
                    buf[len / sizeof(wchar_t)] = 0;
                    if (g_javaCommit) {
                        g_javaCommit(buf.data());
                    }
                }
            }

            // Handle composing text (preedit) - CocoaInput风格
            if (lParam & GCS_COMPSTR) {
                LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
                int cursor = 0;
                if (lParam & GCS_CURSORPOS) {
                    cursor = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
                }

                if (len > 0) {
                    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
                    buf[len / sizeof(wchar_t)] = 0;

                    int selLen = 0;
                    LONG attrLen = ImmGetCompositionStringW(himc, GCS_COMPATTR, NULL, 0);
                    if (attrLen > 0) {
                        std::vector<char> attrs(attrLen);
                        ImmGetCompositionStringW(himc, GCS_COMPATTR, attrs.data(), attrLen);
                        for (int i = 0; i < attrLen; i++) {
                            if (attrs[i] & ATTR_TARGET_CONVERTED) selLen++;
                        }
                    }

                    if (g_javaPreedit) g_javaPreedit(buf.data(), cursor, selLen);

                    // Update candidate window position for native rendering
                    if (!g_compositionLocationNotified) {
                        compositionLocationNotify(hWnd);
                        g_compositionLocationNotified = true;
                    }

                    // Try to read candidates when composition exists
                    // Some IMEs may not send IMN_OPENCANDIDATE, so we read candidates here as backup
                    readCandidates(himc);
                } else {
                    if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
                }
            }

            ImmReleaseContext(hWnd, himc);
            return TRUE;
        }

        case WM_IME_ENDCOMPOSITION: {
            g_compositionLocationNotified = false;
            if (g_javaPreedit) g_javaPreedit(L"", 0, 0);
            return TRUE;
        }

        case WM_IME_NOTIFY: {
            if (wParam == IMN_OPENCANDIDATE) {
                compositionLocationNotify(hWnd);
                g_compositionLocationNotified = true;
                HIMC himcTmp = ImmGetContext(hWnd);
                if (himcTmp) {
                    readCandidates(himcTmp);
                    ImmReleaseContext(hWnd, himcTmp);
                }
                return TRUE;
            } else if (wParam == IMN_CHANGECANDIDATE) {
                compositionLocationNotify(hWnd);
                g_compositionLocationNotified = true;
                HIMC himcTmp = ImmGetContext(hWnd);
                if (himcTmp) {
                    readCandidates(himcTmp);
                    ImmReleaseContext(hWnd, himcTmp);
                }
                return TRUE;
            } else if (wParam == IMN_CLOSECANDIDATE) {
                return TRUE;
            }
            break;
        }

        case WM_INPUTLANGCHANGE: {
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] ImeWndProc WM_INPUTLANGCHANGE received: lParam=%I64u\n", (UINT64)lParam);
            OutputDebugStringA(dbg);

            HKL hkl = (HKL)lParam;
            DWORD_PTR hklVal = (DWORD_PTR)hkl;
            WORD imeId = HIWORD(hklVal);
            LANGID langId = LOWORD(hklVal);

            int imeType = 1; // ENGLISH default
            bool isChinese = false;
            if (langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404) {
                isChinese = true;
                if (imeId != langId) {
                    switch (imeId) {
                        case 0x01: case 0x10: case 0xE010: case 0xE020: imeType = 2; break;
                        case 0x02: case 0xE011: imeType = 5; break;
                        case 0x03: case 0xE001: imeType = 3; break;
                        case 0x04: case 0xE002: imeType = 4; break;
                        case 0x05: case 0xE003: imeType = 6; break;
                        default: imeType = (langId == 0x0804) ? 2 : 4;
                    }
                } else {
                    imeType = (langId == 0x0804) ? 2 : 4;
                }
            }

            // Update ImeStateManager so polling works even if callback fails
            chineseime::ImeStateManager::get().updateInputMethod(static_cast<chineseime::InputMethodType>(imeType));
            chineseime::ImeStateManager::get().updateChineseMode(isChinese);
            chineseime::ImeStateManager::get().updateImeOpen(isChinese);

            sprintf_s(dbg, "[ChineseIME] IME type change: langId=0x%04X, imeId=0x%04X, type=%d, chinese=%d\n",
                langId, imeId, imeType, isChinese ? 1 : 0);
            OutputDebugStringA(dbg);

            if (g_javaImeChange) {
                g_javaImeChange(imeType, isChinese ? 1 : 0);
            } else {
                OutputDebugStringA("[ChineseIME] WM_INPUTLANGCHANGE: g_javaImeChange is NULL, callback not set yet\n");
            }
            break;
        }
    }

    // Forward to original WndProc
    if (g_originalWndProc) {
        return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

extern "C" {

__declspec(dllexport) const wchar_t* GetDllVersion() {
    return L"3.0.0";
}

__declspec(dllexport) int HookWindowProc(void* hwnd) {
    if (!hwnd) return 0;
    HWND h = (HWND)hwnd;
    if (g_hwnd == h) return 1; // Already hooked

    // First get the current WndProc
    g_originalWndProc = (WNDPROC)GetWindowLongPtr(h, GWLP_WNDPROC);
    if (!g_originalWndProc) {
        return 0; // Failed to get original WndProc
    }

    // Try to set our hook
    LONG_PTR result = SetWindowLongPtr(h, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
    if (result == 0) {
        g_originalWndProc = NULL;
        return 0; // Hook failed
    }

    g_hwnd = h;

    // Get initial IME context
    g_himc = ImmGetContext(h);
    if (g_himc) {
        ImmReleaseContext(h, g_himc);
    } else {
        g_himc = ImmCreateContext();
    }

    return 1; // Success
}

__declspec(dllexport) int HookWindowProcRaw(ULONG_PTR hwnd) {
    if (!hwnd) return 0;
    HWND h = (HWND)hwnd;
    if (g_hwnd == h) return 1;

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] Hook attempt, hwnd=%I64u\n", (UINT64)hwnd);
    OutputDebugStringA(dbg);

    if (!IsWindow(h)) {
        OutputDebugStringA("[ChineseIME] Not a valid window\n");
        return 0;
    }

    DWORD errorBefore = GetLastError();
    sprintf_s(dbg, "[ChineseIME] IsWindow=true, LastError before=%d\n", errorBefore);
    OutputDebugStringA(dbg);

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    sprintf_s(dbg, "[ChineseIME] Window PID: %u\n", pid);

    DWORD currentPid = GetCurrentProcessId();
    sprintf_s(dbg, "[ChineseIME] Current PID: %u, Window PID: %u\n", currentPid, pid);
    OutputDebugStringA(dbg);

    LONG_PTR oldProc = GetWindowLongPtr(h, GWLP_WNDPROC);
    DWORD errorAfterGet = GetLastError();
    sprintf_s(dbg, "[ChineseIME] Original WndProc: %I64u, GetLastError: %d\n", (UINT64)oldProc, errorAfterGet);
    OutputDebugStringA(dbg);

    if (oldProc) {
        g_originalWndProc = (WNDPROC)oldProc;
    }

    // Get window styles for diagnostics
    LONG_PTR style = GetWindowLongPtr(h, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtr(h, GWL_EXSTYLE);
    sprintf_s(dbg, "[ChineseIME] Window styles: style=0x%IX, exStyle=0x%IX\n", style, exStyle);
    OutputDebugStringA(dbg);

    // Check if window is layered (WS_EX_LAYERED) - common with OpenGL/硬件加速
    // Layered windows may not support WndProc subclassing
    if (exStyle & WS_EX_LAYERED) {
        OutputDebugStringA("[ChineseIME] WARNING: Window has WS_EX_LAYERED - may cause SetWindowLongPtr failure\n");
    }

    // Check if window has WS_EX_TOOLWINDOW style
    if (exStyle & WS_EX_TOOLWINDOW) {
        OutputDebugStringA("[ChineseIME] WARNING: Window has WS_EX_TOOLWINDOW style\n");
    }

    // Check if window is visible
    if (!(style & WS_VISIBLE)) {
        OutputDebugStringA("[ChineseIME] WARNING: Window is not visible\n");
    }

    // Get window class name
    char className[256];
    int classLen = GetClassNameA(h, className, sizeof(className) - 1);
    className[classLen] = 0;
    sprintf_s(dbg, "[ChineseIME] Window class: '%s'\n", className);
    OutputDebugStringA(dbg);

    // Check if it's a GLFW window (usually has " GLFW" or similar in class name)
    if (strstr(className, "GLFW") != nullptr) {
        OutputDebugStringA("[ChineseIME] GLFW window detected\n");
    }

SetLastError(0);
    LONG_PTR result = SetWindowLongPtr(h, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
    DWORD errorAfterSet = GetLastError();
    sprintf_s(dbg, "[ChineseIME] SetWindowLongPtr result: %I64u, oldProc: %I64u, error: %d\n", (UINT64)result, (UINT64)oldProc, errorAfterSet);
    OutputDebugStringA(dbg);

    if (errorAfterSet == 5) {
        OutputDebugStringA("[ChineseIME] ERROR_ACCESS_DENIED (5) - window locked or not subclassable\n");
    } else if (errorAfterSet != 0) {
        sprintf_s(dbg, "[ChineseIME] SetWindowLongPtr error: %d\n", errorAfterSet);
        OutputDebugStringA(dbg);
    }

    if (errorAfterSet != 0) {
        g_originalWndProc = NULL;
        g_hwnd = NULL; // Don't store invalid handle
        return 0;
    }

    // Success
    g_hwnd = h;
    g_hookInstalled = true;
    g_himc = ImmGetContext(h);
    if (g_himc) {
        ImmReleaseContext(h, g_himc);
    } else {
        g_himc = ImmCreateContext();
    }
    sprintf_s(dbg, "[ChineseIME] Hook success, hwnd=%I64u, oldProc=%I64u\n", (UINT64)h, (UINT64)oldProc);
    OutputDebugStringA(dbg);
    return 1;
}

__declspec(dllexport) void UnhookWindowProc() {
    if (g_hookInstalled && g_hwnd) {
        // Restore original WndProc if we have it
        if (g_originalWndProc) {
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        }
        g_originalWndProc = NULL;
        g_hwnd = NULL;
        g_hookInstalled = false;
    }
    if (g_messageHook) {
        UnhookWindowsHookEx(g_messageHook);
        g_messageHook = NULL;
    }
    if (g_callWndProcHook) {
        UnhookWindowsHookEx(g_callWndProcHook);
        g_callWndProcHook = NULL;
    }
}

// Install WH_GETMESSAGE and WH_CALLWNDPROC hooks as fallback when WndProc subclassing fails
// WH_GETMESSAGE catches posted messages (GetMessage/PeekMessage)
// WH_CALLWNDPROC catches sent messages (SendMessage) - can catch WM_INPUTLANGCHANGE
__declspec(dllexport) int InstallMessageHook(ULONG_PTR hwnd) {
    if (!hwnd) return 0;
    HWND h = (HWND)hwnd;
    if (!IsWindow(h)) return 0;

    g_hwnd = h;

    // Clean up any existing hooks
    if (g_messageHook) {
        UnhookWindowsHookEx(g_messageHook);
        g_messageHook = NULL;
    }
    if (g_callWndProcHook) {
        UnhookWindowsHookEx(g_callWndProcHook);
        g_callWndProcHook = NULL;
    }

    // WH_CALLWNDPROC hook - intercepts messages sent via SendMessage
    // This is needed to catch WM_INPUTLANGCHANGE which is sent, not posted
    g_callWndProcHook = SetWindowsHookEx(WH_CALLWNDPROC, MessageCallWndProc, NULL, GetCurrentThreadId());
    if (g_callWndProcHook) {
        OutputDebugStringA("[ChineseIME] WH_CALLWNDPROC hook installed successfully\n");
    } else {
        DWORD err = GetLastError();
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WH_CALLWNDPROC hook failed, error=%d\n", err);
        OutputDebugStringA(dbg);
    }

    // WH_GETMESSAGE hook - intercepts messages retrieved via GetMessage/PeekMessage
    g_messageHook = SetWindowsHookEx(WH_GETMESSAGE, MessageGetMsgProc, NULL, GetCurrentThreadId());
    if (g_messageHook) {
        g_hookInstalled = true;
        OutputDebugStringA("[ChineseIME] WH_GETMESSAGE hook installed successfully\n");
        return 1;
    } else {
        DWORD err = GetLastError();
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WH_GETMESSAGE hook failed, error=%d\n", err);
        OutputDebugStringA(dbg);
        return 0;
    }
}

__declspec(dllexport) void SetEventCallbacks(
    void* preedit,
    void* commit,
    void* candidates,
    void* imeChange,
    void* keyboard) {
    setJavaCallbacks(
        (void(*)(const wchar_t*, int, int))preedit,
        (void(*)(const wchar_t*))commit,
        (void(*)(const wchar_t**, int, int))candidates,
        (void(*)(int, int))imeChange
    );
}

__declspec(dllexport) int GetCompositionString(wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;

    // First check ImeStateManager (where TSF/WinEventBridge stores composition)
    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (!state.composition.empty()) {
            int len = (int)state.composition.size();
            if (len >= bufferSize) len = bufferSize - 1;
            wcsncpy_s(buffer, bufferSize, state.composition.c_str(), len);
            buffer[len] = 0;
            return len;
        }
    }

    // Fall back to IMM32 - try g_hwnd first, then foreground window
    HWND hwndToTry = g_hwnd;
    if (!hwndToTry || !IsWindow(hwndToTry)) {
        hwndToTry = GetForegroundWindow();
    }

    if (!hwndToTry) return 0;

    HIMC himc = ImmGetContext(hwndToTry);
    if (!himc) {
        // Try foreground window if g_hwnd failed
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd && fgWnd != hwndToTry) {
            himc = ImmGetContext(fgWnd);
            hwndToTry = fgWnd;
        }
        if (!himc) return 0;
    }

    LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
    if (len > 0 && len <= bufferSize * sizeof(wchar_t)) {
        ImmGetCompositionStringW(himc, GCS_COMPSTR, buffer, len);
        ImmReleaseContext(hwndToTry, himc);
        return len / sizeof(wchar_t);
    }
    ImmReleaseContext(hwndToTry, himc);
    return 0;
}

__declspec(dllexport) int GetCandidateCount() {
    // First check ImeStateManager (where TSF monitor stores candidates from window enumeration)
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (!state.candidates.empty()) {
        return (int)state.candidates.size();
    }

    // Fall back to IMM32
    if (!g_hwnd) return 0;
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc) return 0;
    DWORD count = 0;
    DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
        if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0) {
            count = candList->dwCount;
        }
    }
    ImmReleaseContext(g_hwnd, himc);
    return count;
}

__declspec(dllexport) int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;

    // First check ImeStateManager (where TSF monitor stores candidates)
    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (index >= 0 && index < (int)state.candidates.size()) {
            const std::wstring& cand = state.candidates[index];
            int len = (int)cand.size();
            if (len >= bufferSize) len = bufferSize - 1;
            wcsncpy_s(buffer, bufferSize, cand.c_str(), len);
            buffer[len] = 0;
            return len;
        }
    }

    // Fall back to IMM32
    if (!g_hwnd) return 0;
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc) return 0;

    DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
        if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0) {
            if (index >= 0 && index < (int)candList->dwCount && bufferSize > 0) {
                wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[index]);
                int len = wcslen(pStr);
                if (len >= bufferSize) len = bufferSize - 1;
                wcsncpy_s(buffer, bufferSize, pStr, len);
                buffer[len] = 0;
                ImmReleaseContext(g_hwnd, himc);
                return len;
            }
        }
    }
    ImmReleaseContext(g_hwnd, himc);
    return 0;
}

__declspec(dllexport) int GetSelectedCandidateIndex() {
    // First check ImeStateManager
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (!state.candidates.empty()) {
        return state.selectedIndex;
    }

    // Fall back to IMM32
    if (!g_hwnd) return 0;
    HIMC himc = ImmGetContext(g_hwnd);
    if (!himc) return 0;
    int selIdx = 0;
    DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
        if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0) {
            selIdx = candList->dwSelection;
        }
    }
    ImmReleaseContext(g_hwnd, himc);
    return selIdx;
}

__declspec(dllexport) int GetImeOpenStatus() {
    if (!g_hwnd) return 0;
    // Use the window's thread for ImmGetContext
    DWORD threadId = GetWindowThreadProcessId(g_hwnd, NULL);
    DWORD currentThreadId = GetCurrentThreadId();
    BOOL attached = FALSE;
    if (threadId != currentThreadId) {
        attached = AttachThreadInput(currentThreadId, threadId, TRUE);
    }
    HIMC himc = ImmGetContext(g_hwnd);
    if (himc) {
        int open = ImmGetOpenStatus(himc);
        ImmReleaseContext(g_hwnd, himc);
        if (attached) {
            AttachThreadInput(currentThreadId, threadId, FALSE);
        }
        return open;
    }
    if (attached) {
        AttachThreadInput(currentThreadId, threadId, FALSE);
    }
    return 0;
}

__declspec(dllexport) int GetChineseMode() {
    if (!g_hwnd) return 0;
    DWORD threadId = GetWindowThreadProcessId(g_hwnd, NULL);
    DWORD currentThreadId = GetCurrentThreadId();
    BOOL attached = FALSE;
    if (threadId != currentThreadId) {
        attached = AttachThreadInput(currentThreadId, threadId, TRUE);
    }
    HIMC himc = ImmGetContext(g_hwnd);
    if (himc) {
        DWORD conversion = 0, sentence = 0;
        ImmGetConversionStatus(himc, &conversion, &sentence);
        ImmReleaseContext(g_hwnd, himc);
        if (attached) {
            AttachThreadInput(currentThreadId, threadId, FALSE);
        }
        return (conversion & IME_CMODE_NATIVE) ? 1 : 0;
    }
    if (attached) {
        AttachThreadInput(currentThreadId, threadId, FALSE);
    }
    return 0;
}

__declspec(dllexport) int GetShiftMode() {
    int open = GetImeOpenStatus();
    int chinese = GetChineseMode();
    return (open && !chinese) ? 1 : 0;
}

__declspec(dllexport) int GetCapsLockState() {
    return (GetKeyState(VK_CAPITAL) & 0x01) ? 1 : 0;
}

__declspec(dllexport) int GetInputMethodType() {
    // First check ImeStateManager (where TSF/WinEventBridge stores the type)
    {
        auto state = chineseime::ImeStateManager::get().getSnapshot();
        if (state.inputMethodType != chineseime::InputMethodType::UNKNOWN) {
            return static_cast<int>(state.inputMethodType);
        }
    }

    // Try foreground window - this catches layout changes made when game is in background
    HWND fgWnd = GetForegroundWindow();
    if (fgWnd) {
        DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, NULL);
        HKL hkl = GetKeyboardLayout(fgThreadId);
        if (hkl) {
            DWORD_PTR hklVal = (DWORD_PTR)hkl;
            WORD imeId = HIWORD(hklVal);
            LANGID langId = LOWORD(hklVal);

            if (langId == 0x0804) {
                if (imeId != langId) {
                    switch (imeId) {
                        case 0x01: case 0x10: case 0xE010: case 0xE020: return 2; // PINYIN
                        case 0x02: case 0xE011: return 5; // WUBI
                        default: return 2;
                    }
                }
                return 2;
            }
            if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404) {
                if (imeId != langId) {
                    switch (imeId) {
                        case 0x03: case 0xE001: return 3; // ZHUYIN
                        case 0x04: case 0xE002: return 4; // CANGJIE
                        case 0x05: case 0xE003: return 6; // SUCHENG
                        default: return 4;
                    }
                }
                return 4;
            }
        }
    }

    // Fall back to g_hwnd (the hooked window)
    if (!g_hwnd) return 1;

    DWORD threadId = GetWindowThreadProcessId(g_hwnd, NULL);
    HKL hkl = GetKeyboardLayout(threadId);
    if (!hkl) {
        hkl = GetKeyboardLayout(0);
    }
    if (!hkl) return 1;

    DWORD_PTR hklVal = (DWORD_PTR)hkl;
    WORD imeId = HIWORD(hklVal);
    LANGID langId = LOWORD(hklVal);

    if (langId == 0x0804) {
        if (imeId != langId) {
            switch (imeId) {
                case 0x01: case 0x10: case 0xE010: case 0xE020: return 2; // PINYIN
                case 0x02: case 0xE011: return 5; // WUBI
                default: return 2;
            }
        }
        return 2;
    }
    if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404) {
        if (imeId != langId) {
            switch (imeId) {
                case 0x03: case 0xE001: return 3; // ZHUYIN
                case 0x04: case 0xE002: return 4; // CANGJIE
                case 0x05: case 0xE003: return 6; // SUCHENG
                default: return 4;
            }
        }
        return 4;
    }
    return 1; // ENGLISH
}

__declspec(dllexport) int IsWindowHooked() {
    return g_hookInstalled ? 1 : 0;
}

__declspec(dllexport) void RefreshCandidates() {
    // Trigger re-read of candidates
    if (g_hwnd) {
        HIMC himc = ImmGetContext(g_hwnd);
        if (himc) {
            DWORD bufSize = ImmGetCandidateListW(himc, 0, NULL, 0);
            if (bufSize > 0) {
                std::vector<char> buf(bufSize);
                CANDIDATELIST* candList = (CANDIDATELIST*)buf.data();
                if (ImmGetCandidateListW(himc, 0, candList, bufSize) > 0 && candList->dwCount > 0) {
                    std::vector<std::wstring> cands;
                    DWORD count = candList->dwCount > 9 ? 9 : candList->dwCount;
                    for (DWORD i = 0; i < count; i++) {
                        wchar_t* pStr = (wchar_t*)(buf.data() + candList->dwOffset[i]);
                        cands.push_back(pStr);
                    }
                    std::vector<const wchar_t*> ptrs;
                    for (auto& c : cands) ptrs.push_back(c.c_str());
                    if (g_javaCandidates) {
                        g_javaCandidates(ptrs.data(), (int)ptrs.size(), (int)candList->dwSelection);
                    }
                }
            }
            ImmReleaseContext(g_hwnd, himc);
        }
    }
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        if (g_hwnd && g_originalWndProc) {
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        }
        if (g_messageHook) {
            UnhookWindowsHookEx(g_messageHook);
            g_messageHook = NULL;
        }
        if (g_callWndProcHook) {
            UnhookWindowsHookEx(g_callWndProcHook);
            g_callWndProcHook = NULL;
        }
    }
    return TRUE;
}