# ChineseIME Mod 重构进度

## 项目概述
- **目标**: Fabric 1.21.4 中文输入法显示模组，支持 Windows
- **C++ DLL**: 处理 IMM32 IME 状态检测 (v3.0.0 简化版)
- **Java Mod**: 接收 DLL 数据并显示 UI
- **架构**: WndProc Hook + 轮询 (CocoaInput 风格)

---

## 已完成 ✅

### 架构重构 (2026-05-16)
- [x] **v3.0.0 简化架构** - 基于 CocoaInput 风格
  - 移除复杂的 JNA Callback 机制
  - 使用函数指针回调，C++ 直接处理 IME 消息
  - Java 端轮询获取状态
- [x] **HWND 获取** - `EnumWindows` 遍历当前进程窗口
- [x] **WndProc Hook** - `SetWindowLongPtr(GWLP_WNDPROC)`
- [x] **DLL 导出函数简化**:
  - `HookWindowProc(void* hwnd)` / `UnhookWindowProc()`
  - `GetCompositionString()` / `GetCandidateCount()` / `GetCandidate()`
  - `GetInputMethodType()` / `GetChineseMode()` / `GetShiftMode()` / `GetCapsLockState()`

### UI 组件
- [x] **ImeStatusIndicator** - 极简风格，半透明背景
  - 聊天室显示输入法类型（拼/注/倉/五/速）
  - Caps Lock 时蓝色背景
  - Shift 中英文切换时黄色方块指示器
- [x] **CandidateHud** - 候选词界面
  - 半透明背景，高度 40px
  - 选中词语左侧 3px 蓝色条
  - 左右箭头 `<` `>` 翻页
  - 输入内容显示在左侧（罗马拼音/注音符号/仓颉等）

### 功能
- [x] DLL 内嵌到 JAR（`META-INF/natives/amd64/`）
- [x] 键盘布局检测（拼/注/倉/五/速）
- [x] Shift 中英文切换检测
- [x] Caps Lock 大写锁定检测
- [x] composition（输入内容）显示支持多语言文字

### 修复的问题
- [x] **JNA Callback 失败** - 改用函数指针
- [x] **HWND 获取失败** - 使用 `EnumWindows` 遍历
- [x] **IME 类型检测** - 简化为语言默认映射
- [x] **Caps Lock 检测** - 使用 `GetAsyncKeyState`
- [x] **Shift 模式检测** - `GetShiftMode()` 返回 `(!chineseMode && imeOpen)`

---

## 进行中 🔄

### Hook 成功验证
- **问题**: `Hooked window: false` - HWND 可能不正确
- **待验证**: 日志显示 `Using HWND from enum: xxx`
- **2026-05-16 状态**: EnumWindows 找到窗口，但 hook 仍失败

### 候选词显示验证
- **待验证**: hook 成功后候选词是否正常显示
- **2026-05-16 状态**: 因 hook 失败未能测试

---

## 待办 🔧

### 高优先级
- [ ] **修复 Hook 失败** - 验证 HWND 正确性
- [ ] **验证候选词显示** - hook 成功后完整测试

### 中优先级
- [ ] 简化 ConfigScreen - Windows 只显示 UI 缩放
- [ ] 验证 ImeStatusIndicator 在聊天室内正确显示
- [ ] 修复 UI 缩放功能（hudScale 实际应用到 HUD）

---

## 文件结构 (v3.0.0)

```
ChineseIME-Fabric-1.21.4/
├── native/
│   ├── CMakeLists.txt
│   └── src/
│       ├── ime_bridge.cpp       # 主入口、Hook、导出函数 (v3.0.0)
│       ├── ime_bridge.h         # 头文件
│       ├── ime_state_manager.cpp # 状态管理
│       ├── imm32_monitor.cpp    # IMM32 监控
│       ├── tsf_monitor.cpp     # TSF 监控（简化版）
│       ├── win_event_bridge.cpp  # 窗口事件 Bridge
│       └── sta_thread.cpp       # STA 线程管理
├── src/main/java/com/example/chineseime/
│   ├── ChineseIMEInitializer.java  # 主初始化器、HWND 查找
│   ├── config/
│   │   └── ModConfig.java        # 配置管理
│   ├── hud/
│   │   ├── CandidateHud.java     # 候选词 HUD
│   │   └── ImeStatusIndicator.java # 输入法状态指示器
│   ├── platform/
│   │   ├── PlatformIMEManager.java # 平台 IME 管理
│   │   └── win32/
│   │       ├── NativeImeBridge.java     # JNA 接口 (v3.0.0)
│   │       └── WindowsIMEBridgeNative.java # Windows IME Bridge
│   └── engine/
│       └── PinyinDictionary.java # 内置拼音引擎
└── build/libs/chineseime-1.0.0.jar
```

---

## 当前状态 (2026-05-16)

- **C++ DLL**: `natives/Release/chineseime_native.dll` (v3.0.0)
- **Java Mod**: `build/libs/chineseime-1.0.0.jar`
- **DLL 加载**: 正常工作 (DLL version: 3.0.0)
- **WndProc Hook**: 待验证（`Hooked window: false`）
- **IME 类型检测**: 正常工作（日志显示 `IME=拼`）
- **Caps Lock 检测**: 正常工作
- **Shift 检测**: 待验证

---

## 当前未解决问题 (2026-05-16)

### ⚠️ WndProc Hook 失败
- **现象**: `Hooked window: false`
- **可能原因**:
  1. HWND 正确但 hook 操作失败
  2. 窗口已被另一个 Hook 替换
  3. 权限问题
- **排查**: 日志显示 `Using HWND from enum: xxx (of N windows)` - HWND 有效

### ⚠️ 候选词和状态指示器不显示
- **原因**: 因 Hook 失败，IME 消息未被捕获
- **修复**: 先解决 Hook 问题

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+Shift+T | 显示测试候选词（开发用） |
| ↑/↓ 或 ←/→ | 选择候选词 |
| [ / ] | 上一页/下一页 |

---

## 技术笔记

### v3.0.0 架构 vs 旧架构

| 特性 | v3.0.0 (新) | 旧架构 |
|------|-------------|--------|
| 回调机制 | 函数指针 | JNA Callback |
| 消息处理 | C++ 直接处理 | Java 回调 |
| 状态读取 | Java 轮询 | 事件驱动 |
| TSF 支持 | 简化/移除 | 完整实现 |
| 代码复杂度 | 低 | 高 |

### IME 类型检测 (v3.0.0)
- zh-CN (0x0804) → PINYIN (type=2)
- zh-TW (0x0404/0x0C04/0x1404) → CANGJIE (type=4)
- TSF 输入法（`imeId == langId`）使用语言默认

### 调试方法
- Java 调试: 查看游戏日志中的 `[ChineseIME]` 前缀日志
- **关键日志**:
  - `[ChineseIME] Using HWND from enum: xxx` - HWND 获取成功
  - `[ChineseIME] Hooked window: true/false` - Hook 结果
  - `[ChineseIME] Poll: IME=X, CMode=Y, Caps=Z` - 轮询状态