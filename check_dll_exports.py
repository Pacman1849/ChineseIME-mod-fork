import ctypes
import os

# DLL 路径
dll_path = r"C:\Users\user\Documents\ChineseIME-Fabric-1.21.4\natives\Release\chineseime_native.dll"

if os.path.exists(dll_path):
    print(f"DLL found: {dll_path}")
    
    # 使用 ctypes 加载 DLL
    try:
        dll = ctypes.WinDLL(dll_path)
        print("DLL loaded successfully")
        
        # 尝试获取所有导出函数
        # 使用更简单的方法：尝试调用不同的函数名
        function_names = [
            "SetCallbacks",
            "SetCallbacksSimple", 
            "StartTsfListen",
            "GetCompositionString",
            "GetCandidateCount"
        ]
        
        for func_name in function_names:
            try:
                func = getattr(dll, func_name)
                print(f"{func_name} function found")
                
                # 尝试获取函数地址
                address = ctypes.addressof(func)
                print(f"  Address: 0x{address:X}")
                
            except AttributeError:
                print(f"{func_name} function NOT found")
                
    except Exception as e:
        print(f"Error loading DLL: {e}")
else:
    print(f"DLL not found: {dll_path}")