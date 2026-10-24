# CS 1.6 原程序迁移 · macOS ARM64 与 Web

目标：沿用红警项目的原则，以用户提供的 Windows 原程序和游戏资源为输入，通过静态转换和宿主接口适配，在 Apple Silicon macOS 与浏览器 WebAssembly 上运行。

**不采用 Xash3D、不采用重建的客户端／服务端游戏逻辑，也不以兼容引擎替换原始引擎。** 之前的源码引擎试验已经作废，材料留在被忽略的 `.local/rejected-xash-route/`，不属于当前构建路线。此目录独立于 `ra2-native`，不修改红警工程。

## 当前状态

Web v0.0.5 开发构建已在普通 Chrome 中通过：原程序加载 `de_dust2`、触屏选队与角色、虚拟摇杆移动、拖动转向、开火和刷新缓存。测试检查原 HUD 回调中的实际玩家坐标、视角与弹匣变化。详见 [离线地图与操作](docs/WEB_OFFLINE_MAP.md) 和 [验证记录](docs/evidence/web-offline-map-check.json)。

**iPhone Safari 实机反馈正在跟进；完整对局、声音输出和外部多人联机尚未验证。** v0.0.5 已修复菜单文字缺失、提前打开触控后菜单不能点击的问题，并增加可点击复制的 FPS／运行诊断。 macOS ARM64 宿主已加载相同原地图，处理选队／角色／移动／开火输入并持续渲染两千多帧；尚未整理成可交付的 Mac 应用。

早期转换与对照记录见 [进度与证据](docs/STATUS.md)、[窗口与图形](docs/NATIVE_DISPLAY.md)、[异常处理](docs/NATIVE_EXCEPTION.md)、[内嵌网页组件](docs/WEBBROWSER.md)、[音频与系统接口](docs/AUDIO_STARTUP.md) 和 [原程序第一帧](docs/FIRST_FRAME.md)。

## 仓库与手机访问

- 源码仓库：https://github.com/tinypoint/cs16-native （同时维护 macOS 与 Web）。
- 固定 HTTPS 入口：https://cs16-native.vercel.app/ 。
- Web 离线地图测试版本：v0.0.5。原引擎在浏览器本机执行，不串流电脑画面。按用户授权，转换后控制流跳过启动和本机 LAN 的 CD Key 门禁，原输入文件不变；这不提供 Steam 身份或外部联机认证。清单顶层 `ready=false`，仅开放开发测试 `preview`。
- Vercel 项目：`cs16-web`。网页目录为 `web/`，与红警项目独立。
- 首屏实验通过 `tools/prepare_web_preview.py` 与 `tools/publish_web_preview.py` 发布；Vercel 返回未安装 GitHub 集成，尚未启用推送自动部署。详细步骤见 [部署说明](docs/DEPLOYMENT.md)。

## 项目内容

| 路径 | 用途 |
| --- | --- |
| `tools/audit_original.py` | 只读审计上传 EXE／DLL 的格式、导入、导出和哈希 |
| `tools/read_goldsrc_blob.py` | 将旧引擎／客户端 BLOB 解码为分析用内存映像，不改原文件、不伪造 PE 导出表 |
| `tools/probe_original_functions.py` | 原程序函数在 ARM64、Wasm 与 x86 参考之间的对照 |
| `tools/lift_original.py` | PE／BLOB 模块的候选函数发现与 C 转换实验；显式记录缺失指令与边界问题 |
| `runtime/` | 独立的 ARM64 诊断宿主、访客内存、原 DLL 加载与部分 Win32 接口 |
| `tools/build_launcher.py` / `tools/run_launcher.py` | 编译并运行原始启动流程；在独立副本上执行，记录明确停止原因 |
| `tools/build_web.py` / `runtime/web/` | 原程序 Wasm 构建、稀疏内存、WebGL 与浏览器宿主接口 |
| `tools/check_web_frame.cjs` / `tools/check_web_gameplay.cjs` | 首屏、触屏离线地图动作和刷新缓存验证 |
| `tools/pack_web_map.py` | 单独打包原地图、模型、声音及依赖，按内容哈希缓存 |
| `tools/pack_web_startup.py` | 按原文件哈希打包启动资源，携带字体许可证 |
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

离线地图版首次下载约 152 MiB，程序、启动资源和地图分块分别按内容哈希缓存；连续刷新检查没有重新请求这些大包。页面自动启动到原主菜单，选择新游戏后进入 `de_dust2`。进入人物后点右上「触控」启用左侧移动摇杆、屏幕拖动转向和右侧动作按钮；电脑浏览器可使用鼠标／键盘与「鼠标」锁定按钮。存档持久化和外部联机尚未接入。

## 原启动流程诊断

详见 [诊断宿主说明](docs/LAUNCHER.md)。这是开发工具，不是已完成的 Mac 应用；原生 ARM64 与浏览器 Wasm 分别构建；Web 当前提供原程序离线地图开发测试。
