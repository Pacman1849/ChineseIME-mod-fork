# ChineseIME Mod - Agent Instructions

## 关键构建命令

### 构建 C++ DLL
```bash
cd native
cmake -B build
cmake --build build --config Release
```

### 构建 Java 模组
```bash
./gradlew.bat clean build
```

### 部署到 Minecraft 实例
```bash
copy build\libs\chineseime-1.0.0.jar "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods\"
copy natives\Release\chineseime_native.dll "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods\"
```

## 架构要点

### 核心架构：Java 信任 C++ DLL
**C++ DLL 统一检测所有 IME 状态，Java 只做监听和 UI 更新：**
- 输入法类型：通过 HKL 的 IME ID 检测（`detectInputMethodTypeFromImeId`）
- 中文模式：通过 IMM32 `ImmGetOpenStatus` 检测（IME 打开=中文模式，关闭=英文模式）
- Caps Lock：通过 `GetKeyboardState` 系统级检测（不是 `GetAsyncKeyState`）
- 候选词：通过 IMM32 获取

**重要：必须使用 `GetKeyboardState` 而非 `GetAsyncKeyState`**
- `GetAsyncKeyState` 是**线程相关**的，从轮询线程调用时返回错误状态
- `GetKeyboardState` 返回**系统级**键盘状态，对前台窗口有效

**C++ 只在状态变化时回调 Java：**
- 使用 `checkChanges()` 检测实际变化
- 候选词/组合字符串变化时才回调
- 减少不必要的回调

**Java `update()` 方法完全信任 C++ 缓存：**
- 直接读取 C++ 缓存中的状态
- 不做重复检测

### 轮询线程
- 约 60fps (每 16ms 轮询一次)
- 所有状态检测在 `PollIMEState()` 统一进行

### 指示器显示逻辑
- **Shift 指示器**（黄色方块）：中文输入法 && 英文模式时显示
- 英文模式 = `ImmGetOpenStatus` 返回 0
- Shift 切换中英文 ↔ IME 打开/关闭状态切换
- **Caps Lock 指示器**（蓝色背景）：使用 `GetKeyboardState(VK_CAPITAL) & 0x01` 检测
- **输入法类型**：无论中英文模式，始终显示输入法简称（拼/注/仓/速/五）

### IME 类型检测优先级
1. `OnActivated` 回调中直接使用 HKL 检测（最可靠）
2. `PollIMEState` 中使用 `detectInputMethodTypeFromImeId` 验证
3. TSF GUID 检测作为辅助参考（因为经常失败）

## JNA 关键注意事项

### 典型错误
`Error looking up function 'StartTsfListen': The specified procedure could not be found.`

**解决方案：**
- C++: `extern "C"` + `__declspec(dllexport)`
- Java: 接口继承 `StdCallLibrary`
- 回调接口继承 `StdCallLibrary.StdCallCallback`
- C++ `wchar_t*` → Java `WString`

### Caps Lock 检测要点
- **必须使用 `GetKeyboardState`** 而不是 `GetAsyncKeyState`
- `GetKeyboardState(keyboardState)` 返回后检查 `keyboardState[VK_CAPITAL] & 0x01`
- `GetAsyncKeyState(VK_CAPITAL)` 在轮询线程中返回线程相关状态，可能不准确
- Caps Lock 是 toggle 键，按下后保持开启直到再次按下

### 中文模式检测要点
- **必须使用 `ImmGetOpenStatus`** 检测 IME 打开/关闭状态
- `ImmGetOpenStatus(himc) != 0` → IME 打开 → 中文模式
- `ImmGetOpenStatus(himc) == 0` → IME 关闭 → 英文模式（Shift 切换后）
- 这与 Shift 切换中英文的机制完全对应

### TSF Compartment 检测的局限性
- `detectChineseMode()` 中的 TSF compartment 检测可能失败
- 添加了 IMM32 回退机制
- 所有检测都失败时返回当前状态避免误报

## 候选词显示关键修复

