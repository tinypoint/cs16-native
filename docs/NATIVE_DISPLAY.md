# 窗口、字体和 OpenGL 诊断阶段

这是 macOS ARM64 原程序启动链的一部分，不是已经完成的 CS 应用。所有引擎、客户端、VGUI 和 GameUI 调用继续来自上传的原 DLL。手机入口仍为 v0.0.1 / `ready=false`；此宿主尚未编译到浏览器。

## 依赖与复现

本机使用 SDL2 2.32.10、SDL2_ttf 2.24.0 的官方 macOS framework ARM64 切片，以及系统 OpenGL 2.1。把这两个 framework 所在目录交给准备工具；二进制与本机路径不提交。依赖只用于窗口、字体和系统图形调用，没有引入替代引擎。

```sh
.venv/bin/python tools/setup_native_display.py --frameworks /path/to/frameworks
```

在 [启动宿主步骤](LAUNCHER.md) 之外，生成以下原模块。带地址的 PE 先应用原重定位表再转换，不修改输入文件。

```sh
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/cstrike/cl_dlls/client.dll --output .local/lift/cstrike-client
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/valve/cl_dlls/particleman.dll --output .local/lift/particleman --image-base 0x40300000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/vgui2.dll --output .local/lift/vgui2 --image-base 0x41000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/valve/cl_dlls/GameUI.dll --output .local/lift/gameui --image-base 0x43000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/DemoPlayer.dll --output .local/lift/demoplayer --image-base 0x45000000
.venv/bin/python tools/build_launcher.py --diagnostic
.venv/bin/python tools/run_launcher.py --probe-ui
.venv/bin/python tools/run_launcher.py --probe-x87
.venv/bin/python tools/run_launcher.py
```

转换工具仍会因已报告的指令／边界缺口返回非零，须检查 result.json；诊断构建不会把缺失指令变为成功空操作。转换器本轮加入 x87 环境支持，旧模块应重新生成。运行器核对所有链接模块的输入哈希，使用独立游戏副本。`CS16_TRACE=1` 可开启详细系统调用跟踪；默认保留模块、文件和故障日志，避免数十万条重复调用掩盖故障。

## 已验证的范围

- 原引擎通过 USER32 注册 `Valve001` 并创建 640×480 的真实 SDL/macOS 窗口。窗口、DC 与 OpenGL 上下文分别使用访客句柄，64 位宿主指针不交给原程序。
- 原引擎创建 OpenGL 上下文，并读取到实际驱动信息：Apple / Apple M1 Pro / 2.1 Metal。原 client.dll 已进入执行，ParticleMan、GameUI 的原 DllMain 返回成功。此结果不代表菜单初始化完成。
- UI 集成检查覆盖窗口／类的创建销毁、类仍有窗口时拒绝注销、独立系统光标句柄，以及字体和共享 DIB 像素。相同文字在向上／向下排列的 32 位 DIB 中各产生 137 个非零像素；角落像素写入落到正确物理行。
- GPU 检查通过 32 位访客调用接口画三角形并回读像素；也核对六个 double 参数的投影矩阵结果。这是独立诊断图形，不是 CS 游戏画面。
- 原引擎 `0x01dd0824` 的浮点控制环境函数，在 5 种初始控制字下与 Unicorn x86 对照，环境中受检查的已定义字段和最终控制字一致。测试覆盖空栈控制字掩码路径，不代表完整 x87 一致性。现有转换器仍采用 double 浮点栈，算术异常和 80 位精度不完整；恢复不同栈拓扑会明确中止。
- CryptoAPI 仅接入临时 provider 和系统随机字节，检查缓冲区边界及失效句柄；未接入密钥容器、签名、加密或认证功能。

## 图形桥接边界

`tools/generate_gl_bridge.py` 从本机 Apple SDK 的公开 GL 函数声明生成标量和固定长度输入向量的 32 位栈转换；double 消耗两个访客字。字符串、纹理、读回、矩阵和 WGL/DC 生命周期由手写代码处理。尚未实现的核心函数保留显式失败入口，执行即停；可选扩展不会宣称支持。当前只提供一个 OpenGL 上下文，像素传输支持部分格式，窗口事件与输入仍为初步适配。

新版导入 thunk 发现用 `FF 25` 指令的绝对操作数匹配真实 IAT，补上以函数指针传入 VGUI 的六字节入口。不能据此声称所有回调都已找到。

## 当前停止原因

本文件对应的历史证据记录了原 SteamOpenFile 的异常停止点。后续已接通受限 C++ 捕获路径，原捕获函数执行后继续进入 VGUI2 和菜单资源加载；随后也已接通 WebKit 初始化，最新停止点为音频初始化的 DuplicateHandle，见 [网页组件阶段](WEBBROWSER.md)。详见 [异常处理阶段](NATIVE_EXCEPTION.md) 和 [最新证据](evidence/exception-startup.json)。

本阶段仍未取得原游戏缓冲区交换或游戏画面。历史结果保留在 [native-display-startup.json](evidence/native-display-startup.json)，用于区分阶段进展。
