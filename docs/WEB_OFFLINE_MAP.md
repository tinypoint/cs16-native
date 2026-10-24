# 原程序 Web 离线地图阶段

v0.0.4 已在普通桌面 Chrome 155.0.8059.85 中通过原程序离线地图与输入检查：触屏点击进入菜单、加载 `de_dust2`、选择队伍／角色、虚拟摇杆移动、触屏拖动转向、射击。出生结束后，玩家坐标由 `[-832,-704,164.03125]` 变为 `[-879.96875,-656.03125,164.03125]`；原 `CurWeapon` 消息显示弹匣从 20 减至 19。刷新后程序和游戏资源包的网络请求为零。证据见 [动作与缓存检查](evidence/web-offline-map-check.json)。

这是开发测试版。iPhone Safari 稳定性、完整对局、声音输出和外部多人联机尚未验证。部分菜单文字与 HUD 图标仍存在显示瑕疵。不能将桌面触屏事件测试等同于 iPhone 实机验证。

程序和资源在 Vercel 同域托管，首次总下载约 148 MiB，后续按内容哈希缓存。源码保留在 GitHub；原文件、生成 C 和生成二进制不提交 Git。

## 实现与边界

- `mp.dll` 保留原逻辑，PE 重定位至 `0x53000000`，避免与访客堆冲突；原输入文件不变。
- 转换后的启动门禁及服务端注册表门禁按用户授权跳过。同一实例的 loopback + `sv_lan=1` 连接跳过 CD Key 证书与哈希前置要求；这不生成 Steam 身份，不提供外部联机认证。
- 浏览器宿主提供实例内真实 UDP 数据队列、只读文件映射、线程退出、GDI 字形与位图读回、WebGL 旧接口适配。
- Web 和原生宿主共享小块访客堆实现。小块访客分配使用分级 slab，避免每个小对象消耗一个独立页和映射记录。100,000 次小对象分配、重用和扩容检查已通过。
- 鼠标位置先经过一帧供原 VGUI 轮询，随后发送按下；同帧不合并按下／抬起，避免触屏点在上一位置。此适配仅作用于 Web 输入队列。
- 浏览器输入通过原子队列交给游戏线程，再进入原 Win32 鼠标／键盘消息；浏览器主线程不直接修改访客寄存器或调用游戏函数。
- 保留完整引擎和 GameUI 的已转换函数，避免删除间接调用的菜单／移动回调。超大转换单元采用体积优化，降低浏览器编译负担。
- 即时绘图按原 OpenGL 当前属性语义保存颜色／纹理坐标，在每个顶点提交完整布局，修复原程序一次设置颜色后连续提交顶点时的错色和开火断言。
- HUD 回调只读采集原玩家坐标、视角和 `CurWeapon` 消息，用于验证动作效果；不替代游戏模拟。
- 地图资源包补充原 `classes`、菜单图片、Bot 配置和地图依赖；逐项保留原文件哈希。缓存以内容哈希为键，更新页面不要求重复下载未变化的资源。

## 复现

先完成 README 中的原模块转换，再转换服务端模块：

```sh
.venv/bin/python tools/lift_original.py \
  game/upload-531009895/CounterStrike1.6/cstrike/dlls/mp.dll \
  --image-base 0x53000000 --output .local/lift/mp
.venv/bin/python tools/pack_web_map.py --map de_dust2
.venv/bin/python tools/build_web.py --emsdk /path/to/emsdk \
  --entry-selection docs/evidence/web-offline-map-entries.json
.venv/bin/python tools/prepare_web_preview.py --interactive --local-unverified
.venv/bin/python tools/serve_web_test.py --publish --port 8897
```

`tools/check_web_gameplay.cjs` 依次检查地图、角色、移动距离、视角变化和弹匣减少。已通过全流程，并检查出生稳定后的位移和刷新资源请求。发布工具要求证据中的程序、资源分块、前端哈希与暂存文件一致，并要求五项动作检查通过。

桌面 Chromium 的结果不能代替 iPhone Safari 实机验证。macOS 原生宿主已加载地图并处理选队／角色／移动／射击消息，持续渲染两千余帧；完整对局尚未验证。

## v0.0.5 菜单与移动端点击修复

