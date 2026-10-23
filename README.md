# CS 1.6 原程序迁移 · macOS ARM64 与 Web

目标：沿用红警项目的原则，以用户提供的 Windows 原程序和游戏资源为输入，通过静态转换和宿主接口适配，在 Apple Silicon macOS 与浏览器 WebAssembly 上运行。

**不采用 Xash3D、不采用重建的客户端／服务端游戏逻辑，也不以兼容引擎替换原始引擎。** 之前的源码引擎试验已经作废，材料留在被忽略的 `.local/rejected-xash-route/`，不属于当前构建路线。此目录独立于 `ra2-native`，不修改红警工程。

## 当前状态

已接收并解开 `CounterStrike1.6.zip`，完成原程序清单、哈希和 PE／旧 GoldSrc BLOB 格式审计。已将上传的 `mp.dll` 中 3 个原程序函数转换为 C，再编译到 ARM64 与 Wasm，64 组受控对照用例与 Unicorn x86 参考一致。

这仅证明第一批隔离函数的转换路径，**还没有启动完整 CS 1.6，也没有可玩的 Mac 或 Web 入口**。引擎级动态库加载、接口、回调和实际对局仍需开发与验证。详见 [进度与证据](docs/STATUS.md)、[原程序迁移路线](docs/PORT_PLAN.md)。

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
