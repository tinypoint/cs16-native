# 原程序浏览器 Wasm 实验

目标是让上传包里的原始 `hl.exe` 和原 DLL 在手机浏览器本机执行。不是远程桌面，也不使用 Mac 截图、录屏或替代引擎。当前是启动路径开发实验，尚不可玩。

## 实现

- 同一批原程序静态转换结果与 Win32 接口桥，单独编译到 Emscripten Wasm。macOS 图形桥保持独立，浏览器平台差异在构建目录内生成。
- 稀疏页映射保留原 PE32 虚拟地址，避免为了高地址 DLL 分配整个 4 GiB。跨页、反向指针、重分配与释放复用通过独立 Wasm 内存探针。权限仅作元数据记录，原始转换指令的每次内存访问尚无权限检查。
- 虚拟文件系统采用 Windows 的大小写规则，避免同一资源的 `resource`／`Resource` 拼写造成漏读。
- 独立映射原纹理编号与 WebGL 对象，保留桌面 OpenGL 首次绑定新编号即创建纹理的语义。旧的 RGB／RGBA 存储格式转为 WebGL 可用的上传格式，低位深颜色先量化；不以图片替换绘制。
- 原引擎的 OpenGL 调用进入 Emscripten 固定管线兼容层和 worker WebGL。可见画布只接收当前 Wasm 实例实际绘制缓冲区的像素。
- 首帧实验在实际彩色缓冲区出现后主动停止。没有接入点击、地图载入或完整游戏循环；不能据此宣称可玩。
- 为控制手机下载量，仅保留启动需要的原函数体。未包含函数一旦被调用就记录原地址并终止，不能以空实现继续。选择配置包含原生启动实测入口和浏览器追加入口；原文件与生成的原始转换结果保持不变。
- 只打包启动过程中实际读取的原文件与所需原 DLL；不含完整地图资源。字体使用 Liberation Sans 2.1.5，携带原许可证。
- HTML 面板使用浏览器 DOM 后端，目前仅支持初始空文档及部分属性，尚不支持网页快照及完整元素事件接口。网卡枚举仅暴露兼容层创建的、地址为 0.0.0.0 的软件套接字接口，名称明确标识 WebAssembly，随机生成局部单播标识；不读取或冒充手机物理网卡。发送与数据报接收缓冲限制作用于实际 Emscripten WebSocket 队列，超限时拒绝发送或丢弃 UDP 数据报。尚无原始 TCP/UDP 或局域网联机能力。进程 CPU 计时返回失败。

## 构建

先按原生文档生成各模块的转换结果。可用原生构建的 `--profile-entries` 配合环境变量 `CS16_PROFILE_TARGETS=1` 记录启动入口。SDK 缓存放在本工程的 `.local/emscripten-cache`。

```sh
.venv/bin/python tools/build_web.py --emsdk /path/to/emsdk --memory-probe
.venv/bin/python tools/build_web.py --emsdk /path/to/emsdk \
  --entry-selection docs/evidence/web-first-frame-entries.json
.venv/bin/python tools/pack_web_startup.py --file-list docs/evidence/web-startup-files.json
.venv/bin/python tools/prepare_web_preview.py --local-unverified
.venv/bin/python tools/serve_web_test.py --publish --port 8897
```

资源打包工具从原生启动的文件访问日志或已记录的输入清单提取文件集合，另包含原始菜单资源目录；需要保留那次启动的日志，不能拿接口探针日志替代。原游戏输入、生成代码、二进制和资源包均不提交 Git。

## 网页与发布

网页按 SHA-256 分别缓存压缩程序与启动资源，刷新只重新请求小型清单，内容没变时复用 CacheStorage；浏览器清理站点数据后需要重新下载。程序与资源可以独立更新。

发布前必须验证浏览器实际输出了原程序像素；`prepare_web_preview.py` 和 `publish_web_preview.py` 会检查本地验证结果。`--local-unverified` 仅供本地调试，其输出不允许发布。清单保持顶层 `ready=false`，首帧实验通过单独的 `preview` 字段开放。

生产网页验证：`serve_web_test.py --publish --port 8897`，启用与 Vercel 一致的隔离策略与 CSP。最终仍需真实 iPhone 验证，桌面浏览器的通过不能代替手机验证。

启动资源另外需要 `.local/fonts/LiberationSans-Regular.ttf` 和同发行版的 `LICENSE`，来源为 Liberation Fonts 2.1.5。`--adapter-probe` 检查虚拟网卡的 ABI／缓冲边界与实际套接字队列限额。

正式加载器检查使用 `tools/check_web_frame.cjs`，通过 `CS16_PLAYWRIGHT` 指定 Playwright 包路径、`CS16_BROWSER_EXECUTABLE` 指定 Chromium 路径；`CS16_CHECK_RELOAD=1` 额外检查刷新后没有再次请求大程序包和资源包。验证记录绑定实际压缩程序、加载器与资源哈希，无法拿其他构建的首帧证据发布。
