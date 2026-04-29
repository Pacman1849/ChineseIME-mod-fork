import ctypes
import os

# DLL 路径
dll_path = r"C:\Users\user\Documents\ChineseIME-Fabric-1.21.4\natives\Release\chineseime_native.dll"

# 加载 DLL
if os.path.exists(dll_path):
    print(f"DLL found: {dll_path}")
    
    # 尝试加载 DLL
    try:
        dll = ctypes.WinDLL(dll_path)
        print("DLL loaded successfully")
        
        # 检查 SetCallbacks 函数
        try:
            func = getattr(dll, "SetCallbacks")
            print("SetCallbacks function found")
        except AttributeError:
            print("SetCallbacks function NOT found")
            
        # 检查 SetCallbacksSimple 函数
        try:
            func = getattr(dll, "SetCallbacksSimple")
            print("SetCallbacksSimple function found")
        except AttributeError:
            print("SetCallbacksSimple function NOT found")
            
        # 检查其他关键函数
        functions_to_check = [
            "StartTsfListen", "StopTsfListen", "IsTsfListening",
            "GetCompositionString", "GetCandidateCount", "GetCandidate",
            "StartListen", "StopListen", "IsListening"
        ]
        
        for func_name in functions_to_check:
            try:
                func = getattr(dll, func_name)
                print(f"{func_name} function found")
            except AttributeError:
                print(f"{func_name} function NOT found")
                
    except Exception as e:
        print(f"Error loading DLL: {e}")
else:
    print(f"DLL not found: {dll_path}")