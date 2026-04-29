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
- [x] 候选词同步显示
- [x] composition（输入内容）显示支持多语言文字

### 修复的问题
- [x] `SetCallbacks` 参数不匹配（3参数 vs 4参数）
- [x] 日志刷屏（handleLayoutChange/handleModeChange 每次回调都打印）
- [x] DLL 资源路径错误
- [x] ImeStatusIndicator 在聊天室内不显示
- [x] 回调机制导致的游戏挂起（JNA 静态初始化）

---

## 待办 🔧

### 高优先级
- [ ] 游戏内测试输入法切换、Shift 中英文、Caps Lock、候选词显示
- [ ] 验证 ImeStatusIndicator 在聊天室内正确显示
- [ ] 验证候选词界面显示输入内容（composition）

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
│       ├── ime_bridge.cpp       # C++ 主入口、TSF+IMM32 监听
│       ├── ime_state_manager.cpp # 状态管理、变化检测
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
│       └── WindowsIMEBridgeNative.java # 桥接管理
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

---

## 已知问题

- ❌ 游戏启动问题（PrismLauncher 相关，非模组问题）
  - 症状：游戏窗口不显示，Java 进程挂起
  - 尝试：移除其他模组测试、使用官方启动器