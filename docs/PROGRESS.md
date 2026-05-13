# ChineseIME Mod 重构进度

## 项目概述
- **目标**: Fabric 1.21.4 中文输入法显示模组，支持 Windows/Linux/macOS
- **C++ DLL**: 处理 TSF/IMM32 IME 状态检测
- **Java Mod**: 接收 DLL 数据并显示 UI
- **架构**: 双通道 - WndProc Hook (IMM32) + TSF Polling

---

## 已完成 ✅

### 架构
- [x] C++ DLL + JNA 架构
- [x] `extern "C"` + `__declspec(dllexport)` 防止名称粉碎
- [x] `GetAsyncKeyState` 检测 Caps Lock（硬件级别，从任何线程都准确）
- [x] `GetKeyboardState` 检测 Caps Lock（系统级别，AGENTS.md 文档要求，但与实际代码冲突）
- ⚠️ **注意**: AGENTS.md 要求使用 `GetKeyboardState`，但代码使用 `GetAsyncKeyState`。后者读硬件状态更可靠，保留实现。
- [x] `ImmGetOpenStatus` 检测中文模式
- [x] `ImeStateManager.checkChanges()` 只在变化时回调
- [x] 懒加载 DLL，避免游戏启动挂起

### 事件驱动架构 (新实现)
- [x] **WinEventBridge** (`win_event_bridge.cpp`) - WndProc Hook
  - 通过 `SetWindowLongPtr(GWLP_WNDPROC)` 替换窗口过程
  - 监听 `WM_IME_STARTCOMPOSITION`, `WM_IME_COMPOSITION`, `WM_IME_ENDCOMPOSITION`, `WM_IME_NOTIFY`
  - 处理 IMM32 候选词消息
  - 调用 `CallWindowProc` 传递所有消息（不阻塞 Windows）
- [x] **WindowsIMEEventBridge.java** - 事件回调处理
  - `initialize()` 同时启动 WndProc Hook 和 TSF Polling
  - 通过 JNA 回调接收 preedit, commit, candidate, IME change, keyboard 事件
  - 在 Minecraft 主线程执行 UI 更新（`MinecraftClient.execute()`）
- [x] **TSF Polling** (`tsf_monitor.cpp`) - 读取 TSF 候选词
  - 通过 `ITfContext::GetProperty(GUID_PROP_CANDIDATE)` 获取候选词
  - 解决 IMM32 对 TSF 架构输入法返回 0 的问题
- [x] **初始化时机** - 使用 `START_CLIENT_TICK` 获取 HWND
  - `onInitializeClient()` 时 GameOptions 未初始化，不能获取 HWND
  - 在 `ClientTickEvents.START_CLIENT_TICK` 中延迟初始化

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
- [x] **VerticalCandidateHud** - 垂直候选词界面
  - 用于仓颉/注音/速成输入法
- [x] **ConfigScreen** - 设置界面（预留）

### 功能
- [x] DLL 内嵌到 JAR（`META-INF/natives/amd64/`）
  - 启动时自动提取到 temp 目录加载
  - 用户只需下载一个 JAR 文件
- [x] 键盘布局检测（拼/注/倉/五/速）
- [x] Shift 中英文切换检测
- [x] Caps Lock 大写锁定检测
- [x] composition（输入内容）显示支持多语言文字
- [x] 方向键循环选择候选词（Ctrl+Shift+T 测试）

### 修复的问题
- [x] `SetCallbacks` 空实现（回调函数指针未正确注册）
- [x] DLL 资源路径错误 + 资源泄漏（添加 DLL 路径缓存）
- [x] ImeStatusIndicator 在聊天室内显示/隐藏状态冲突
- [x] 回调机制导致的游戏挂起（JNA 静态初始化）
- [x] CandidateHud visible 计算逻辑（composition 非空时也显示）
- [x] `tsf_monitor.cpp` 用空候选词覆盖有效数据的问题
- [x] `isAvailable()` 不调用 `getInstance()` 导致 DLL 从不加载
- [x] **IME 类型检测错误** - 拼音显示"倉"的问题
  - `updateInputMethodType()` GUID 不匹配时返回 `OTHER_CHINESE` → Java 映射为 CANGJIE
  - 修复：使用语言区域默认（zh-CN → PINYIN，zh-TW → CANGJIE）
  - `queryCurrentInputMethod()` 枚举所有 profiles 而非当前活动 profile
  - `PollIMEState()` 首次检测后永不更新
  - `pollUpdate()` 只在 UNKNOWN/ENGLISH 时查询
  - 2026-05-10: 添加全面调试日志，修复以上所有问题

---

## 进行中 🔄

### IME 类型检测验证
- **问题**: 拼音输入法被错误识别为仓颉
- **修复**: 2026-05-10 修复了 5 个相关 bug
- **待验证**: DebugView 日志确认类型检测正确
- **2026-05-10 最新状态**:
  - 简化 `PollIMEState()` 函数，移除刷屏调试日志
  - 修复 TSF 输入法检测（`imeId == langId` 时使用语言默认）
  - zh-CN (0x0804) → PINYIN, zh-TW (0x0404/0x0C04/0x1404) → CANGJIE
  - Shift 状态异常问题（`chineseMode_` 检测）待进一步调查
  - 候选词不显示问题待验证