### 1. PollIMEState() 必须无条件获取候选词
**ime_bridge.cpp 中的错误逻辑：**
```cpp
// 错误：条件限制了非传统 IME 的候选词获取
if (isLegacy || newState.composition.empty()) {
    GetCandidatesFromIMM(himc, newState.candidates, newState.selectedIndex);
}
```

**正确逻辑：始终获取候选词：**
```cpp
// 正确：无条件获取所有 IME 的候选词
GetCompositionFromIMM(himc, newState.composition);
GetCandidatesFromIMM(himc, newState.candidates, newState.selectedIndex);
```

### 2. CandidateHud.visible 计算方式
**错误：`visible = !candidates.isEmpty() || !input.isEmpty()`**
- 当 candidates 为空但 input 有值时，visible=true 但不渲染任何内容

**正确：`visible = !candidates.isEmpty()`**
- 只在有候选词时才显示 HUD

### 3. GetCandidatesFromIMM 必须读取 selectedIndex
```cpp
// 错误：selectedIndex 总是 0
selectedIndex = 0;

// 正确：从 CANDIDATELIST 读取
selectedIndex = candList->dwSelection;
```

### 4. syncFromWindows 需要内置引擎回退
当 Windows IME 候选词为空但 composition 不为空时，需要调用内置引擎生成候选词：
```java
if (!winComposition.isEmpty()) {
    if (!winCandidates.isEmpty()) {
        hud.updateCandidates(winCandidates, winComposition);
    } else {
        // 使用内置引擎生成候选词回退
        List<String> cands = getSuggestions(winComposition);
        hud.updateCandidates(cands, winComposition);
    }
}
```

## IME ID 对照表 (HKL 高位字)

### C++ enum ↔ Java constant 对照表
| C++ InputMethodType | Java IME_TYPE | 显示 | IME ID |
|---------------------|---------------|------|--------|
| PINYIN=2 | IME_TYPE_PINYIN=2 | 拼 | 0x0001, 0x0010, 0xE010, 0xE020 |
| ZHUYIN=3 | IME_TYPE_ZHUYIN=3 | 注 | 0x0003, 0xE001 |
| CANGJIE=4 | IME_TYPE_CANGJIE=4 | 仓 | 0x0004, 0xE002 |
| WUBI=5 | IME_TYPE_WUBI=5 | 五 | 0x0002 |
| SUCHENG=6 | IME_TYPE_SUCHENG=6 | 速 | 0x0005, 0xE003 |
| ENGLISH=1 | IME_TYPE_ENGLISH=1 | En | - |

### IME ID 详细对照表
| IME | IME ID (HKL高位) |
|-----|------------------|
| 微软拼音 | 0x0001, 0x0010, 0xE010, 0xE020 |
| 微软五笔 | 0x0002 |
| 微软注音 | 0x0003, 0xE001 |
| 微软仓颉 | 0x0004, 0xE002 |
| 微软速成 | 0x0005, 0xE003 |

### IsLegacyIME 函数
只应返回传统 IME（仓颉 0x0004 和速成 0x0005）：
```cpp
bool IsLegacyIME(HKL hkl) {
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);
    return imeId == 0x0004 || imeId == 0x0005;
}
```

## 版本兼容性
- Minecraft: 1.21.4
- Fabric Loader: 0.16.9+
- Fabric API: 0.110.5+1.21.4
- Java: 21
- Windows: 10/11 (需要 TSF 支持)

## 目录结构
```
native/src/ # C++ DLL 源码
ime_bridge.cpp # 主入口、轮询线程、回调通知
tsf_sink.cpp # TSF 监听实现、中文模式检测
tsf_sink.h # TSF Sink 类定义
common.h # 数据结构、回调定义
candidate_cache.cpp # 状态缓存、变化检测
sta_thread.cpp # STA 线程管理

src/main/java/com/example/chineseime/
platform/win32/
NativeImeBridge.java # JNA 接口定义
WindowsIMEBridgeNative.java # IME 桥接
hud/
ImeStatusIndicator.java # 指示器和候选词 UI
engine/
InputMode.java # 输入模式枚举

build/libs/chineseime-1.0.0.jar # 输出的 mod JAR
natives/Release/chineseime_native.dll # 输出的 native DLL
```

