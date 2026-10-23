# 原程序第一帧 · 2026-10-23

ARM64 诊断宿主已收到原 CS 引擎的第一次 OpenGL SwapBuffers，实际缓冲区包含 Counter-Strike 启动背景；此前带 -console 的诊断启动也绘出了原控制台。图像来自原引擎和原资源，没有用自制页面或合成图替代游戏。此前的独立 GPU 三角形检查与本次原程序画面分开记录。

**这仍不是可玩的游戏。** 随后原 HTML 面板在给元素订阅事件时请求 IConnectionPointContainer，当前部分 MSHTML 桥接不支持该接口；原程序未检查此失败，继续解引用空指针而停止。还没有证明菜单持续运行、进入地图或完整音频／网络游戏行为。Web 完整运行时尚未建立，Vercel ready=false。

精简证据、原输入哈希、启动日志哈希与九组检查见 [first-frame.json](evidence/first-frame.json)。原缓冲区抓图只保存在本机忽略目录，不把游戏图像／资源打入源码仓库。

## 本轮修复

- GetAdaptersInfo：用 getifaddrs 枚举真实非 loopback IPv4 接口，序列化 32 位 XP 的 640 字节 IP_ADAPTER_INFO。包含 MAC、索引、地址／掩码链；SystemConfiguration 能提供时读取网关及 DHCP 服务信息。32 位租期饱和至 INT32_MAX，未知／不存在的配置字段保持空值。没有重配网络，没有虚构网卡。
- 支持查询所需大小和缓冲区不足重试，检查访客地址可写及溢出；独立检查核对宿主 IPv4、掩码、MAC、多条地址链、输出哨兵及小缓冲区不被写入。不代表所有网络配置／Windows IP Helper 接口均已支持。
- WSASocketA/W：支持不带协议描述和分组的普通 IPv4 socket 创建，允许普通与 OVERLAPPED 创建标记；自定义 provider、其他标记和分组明确失败。异步传输和完成端口仍未实现；回环 UDP 通过该创建入口完成真实收发。
- ExtTextOutA/W：字节或 UTF-16 转实际字体绘制，支持透明／不透明背景矩形、裁剪、文本对齐及共享 DIB。无效 UTF-16 拒绝；glyph-index、PDY、自定义字符位移和双向排版未实现。修正 GetPixel 原先直接返回像素格式数值的问题，现转回 COLORREF 并应用坐标原点。
- OpenGL：接通访客顶点、纹理坐标、颜色和法线数组，DrawArrays／DrawElements 在绘制前验证所引用访客内存跨度。读取索引最大值，并核对实际启用的数组。VBO、index／edge arrays 和 glArrayElement 仍明确不支持。独立 GPU 检查用交错访客数据和反向索引绘制并核对红／蓝像素。
- 修正 import 身份发布时机：原实现查找入口时设置全局名称，执行前让出访客机器后可能被其他线程覆盖，导致共享 COM property handler 用了错误的方法名。现在进入 shim 后设置并恢复身份，机器切换与嵌套访客调用也保存该上下文；新增嵌套导入身份检查。
- 修正 HTML 滚动通知递归：只在实际 DOM 滚动位置变化时标记脏状态，下次消息泵发送通知。同位置设置不再通知；WebKit 检查复现“收到通知后再次设置同一滚动值”并验证不递归、不多发事件。相同尺寸也不再反复调整 backing window。

接口布局依据 Microsoft [GetAdaptersInfo](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersinfo)、[IP_ADAPTER_INFO](https://learn.microsoft.com/en-us/windows/win32/api/iptypes/ns-iptypes-ip_adapter_info) 和 [WSASocketA](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasocketa)。macOS 网卡配置来自系统 SDK API，未复制其他引擎实现。

## 复现

完成 [音频阶段](AUDIO_STARTUP.md) 的模块准备后：

```sh
.venv/bin/python tools/generate_gl_bridge.py
.venv/bin/python tools/build_launcher.py --diagnostic
.venv/bin/python tools/run_launcher.py --probe-network
.venv/bin/python tools/run_launcher.py --probe-ui
.venv/bin/python tools/run_launcher.py --probe-kernel
.venv/bin/python tools/run_launcher.py --probe-browser
CS16_CALLBACK_TRACE=1 .venv/bin/python tools/run_launcher.py
```

启动默认 `hl.exe -game cstrike -windowed`；设置 `CS16_CONSOLE=1` 会增加原启动参数 `-console`。抓取原缓冲区保存在 `.local/first-game-buffer.bmp`，默认第一帧；`CS16_CAPTURE_FRAME=N` 可指定 1–10000 范围内的帧。指定帧未到达时不会生成新抓图，须核对当前运行日志与文件时间，避免把旧抓图当作本次输出。

诊断日志区分第一次提交、抓取的具体帧与看门狗时的累计帧数。第一帧标记只能证明缓冲区提交，需要实际查看抓图确定内容。宿主仍保留 15 秒看门狗与未实现接口的明确停止，不改写原游戏检查。