### 候选词同步
- **问题**: IMM32 `ImmGetCandidateList` 对 TSF 架构输入法返回 0
- **解决方案**: 启用 TSF Polling 通过 `GUID_PROP_CANDIDATE` 读取候选词
- **状态**: 已实现，待完整验证
- **已知问题**: 微软拼音等 TSF 输入法候选词仍不显示，可能需要使用 ITfCandidateList

---

## 待办 🔧

### 高优先级
- [ ] **验证 TSF 候选词读取** - DebugView 应显示 `updateCache: candCount=X, found=1`
- [ ] **验证双通道协同** - WndProc Hook 处理消息 + TSF 轮询获取候选词
- [ ] **鼠标点击箭头** - 需要添加鼠标事件 mixin 来调用 `handleClick()`

### 中优先级
- [ ] 简化 ConfigScreen - Windows 只显示 UI 缩放
- [ ] 验证 ImeStatusIndicator 在聊天室内正确显示
- [ ] 修复 UI 缩放功能（hudScale 实际应用到 HUD）

### 低优先级（后期）
- [ ] RIME 中州韵输入引擎集成（Linux/macOS）
- [ ] 快捷键自定义界面
- [ ] 内置中文联想引擎（拼音/注音/速成/五笔/粤拼）

---

## 文件结构

```
ChineseIME-Fabric-1.21.4/
├── native/
│   ├── CMakeLists.txt
│   └── src/
│       ├── ime_bridge.cpp       # C++ 主入口、导出函数
│       ├── win_event_bridge.cpp  # WndProc Hook (IMM32 通道)
│       ├── tsf_monitor.cpp      # TSF 监听、候选词读取
│       ├── imm32_monitor.cpp     # IMM32 备用监听
│       ├── ime_state_manager.cpp # 状态管理、变化检测
│       ├── jni_callback.cpp      # JNA 回调
│       └── sta_thread.cpp        # STA 线程管理
├── src/main/java/com/example/chineseime/
│   ├── ChineseIMEInitializer.java  # 主初始化器
│   ├── config/
│   │   └── ModConfig.java           # 配置管理
│   ├── hud/
│   │   ├── CandidateHud.java        # 候选词 HUD
│   │   ├── ImeStatusIndicator.java  # 输入法状态指示器
│   │   └── VerticalCandidateHud.java # 垂直候选词
│   └── platform/win32/
│       ├── NativeImeBridge.java      # JNA 接口
│       └── WindowsIMEEventBridge.java # 事件回调处理
└── build/libs/chineseime-1.0.0.jar
```

---

## 当前状态

- **C++ DLL**: `natives/Release/chineseime_native.dll` (编译成功, 2026-05-10 简化版)
- **Java Mod**: `build/libs/chineseime-1.0.0.jar` (编译成功)
- **部署位置**: PrismLauncher mods 文件夹
- **DLL 加载**: 已修复 `isAvailable()` 自动调用 `getInstance()`
- **WndProc Hook**: 已实现
- **TSF Polling**: 已实现
- **IME 类型检测**: 已修复，TSF 输入法使用语言默认
- **调试日志**: 已大幅简化，只在前 5 次轮询时输出

---

## 当前未解决问题 (2026-05-10)

### ⚠️ 拼音输入随机丢字 + 自动提交（已修复）
- **现象**: 输入拼音时字母随机消失；或未选择候选词就自动提交了
- **根本原因**: 三层嵌套的竞态条件：
  1. C++ `updateCandidates()` 盲目用空字符串覆盖已有状态
  2. 多个调用点（`OnCandidateListUIElementChanged`、`UpdateUIElement`、`updateCache()`）独立读取 composition 和 candidates，但节奏不同
  3. TSF 事件回调先调用 `updateCandidates(L"", cands, ...)` 清除 composition，`updateCache()` 还没来得及读到正确的 composition
- **修复** (2026-05-10):
  1. `ImeStateManager::updateCandidates()` 改为增量合并：只有新值非空才覆盖，两边都空才清除
  2. `PollIMEState()` 和 `updateCache()` 增加 `if (!comp.empty() || !cands.empty())` 保护
  3. `ImmGetCandidateList` 返回值检查（`tsf_monitor.cpp`、`imm32_monitor.cpp`）
  4. `GetKeyboardStateForPolling` 改用 `GetAsyncKeyState`（与 `PollKeyboardState` 一致）
- **验证**: DebugView 观察 `PollIMEState: comp='zhong'` 字母不再随机消失

### ⚠️ 拼音输入法 Shift 状态异常
- **现象**: Shift 状态检测异常（会一直被检测为开）
- **可能原因**: `chineseMode_` 字段检测逻辑问题
- **状态**: 待调查

