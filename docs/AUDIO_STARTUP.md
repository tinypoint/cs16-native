# 音频与系统接口阶段

原程序仍通过静态转换执行。句柄复制、实际 DirectSound/CoreAudio 输出已接通，原音频初始化继续到原好友／服务器界面；尚无原游戏帧，也没有完整浏览器运行时。本阶段停止位置和输入哈希见 [机器证据](evidence/audio-startup.json)；后续已得到原程序画面，最新状态见 [第一帧阶段](FIRST_FRAME.md)。

## 实现与验证范围

- DuplicateHandle：同一访客进程、SAME_ACCESS、不可继承的真实对象共享；支持独立关闭、关闭源句柄、当前线程伪句柄转真实句柄、工作线程退出后查询。跨进程、权限变更与继承请求明确失败。
- 线程对象的运行期引用与句柄引用分开，pthread TLS 析构标记线程退出并释放运行期引用。递归临界区沿用访客机器锁，保存 spin hint，不宣称 Windows 自旋行为完全一致。
- INI 字符串支持大小写不敏感、默认值、引号、枚举、截断和缓冲区边界；GetProfileStringA 使用 win.ini，缺失时走调用方默认值。
- 模块注册表由 32 扩到 256；修复容量耗尽时把已支持的系统库误判为缺失。UTF-16 模块名经过边界／代理项验证后按名称加载。
- 原 Mss32 使用实际 DirectSound，经 SDL 输出至 CoreAudio，44100 Hz。共享 PCM 数据与独立播放游标、分段 Lock、Unlock、音量、声道平衡、频率调整有独立检查。回调读取受锁保护的宿主快照，避免与访客写 PCM 并发。独立硬件检查播放静音并核对游标推进，不构成原游戏音频质量证明。
- waveOut、录音、Windows mixer 和 MCI CD 音频没有实现，现明确报告无设备／不支持；原程序按自身失败分支继续，不用静音假设备冒充可用设备。无硬件／3D 混音，原游戏流式音频仍需验证。
- Winsock 使用真实 BSD socket。增加 Windows fd_set 布局转换、select、shutdown、__WSAFDIsSet。回环 UDP 收发与边界检查通过；尚无联机游戏验证。
- wsock32.dll 和 ws2_32.dll 的 10–12 序号不同，分别映射。最初误用同一表，造成 ioctlsocket 被当作单参数函数、ESP 少恢复 8 字节，最终服务器列表列号变成堆指针。修复保留两套 ABI，新增直接调用序号入口的 stdcall 栈平衡检查，防止辅助调用器恢复 ESP 掩盖问题。
- IsDebuggerPresent 查询当前 macOS 进程的 P_TRACED 状态；查询失败明确停止，不固定伪造调试器状态。

网络序号依据 [Wine wsock32 导出规范](https://github.com/wine-mirror/wine/blob/master/dlls/wsock32/wsock32.spec) 与 [ws2_32 导出规范](https://github.com/wine-mirror/wine/blob/master/dlls/ws2_32/ws2_32.spec)。仅使用接口事实，没有复制 Wine 的网络实现。

## 复现新增模块

先完成 [基础模块](LAUNCHER.md) 与 [图形阶段模块](NATIVE_DISPLAY.md)。以下命令均从仓库根执行；当前上传包的固定地址和哈希不可直接套用到其他版本。

```sh
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Mss32.dll --output .local/lift/mss32 --entry 0x2111c344
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Mp3dec.asi --output .local/lift/mp3dec
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Mssv29.asi --output .local/lift/mssv29
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/Mssv12.asi --output .local/lift/mssv12
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/voice_miles.dll --output .local/lift/voice-miles --image-base 0x47000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/core.dll --output .local/lift/core --image-base 0x48000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/swda.dll --output .local/lift/swda --image-base 0x49000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/platform/Friends/TrackerUI.dll --output .local/lift/trackerui --image-base 0x4a000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/platform/Servers/ServerBrowser.dll --output .local/lift/serverbrowser --image-base 0x4c000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/platform/Friends/TrackerNET.dll --output .local/lift/trackernet --image-base 0x4e000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/steamclient.dll --output .local/lift/steamclient --image-base 0x50000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/vstdlib_s.dll --output .local/lift/vstdlib --image-base 0x51000000
.venv/bin/python tools/lift_original.py game/upload-531009895/CounterStrike1.6/tier0_s.dll --output .local/lift/tier0 --image-base 0x52000000
.venv/bin/python tools/build_launcher.py --diagnostic
.venv/bin/python tools/run_launcher.py --probe-kernel
.venv/bin/python tools/run_launcher.py --probe-audio
.venv/bin/python tools/run_launcher.py --probe-network
.venv/bin/python tools/run_launcher.py
```

原音频混音器含手写汇编入口，启发式发现未识别到 `0x2111c344`。新增 `--entry` 将实际观察到的原始代码入口加入发现根，校验其位于原代码节并记录在 result.json；没有替换汇编逻辑。转换仍可能以非零退出并保存未实现指令／边界诊断，须逐项检查；默认构建仍拒绝未解决诊断。

九组独立检查包括文件系统、版本、UI／GPU、x87、异常、WebKit、kernel、音频和网络。它们验证各自限定行为，不能视为完整 CS 或完整 Win32 兼容层通过。高地址布局、完整图形和输入、线程／尾调用的浏览器实现仍待完成，Vercel 的 manifest.ready 仍为 false。
