#include <windows.h>
#include <iostream>

int main() {
    HMODULE hModule = LoadLibraryA("C:\\Users\\user\\Documents\\ChineseIME-Fabric-1.21.4\\natives\\Release\\chineseime_native.dll");
    if (!hModule) {
        std::cout << "Failed to load DLL" << std::endl;
        return 1;
    }
    
    // 检查 SetCallbacksSimple 函数
    FARPROC func = GetProcAddress(hModule, "SetCallbacksSimple");
    if (func) {
        std::cout << "SetCallbacksSimple found at address: " << func << std::endl;
    } else {
        std::cout << "SetCallbacksSimple NOT found" << std::endl;
    }
    
    // 检查 SetCallbacks 函数
    func = GetProcAddress(hModule, "SetCallbacks");
    if (func) {
        std::cout << "SetCallbacks found at address: " << func << std::endl;
    } else {
        std::cout << "SetCallbacks NOT found" << std::endl;
    }
    
    FreeLibrary(hModule);
    return 0;
}