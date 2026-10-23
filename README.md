# CS 1.6 原程序迁移 · macOS ARM64 与 Web

目标：沿用红警项目的原则，以用户提供的 Windows 原程序和游戏资源为输入，通过静态转换和宿主接口适配，在 Apple Silicon macOS 与浏览器 WebAssembly 上运行。

**不采用 Xash3D、不采用重建的客户端／服务端游戏逻辑，也不以兼容引擎替换原始引擎。** 之前的源码引擎试验已经作废，材料留在被忽略的 `.local/rejected-xash-route/`，不属于当前构建路线。此目录独立于 `ra2-native`，不修改红警工程。

## 当前状态

已接收并解开 `CounterStrike1.6.zip`，完成原程序清单、哈希和 PE／旧 GoldSrc BLOB 格式审计。已将上传的 `mp.dll` 中 3 个原程序函数转换为 C，再编译到 ARM64 与 Wasm，64 组受控对照用例与 Unicorn x86 参考一致。

原始 hl.exe 已在 ARM64 诊断宿主中执行；原引擎创建了 Valve001 窗口和实际 OpenGL 上下文，原客户端已开始执行，ParticleMan 和 GameUI 的原 DLL 入口返回成功。窗口／字体／GPU 检查，以及原浮点环境函数的 5 组 x86 对照通过。**已抓到原版 CS 背景／界面的第一帧，但尚不可玩，完整 Web 运行时仍未建立**。原 Steam 异常现已交给原捕获函数处理，启动继续进入 VGUI2 和菜单资源加载；已接通原 HTML 面板的 WebKit 初始化及部分 COM／DOM 接口，原启动继续加载队伍、计分板和旁观界面资源。句柄复制、实际 CoreAudio/DirectSound 输出和网络接口序号桥接已接通，原启动继续完成好友／服务器界面初始化并提交了实际 OpenGL 画面，目前在 HTML 元素的事件订阅接口停止。详见 [进度与证据](docs/STATUS.md)、[窗口与图形阶段](docs/NATIVE_DISPLAY.md)、[异常处理阶段](docs/NATIVE_EXCEPTION.md)、[内嵌网页组件](docs/WEBBROWSER.md)、[音频与系统接口阶段](docs/AUDIO_STARTUP.md)、[原程序第一帧](docs/FIRST_FRAME.md)。

## 仓库与手机访问

- 源码仓库：https://github.com/tinypoint/cs16-native （同时维护 macOS 与 Web）。
- 固定 HTTPS 入口：https://cs16-native.vercel.app/ 。
- 当前入口版本：v0.0.1，`manifest.ready=false`，明确显示原程序尚不可运行，不下载游戏资源。
- Vercel 项目：`cs16-web`。网页目录为 `web/`，与红警项目独立。
- 当前通过 `tools/deploy_web.py` 直接发布；Vercel 返回未安装 GitHub 集成，尚未启用推送自动部署。详细步骤见 [部署说明](docs/DEPLOYMENT.md)。

## 项目内容

| 路径 | 用途 |
| --- | --- |
| `tools/audit_original.py` | 只读审计上传 EXE／DLL 的格式、导入、导出和哈希 |
| `tools/read_goldsrc_blob.py` | 将旧引擎／客户端 BLOB 解码为分析用内存映像，不改原文件、不伪造 PE 导出表 |
| `tools/probe_original_functions.py` | 原程序函数在 ARM64、Wasm 与 x86 参考之间的对照 |
| `tools/lift_original.py` | PE／BLOB 模块的候选函数发现与 C 转换实验；显式记录缺失指令与边界问题 |
| `runtime/` | 独立的 ARM64 诊断宿主、访客内存、原 DLL 加载与部分 Win32 接口 |
| `tools/build_launcher.py` / `tools/run_launcher.py` | 编译并运行原始启动流程；在独立副本上执行，记录明确停止原因 |
| `tools/inspect_assets.py` | 只读检查资源目录 |
| `tools/upload_server.py` | 用户请求的局域网资源接收服务，不运行上传程序 |
| `repos.lock.json` | 转换工具版本与格式研究来源 |

`incoming/`、`game/`、`vendor/`、`.local/` 均不提交。源码、原游戏文件、生成的游戏代码、测试映像和本机输出分开保存。上传包为用户提供的整合包，其文件与官方发布版的一致性尚未认证。

## 复现第一轮实验

需要 Apple Silicon macOS、Command Line Tools、Python 3.12。Web 函数实验另需 Emscripten SDK 与 Node。

```sh
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python -r requirements.txt
.venv/bin/python tools/bootstrap.py
.venv/bin/python tools/extract_input.py incoming/CounterStrike1.6.zip
.venv/bin/python tools/audit_original.py game/upload-531009895/CounterStrike1.6
.venv/bin/python tools/probe_original_functions.py \
  --module game/upload-531009895/CounterStrike1.6/cstrike/dlls/mp.dll \
  --emsdk /path/to/emsdk
```

不提供 `--emsdk` 时只检查 ARM64；同时提供它才检查两个目标。实验固定了当前上传 DLL 的哈希和导出地址，其他版本须先审计，不应直接更改地址绕过检查。Wasm 实验在 Node 中执行，尚非浏览器兼容性验证。

后续保持手机反复刷新时复用资源缓存、程序与资源分开更新等需求；它们将在实际原程序运行路径建立后接入，不以展示页面代替可玩游戏。

## 原启动流程诊断

详见 [诊断宿主说明](docs/LAUNCHER.md)。这是开发工具，不是已完成的 Mac 应用；当前只支持原生 ARM64。完整宿主尚未编译到 Web，首批函数的 Wasm 验证仍是独立实验。
