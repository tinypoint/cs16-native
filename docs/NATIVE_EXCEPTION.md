# 原异常捕获与菜单初始化阶段

当前目标是继续执行上传的原程序，不改变原引擎或 Steam 的初始化检查。此阶段只验证 macOS ARM64 诊断宿主，浏览器入口仍为 v0.0.1 / `ready=false`，没有游戏画面。

## 原启动路径的实际变化

原 SteamOpenFile 抛出的 `Grid::CLibraryNotInitializedException` 有原有捕获路径。原异常链中，Steam 帧的 FuncInfo 为 `0x202f5da8`，包含类型捕获和兜底捕获；外层引擎也有捕获帧。上一阶段缺少异常运行时，因此停在抛出位置，不能据此判断输入包有问题。

本轮使用原 ThrowInfo、CatchableType、FuncInfo、清理函数和捕获函数。一次实际启动记录为：

```text
[guest-eh] handling frame=10403608 catch=2017e1e2 owner=2017deed
[guest-eh] resume=2017e230 esp=10403328
```

原捕获函数完成后，执行回到原 `0x2017e230`；随后原 vgui2.dll 的 DllMain 返回 1，读取 `platform/Resource/TrackerScheme.res`，并加载菜单、语音等图标。累计 8 个原 DLL 入口返回成功。没有替换 SteamOpenFile，也没有强行设置 Steam 为初始化成功。

## 转换与运行时实现

- 捕获函数的继续地址改用有界控制流遍历，遇到 RET 即结束该路径。原工具的线性扫描会读入相邻函数的返回常量，导致继续地址集合不可靠。
- 将原异常表中的捕获和清理函数作为转换入口；Steam 当前生成 32,898 个候选函数、542 个异常继续入口。候选数量不等于完整正确函数数量。
- 可捕获函数保留宿主 setjmp 入口，状态保存到线程内堆对象，避免 longjmp 后读取被修改的自动变量；有活跃捕获入口的尾部块保留 C 活动帧。
- 按原类型修饰名匹配跨模块类型描述符，支持当前验证的直接基类偏移、引用捕获和数值复制；先执行原清理，再执行原捕获和异常析构，最后恢复原 EBP／ESP 与继续地址。
- 跨宿主回调边界、未知 SEH 帧、虚基类调整和没有对应宿主捕获入口的帧明确停止。RtlUnwind、SEH 过滤器、复杂嵌套／重抛和完整浮点上下文尚未验证；不得视为完整 Windows 异常实现。
- 本轮只重新生成 Steam 的异常入口。其他已链接模块沿用先前生成物；进入它们的捕获路径前仍需重新生成和验证。

异常调度参考固定版本的 MIT pcrecomp 运行时，来源及哈希见 [runtime-source.json](evidence/runtime-source.json)，许可证随 `runtime/guest/LICENSE` 保存。

## 字体与坐标适配

原 VGUI2 初始化实际调用了 EnumFontFamiliesExA、SetMapMode、LPtoDP 和 GetCharABCWidthsW。本轮接入真实 SDL_ttf 字体度量和访客回调，修正 TEXTMETRICA 的 first/last/default/break 字符字段偏移。枚举目前提供有限字体别名映射，未实现 Windows 的完整字体目录和字形一致性；坐标映射只支持 MM_TEXT，其他映射模式失败。

UI 检查覆盖枚举回调返回零即终止、未知字体不回调、ANSI／Unicode 字宽与输出边界、TEXTMETRICA 字段、坐标往返与计数溢出，以及此前的实际 GPU 和共享 DIB 检查。这是兼容层诊断，不是原游戏画面。

## 该阶段的缺口：内置网页控件

后续已经接入系统 WebKit，原启动越过此停止点；最新范围与限制见 [网页组件阶段](WEBBROWSER.md)。以下保留当时的定位结果。

原引擎 HTML 面板构造函数 `0x01d06e70` 请求 CLSID `8856f961-340a-11d0-a96b-00c04fd705a2`。宿主尚未提供这个 Windows WebBrowser COM 类。原函数没有检查 CoCreateInstance 的失败返回，便从输出指针取虚表，因此此前出现访客空指针访问。

现在在缺失系统组件的位置输出明确诊断并停止，仍未提供网页组件，也不返回虚假的激活成功。仅跳过网页加载并不足以解决：构造函数接着请求多个接口并调用方法。由输入映像直接提取到的 IID 为：

| 原 IID 地址 | GUID |
| --- | --- |
| `0x01e8ac88` | `0000010d-0000-0000-c000-000000000046` |
| `0x01e8ac78` | `00000112-0000-0000-c000-000000000046` |
| `0x01e8ac68` | `7fd52380-4e07-101b-ae2d-08002b2ec713` |
| `0x01e8ac58` | `00000113-0000-0000-c000-000000000046` |
| `0x01e8ac48` | `d30c1661-cdaf-11d0-8a3e-00c04fc9e26e` |
| `0x01e8ac38` | `b196b284-bab4-101a-b69c-00aa00341d07` |
| `0x01e8ac28` | `34a715a0-6587-11d0-924a-0020afc7ac4d` |

下一步需要实现接口身份、引用生命周期、站点／尺寸／通知绑定，并将真正的网页呈现接到宿主。IViewObject 的职责包含向设备上下文呈现对象及维护视图通知，参见 [Microsoft 的接口文档](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-iviewobject)。macOS 和浏览器的网页后端需要分别适配；不能通过修改原构造函数或将所有方法返回成功替代这些功能。

## 复现与证据

在已有启动宿主和显示依赖基础上，重新生成 Steam：

```sh
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Steam.dll --output .local/lift/steam
.venv/bin/python tools/build_launcher.py --diagnostic
.venv/bin/python tools/run_launcher.py --probe-exception
.venv/bin/python tools/run_launcher.py --probe-ui
.venv/bin/python tools/run_launcher.py --probe-x87
.venv/bin/python tools/run_launcher.py --probe-filesystem
.venv/bin/python tools/run_launcher.py --probe-version
.venv/bin/python tools/run_launcher.py
```

Steam 的原基址就是 `0x20000000`，无需另行重定位。转换工具会因已报告的指令／边界缺口返回非零，须检查 result.json；最后的实际启动在当时的证据中停于缺失 WebBrowser 适配处；最新停止点见网页组件阶段。5 个独立检查均须成功，不能将预期停止视为游戏启动成功。

异常检查含 4 组访客表布局用例；分析器另含 3 组继续地址控制流边界用例。原 x87 函数的 5 组 x86 对照、文件系统和版本资源检查继续通过。最新机器可读结果见 [exception-startup.json](evidence/exception-startup.json)。