## 核心文件说明

### ime_bridge.cpp - PollIMEState()
轮询线程的主要函数，检测顺序：
1. 获取 HKL，计算 IME 类型
2. 使用 `GetKeyboardState` 检测 Caps Lock 和 Shift 状态
3. 使用 `ImmGetOpenStatus` 检测 IME 打开/关闭状态（中文模式）
4. 计算 `inShiftMode = isChineseInputMethod && !chineseMode`
5. **无条件**调用 `GetCompositionFromIMM` 和 `GetCandidatesFromIMM`
6. 更新 `g_cache`

### tsf_sink.cpp - OnActivated()
输入法切换时的回调：
1. 直接使用 HKL 检测 IME 类型（不依赖 TSF GUID）
2. 根据语言 ID 设置 `chineseMode = true`（中文语言）
3. 更新 `g_cache`

### tsf_sink.cpp - detectChineseMode()
中文模式检测：
1. 尝试 TSF compartment 检测（可能失败）
2. 回退到 IMM32 `ImmGetConversionStatus`
3. 所有检测失败时返回当前状态

### tsf_sink.cpp - forceUpdateChineseMode()
强制更新中文模式：
1. 使用 IMM32 `ImmGetConversionStatus` 检测
2. IME_CMODE_NATIVE 标志表示中文模式
3. 失败时默认设置为 true（避免误报）

## 调试日志

### C++ 调试输出
`PollIMEState` 中的状态变化日志：
```
[ChineseIME] State: CapsLock=X, ShiftMode=X, ChineseMode=X, IMEType=X
```
- CapsLock: 1=大写锁定开启
- ShiftMode: 1=中文输入法+英文模式（显示黄色指示器）
- ChineseMode: 1=中文模式
- IMEType: 2=拼音, 3=注音, 4=仓颉, 5=五笔, 6=速成

### Java 调试
```java
System.setProperty("jna.debug_load", "true");
System.setProperty("jna.debug_load.jna", "true");
```

### 调试建议
1. 使用 Windows DebugView 工具查看 C++ 端的 OutputDebugStringW 输出
2. 检查日志中是否有 `[ChineseIME] Native DLL loaded successfully`
3. 在 `syncFromWindows()` 添加日志验证数据流

## 常见问题

### 候选词界面不显示
1. 检查 `PollIMEState()` 是否**无条件**获取候选词
2. 检查 `CandidateHud.updateCandidates()` 的 `visible` 计算是否正确
3. 检查 `GetCandidatesFromIMM()` 是否正确读取 `dwSelection`
4. 检查 `syncFromWindows()` 是否有内置引擎回退逻辑

### DLL 导出问题
1. 检查 CMakeLists.txt 包含 `ime_bridge.cpp`
2. 确保所有导出函数使用 `__declspec(dllexport)`
3. 验证 DLL 大小（正常约 52KB）

### TSF 初始化失败
- TSF 操作必须在 STA 线程中执行
- 使用 `StaThread` 类管理 STA 线程

### 输入法类型不显示（仍显示"拼"）
- 检查 `detectInputMethodTypeFromImeId` 是否处理所有 IME ID
- 检查 `OnActivated` 是否正确使用 HKL 检测
- 检查 TSF GUID 匹配是否失败导致覆盖

### Caps Lock 蓝色高光闪烁或消失
- **必须使用 `GetKeyboardState`** 而不是 `GetAsyncKeyState`
- `GetKeyboardState` 返回系统级状态
- `GetAsyncKeyState` 在轮询线程中不可靠

### 黄色方块一直显示不消失
- 检查 `ImmGetOpenStatus` 是否正确返回 IME 状态
- IME 关闭 = 英文模式 = 黄色方块显示
- IME 打开 = 中文模式 = 黄色方块消失

### 黄色方块按下 Shift 后不显示
- 检查 `ImmGetOpenStatus` 在 Shift 切换后是否正确返回 0
- Shift 切换会导致 IME 关闭（英文模式）