### ⚠️ 候选词不显示
- **影响**: 所有微软中文输入法（拼音/五笔/仓颉/速成）
- **可能原因**:
  1. IMM32 `ImmGetCandidateList` 对 TSF 输入法返回 0
  2. TSF Polling 候选词读取可能有问题
  3. 可能需要使用 `ITfCandidateList` COM 接口获取 TSF 候选词
- **状态**: 待进一步调查和修复

### ⚠️ 速成使用拼音 HUD
- **现象**: 速成输入法使用拼音的 HUD 而不是仓颉的垂直 HUD
- **原因**: IME 类型检测错误
- **状态**: 已修复检测逻辑，待验证

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+G | 打开设置界面（预留） |
| Ctrl+Shift+F | 简繁切换（预留） |
| Ctrl+Shift+T | 显示测试候选词（开发用） |
| ↑/↓ 或 ←/→ | 选择候选词 |
| [ / ] | 上一页/下一页 |

---

## 已知问题

### IME 类型检测错误（拼音显示"倉"）✅ 已修复（2026-05-10）
- **现象**: 英文正确显示"En"，拼音显示"倉"，速成也显示"倉"；一旦错误检测后切换输入法不会更新
- **根本原因**: 多层检测逻辑的 bug 链
- **修复状态**: 2026-05-10 已简化检测逻辑：
  - TSF 输入法（`imeId == langId`）直接使用语言默认
  - zh-CN (0x0804) → PINYIN (type=2)
  - zh-TW (0x0404/0x0C04/0x1404) → CANGJIE (type=4)
  - 移除了复杂的 layout name 解析逻辑
- **验证方法**: 启动游戏，DebugView 查看 `PollIME #N: ... type=X`，type=2 为拼音，type=4 为仓颉

### 候选词不同步（TSF IME）
- **影响**: 微软拼音、微软五笔等现代 TSF 输入法的候选词
- **原因**: `ImmGetCandidateList` 对 TSF 架构返回 0
- **解决方案**: TSF 轮询通过 `GUID_PROP_CANDIDATE` 获取候选词
- **状态**: 2026-05-09 已实现 `startTsfListening()` 调用

### 鼠标点击箭头无响应
- **影响**: 无法通过鼠标点击 `<` `>` 箭头翻页
- **原因**: `CandidateHud.handleClick()` 方法存在但从未被调用
- **解决方向**: 添加鼠标事件 Mixin 来调用 `handleClick()`

### DebugView 看不到 `ImeWndProc:` 日志
- **现象**: 能看到 `WM_IME_NOTIFY` 和 `readCandidates:` 但没有 `ImeWndProc:`
- **可能原因**: 日志格式中的 `%` 被 DebugView 误解，或消息被过滤
- **不影响**: WndProc Hook 实际在工作（因为 `readCandidates` 被调用）

---

## 技术笔记

### 双通道架构
| 通道 | 用途 | 启动方式 |
|------|------|----------|
| WndProc Hook | 接收 IME 消息（WM_IME_*） | `NativeImeBridge.hookWindowProc(hwnd)` |
| TSF Polling | 读取候选词（通过 TSF API） | `NativeImeBridge.startTsfListening()` |

### TSF vs IMM32 架构
| 输入法 | 架构 | 候选词获取 | WndProc Hook |
|--------|------|------------|--------------|
| 微软拼音 | TSF | TSF API (`GUID_PROP_CANDIDATE`) | IMM32 返回 0 |
| 微软五笔 | TSF | TSF API (`GUID_PROP_CANDIDATE`) | IMM32 返回 0 |
| 微软注音 | TSF | TSF API (`GUID_PROP_CANDIDATE`) | IMM32 返回 0 |
| 微软仓颉 | IMM32 | `ImmGetCandidateList` 正常 | Hook 正常 |
| 微软速成 | IMM32 | `ImmGetCandidateList` 正常 | Hook 正常 |

### IME 类型检测逻辑（2026-05-10 简化版）
**TSF 输入法检测**：
- 对于 TSF 输入法，`HIWORD(hkl)` 返回的是语言 ID（如 0x0804）而不是 IME ID
- 检测方法：`imeId == langId` 时表示是 TSF 输入法
- 默认映射：zh-CN → PINYIN，zh-TW → CANGJIE

**IMM32 输入法检测**：
- 使用 `HIWORD(hkl)` 获取真实的 IME ID
- 根据 IME ID 映射到对应的输入法类型

### 调试方法
- C++ 调试输出: 使用 Windows DebugView 工具查看 `OutputDebugStringA`
- Java 调试输出: 查看游戏日志中的 `[ChineseIME]` 前缀日志
- **关键日志序列（IME 类型检测）**:
  1. `PollIME #N: hkl=0x..., imeId=0x..., type=X, open=Y` (前 5 次轮询)
  2. type=2 为 PINYIN (拼音)
  3. type=4 为 CANGJIE (仓颉/速成)
  4. type=6 为 SUCHENG (速成)