修复 v0.0.4 的两个问题：即时绘图模拟会漏画菜单文字；在主菜单提前启用触控会把手指点击转成视角移动。Web 即时绘图现按每个顶点保存完整属性，提交独立数组并恢复原客户数组状态，避免旧即时模拟器状态污染；纹理属性数量同时受实际 WebGL 顶点属性上限约束。触屏转向只在原客户端进入人物后生效，退出到游戏菜单后恢复普通点击。Web 输入只经过原子队列，不再重复转发 SDL 的 document 键鼠事件。

版本信息现在显示 FPS，并可点击复制当前浏览器、帧数、输入和异常诊断；它放在页面顶部，避免遮住原生菜单的 Start 按钮。保留 Core 的已转换回调，避免进入原初始化／收尾路径时缺少间接调用函数。

393×852 的移动端 Chromium 环境已验证：提前启用触控仍可点主菜单，随后可开图、选人物、移动、转向、射击与缓存刷新。见 [移动菜单修复检查](evidence/web-menu-touch-fix.json)。此验证不能替代 iPhone Safari 实机体验。


## v0.0.6：Safari 开始游戏时网络错误中止

iPhone 诊断显示本机服务器状态已为 2，但尚无客户端 HUD 更新；随后 `w_sendto errno=23`，原程序报告 `NET_SendPacket ERROR: NO ERROR` 并弹出 Fatal Error。桌面 Chrome 中让 WebSocket 构造同步抛出 SecurityError，可以复现同一故障。

Web 启动参数增加原引擎提供的 `-nomaster`，关闭本地局默认向公网主服务器上报的行为。用户上传包的 `readme.txt` 第 47 行明确说明：即使 `sv_lan 1` 也默认上报，可以用该参数退出上报。原游戏文件不变，macOS 启动参数不变。

同时修正 Web Winsock 失败日志：在输出日志之前保存 errno 并转换为 WSA 错误码，避免代理标准输出覆盖 errno 后错误地报告 NO ERROR。页面保留首次具体错误（含原 MessageBox 内容），不再用后续通用 Aborted 文本覆盖。

回归运行：`CS16_TEST_WEBSOCKET_DENIED=1` 配合 `tools/check_web_gameplay.cjs`，在浏览器中同步拒绝外部 WebSocket，检查本机地图、角色、移动、转向、开火及缓存刷新；此模拟不等同于 iPhone 实机验证。

回归结果：v0.0.5 复现同样的 NO ERROR 致命退出，v0.0.6 的地图、角色、移动、转向、射击和缓存刷新全部通过。见 [前后对照](evidence/web-safari-network-fix.json)。


## v0.0.7：关闭 Windows 整合包更新器并保留跨刷新诊断

连续移动测试可复现开局后中止。源程序核对发现，上传整合包的 Core.dll 在 `0x480027C7` 注册一个 60,000 毫秒的定时器，回调为 `0x480018C0`；该回调进入 Windows 包更新器，模块包含 `UpdateThread::Startup`、`UpdateThread::DownloadUpdatePackages` 等实现。它先请求 `std::locale("chs")`，随后使用 Windows 版本、Shell 和网络接口。此前只覆盖了更短的游戏输入测试，未跨过这个定时任务。

最终方案仅在 Web 构建时关闭这一处 Windows 自动更新定时器注册。该任务无法更新 Vercel 的浏览器部署，不属于地图、玩家或战斗执行路径。转换补丁绑定 Core.dll SHA-256 `9e4c01711b697e969cde61ebe2d02838c399fb0b4b0f59cbb180c0860f2a4067` 并检查原调用位置只出现一次；原文件和 macOS 构建代码不变。补丁仅作用于 Web 的转换产物。

绘图层复用 ImageData，避免每帧分配约 1.2 MiB，只在分辨率变化时重建。前端每三秒保存状态和最近日志，重新载入后的诊断包含 previousRun；再次在初始化阶段刷新也保留上一段有效游戏记录。记录不能单凭 Wasm 内存数字确认 Safari 系统终止原因。

验证命令：`CS16_TEST_LONG_MOVEMENT=1 CS16_TEST_WEBSOCKET_DENIED=1` 配合 `tools/check_web_gameplay.cjs`，包含 48 秒交替方向移动、实际原 HUD 状态、帧持续更新、绘图对象复用以及刷新后的历史诊断。桌面回归不能替代 iPhone 实机验证，反复语音现象仍待手机复核。

最终回归全部通过：地图、角色、移动、转向、开火、持续帧、同一绘图对象复用、缓存刷新和上一局记录恢复。见 [运行稳定性证据](evidence/web-runtime-stability.json)。
