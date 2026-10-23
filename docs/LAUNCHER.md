# 原启动流程诊断宿主

此宿主编译上传原程序转换后的 C，并适配其 Windows 系统调用。它没有替换原引擎或客户端。通用 Win32／访客内存代码来源和 MIT 许可见 `runtime/win32/UPSTREAM.md` 与 `docs/evidence/runtime-source.json`，红警工程不做修改。

## 复现

先按 README 准备输入、工具和 Python 环境。当前实验固定上传包版本；其他版本不能照用地址。

窗口阶段新增 SDL 依赖和五个客户端／UI 模块，完整补充步骤见 [窗口与图形阶段](NATIVE_DISPLAY.md)。下列命令仍是基础模块步骤，应与补充步骤及 [音频／依赖模块步骤](AUDIO_STARTUP.md) 一起执行。

```sh
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/hl.exe --output .local/lift/hl
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/FileSystem_Stdio.dll --output .local/lift/filesystem --image-base 0x40000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/vgui.dll --output .local/lift/vgui --image-base 0x40100000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Steam.dll --output .local/lift/steam
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Mss32.dll --output .local/lift/mss32 --entry 0x2111c344
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/dbg.dll --output .local/lift/dbg --image-base 0x40200000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/hw.dll --output .local/lift/hw
.venv/bin/python tools/build_launcher.py --diagnostic
.venv/bin/python tools/run_launcher.py --probe-filesystem
.venv/bin/python tools/run_launcher.py --probe-version
.venv/bin/python tools/run_launcher.py
```

部分模块仍有缺失指令或不可靠代码边界，`lift_original.py` 会保存输出并返回非零。必须审阅 `.local/lift/*/result.json`。默认构建拒绝这些缺口；`--diagnostic` 仅允许追踪已经覆盖的启动路径，执行到未实现指令仍中止。它不表示模块已经完整转换。

启动工具先复制原游戏到 `.local/run/game`（APFS 上采用写时复制），然后校验所链接原模块的 SHA-256。输入目录保留不动。详细日志位于忽略的 `logs/`，宿主有 15 秒看门狗；缺少模块、未转换跳转和所需系统导入会明确停止。

## 当前实现范围

- PE 原始重定位先应用到生成用副本，运行时映射到同一地址；原文件保持原样。DLL 导入按模块解析，真实导出按名称或序号查找，先注册模块再运行原 DllMain，以处理依赖回调。转发导出和完整卸载生命周期尚未实现。
- BLOB 引擎由原 hl.exe 的加载代码读取和解码，不能把分析用内存映像伪装成 PE。引擎固定地址落在原启动器的大块数据预留范围内，这是当前上传程序的原布局。
- 生成代码的跨函数 JMP 使用 Clang `musttail`，避免原代码块循环累积宿主 C 栈。普通 CALL 保持调用语义，未知目标明确中止。
- 文件版本读取来自原 PE 的 RT_VERSION，支持根固定结构、二进制子块及 ASCII 字符串查询；非 ASCII 字符串转换会明确失败，尚无 MUI 合并。接口依据 [GetFileVersionInfoA](https://learn.microsoft.com/en-us/windows/win32/api/winver/nf-winver-getfileversioninfoa) 与 [VerQueryValueA](https://learn.microsoft.com/en-us/windows/win32/api/winver/nf-winver-verqueryvaluea) 的调用约定实现，版本字段由独立 pefile 解析结果核对。
- 已支持受限的原 MSVC x86 C++ 异常捕获及继续入口，范围与检查见 [异常阶段](NATIVE_EXCEPTION.md)。尚未支持的异常输出参数和原生调用链后停止，不吞掉异常或修改游戏返回值。

## 限制

这是不完整的 Win32 兼容层，部分继承接口仍有简化行为。当前成功路径只能证明已执行过的步骤。高地址 DLL 映射适用于此原生诊断实验，不能直接搬入 iPhone 的稠密 Wasm 线性内存；Web 还需要紧凑映射、尾调用兼容验证、图形和输入实现。不能由本机进展推断手机已经可玩。
