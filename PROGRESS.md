# ChineseIME Mod 重构进度

## 项目概述
- **目标**: Fabric 1.21.4 中文输入法显示模组，支持 Windows/Linux/macOS
- **C++ DLL**: 处理 TSF/IMM32 IME 状态检测
- **Java Mod**: 接收 DLL 数据并显示 UI

---

## 已完成 ✅

### 架构
- [x] C++ DLL + JNA 架构（按 mod_debug_idea.txt 实现）
- [x] `extern "C"` + `__declspec(dllexport)` 防止名称粉碎
- [x] `GetKeyboardState` 检测 Caps Lock（非 `GetAsyncKeyState`）
- [x] `ImmGetOpenStatus` 检测中文模式
- [x] `ImeStateManager.checkChanges()` 只在变化时回调
- [x] 60fps 轮询，避免日志刷屏
- [x] 懒加载 DLL，避免游戏启动挂起
- [x] 纯轮询模式（JNA 回调改用轮询，更稳定）

### UI 组件
- [x] **ImeStatusIndicator** - 极简风格，半透明背景
  - 聊天室显示输入法类型（拼/注/仓/五）
  - Caps Lock 时蓝色背景
  - Shift 中英文切换时黄色方块指示器
- [x] **CandidateHud** - 候选词界面
  - 半透明背景，高度 40px
  - 选中词语左侧 3px 蓝色条
  - 左右箭头 `<` `>` 翻页
  - 输入内容显示在左侧（罗马拼音/注音符号/仓颉等）
- [x] **ConfigScreen** - 设置界面
  - Ctrl+G 打开
  - Windows: 仅 UI 缩放和候选词数
  - Linux/macOS: 输入模式、简繁切换、快捷键设置（预留）

### 功能
- [x] DLL 正确路径: `/native/chineseime_native.dll`
- [x] 键盘布局检测（拼/注/仓/五/速）
- [x] Shift 中英文切换检测
- [x] Caps Lock 大写锁定检测
- [x] composition（输入内容）显示支持多语言文字
- [x] 方向键循环选择候选词

### 修复的问题
- [x] `SetCallbacks` 空实现（回调函数指针未正确注册）
- [x] 日志刷屏（减少为每 10 秒打印一次）
- [x] DLL 资源路径错误 + 资源泄漏（添加 DLL 路径缓存）
- [x] ImeStatusIndicator 在聊天室内显示/隐藏状态冲突
- [x] 回调机制导致的游戏挂起（JNA 静态初始化）
- [x] CandidateHud visible 计算逻辑（composition 非空时也显示）
- [x] tsf_monitor.cpp 用空候选词覆盖有效数据的问题

---

## 待办 🔧

### 高优先级
- [ ] **微软拼音候选词同步** - `ImmGetCandidateList` 对 TSF 架构 IME 返回 0，需要实现 `ITfUIElementMgr` 接口获取候选词
- [ ] **鼠标点击箭头** - 需要添加鼠标事件 mixin 来调用 `handleClick()`
- [ ] 验证 ImeStatusIndicator 在聊天室内正确显示

### 中优先级
- [ ] 简化 ConfigScreen - Windows 只显示 UI 缩放
- [ ] 添加 KeyBinding 到 Minecraft 设置界面
- [ ] 修复 UI 缩放功能（hudScale 实际应用到 HUD）

### 低优先级（后期）
- [ ] RIME 中州韵输入引擎集成（Linux/macOS）
- [ ] 快捷键自定义界面
- [ ] 滑块组件（设置界面）
- [ ] 内置中文联想引擎（拼音/注音/速成/五笔/粤拼）

---

## 文件结构

```
ChineseIME-Fabric-1.21.4/
├── native/
│   └── src/
│       ├── ime_bridge.cpp       # C++ 主入口、轮询线程
│       ├── ime_state_manager.cpp # 状态管理、变化检测
│       ├── tsf_monitor.cpp      # TSF 监听、输入法切换检测
│       ├── imm32_monitor.cpp    # IMM32 备用监听
│       ├── jni_callback.cpp     # JNA 回调
│       └── ...
├── src/main/java/com/example/chineseime/
│   ├── ChineseIMEInitializer.java  # 主初始化器
│   ├── config/
│   │   ├── ConfigScreen.java       # 设置界面
│   │   └── ModConfig.java          # 配置管理
│   ├── hud/
│   │   ├── CandidateHud.java       # 候选词 HUD
│   │   └── ImeStatusIndicator.java # 输入法状态指示器
│   ├── keybind/
│   │   └── KeyBindingManager.java  # 键位管理
│   └── platform/win32/
│       ├── NativeImeBridge.java    # JNA 接口
│       ├── WindowsIMEBridgeNative.java # 桥接管理
│       └── NativeLoader.java        # DLL 加载器
└── build/libs/chineseime-1.0.0.jar
```

---

## 当前状态

- **C++ DLL**: `natives/Release/chineseime_native.dll` (编译成功)
- **Java Mod**: `build/libs/chineseime-1.0.0.jar` (编译成功)
- **部署位置**: PrismLauncher mods 文件夹

---

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+G | 打开设置界面 |
| Ctrl+Shift+F | 简繁切换 |
| Ctrl+Shift+T | 显示测试候选词（开发用） |

---

## 已知问题

### 候选词不同步（TSF IME）
- **影响**: 微软拼音、微软五笔等现代 TSF 输入法的候选词无法同步
- **原因**: `ImmGetCandidateList` 对 TSF 架构 IME 返回 0，现代输入法使用 `ITfUIElementMgr` 接口
- **临时方案**: 使用内置词库回退显示候选词
- **解决方向**: 实现 `ITfUIElementMgr::GetUIElement` 接口获取候选词

### 鼠标点击箭头无响应
- **影响**: 无法通过鼠标点击 `<` `>` 箭头翻页
- **原因**: `CandidateHud.handleClick()` 方法存在但从未被调用
- **解决方向**: 添加鼠标事件 Mixin 来调用 `handleClick()`

---

## 技术笔记

### TSF vs IMM32 架构
| 输入法 | 架构 | 候选词获取 |
|--------|------|------------|
| 微软拼音 | TSF | `ImmGetCandidateList` 返回 0 |
| 微软五笔 | TSF | `ImmGetCandidateList` 返回 0 |
| 微软注音 | TSF | `ImmGetCandidateList` 返回 0 |
| 微软仓颉 | IMM32 (传统) | `ImmGetCandidateList` 正常工作 |
| 微软速成 | IMM32 (传统) | `ImmGetCandidateList` 正常工作 |

### 调试方法
- C++ 调试输出: 使用 Windows DebugView 工具查看 `OutputDebugStringA`
- Java 调试输出: 查看游戏日志中的 `[ChineseIME]` 前缀日志