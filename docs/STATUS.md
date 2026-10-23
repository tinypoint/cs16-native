# 原程序迁移进度 · 2026-10-23

## 已完成

- 已完整接收并解开上传包：531,009,895 字节，5,278 个条目，解开后 934,761,943 字节，读取时 ZIP CRC 校验通过。
- 输入 SHA-256：`0aca7809be8bb5b164bb4a826b468f9bff1776342febdf4f477d95e746633ad8`。
- 上传包包含 158 张 CS BSP 地图，以及 valve/、cstrike/ 的模型、声音等内容；这不构成全部内容完整性或官方来源证明。
- 原引擎 hw.dll：classic BLOB，5 个节，映像 19,365,888 字节，616 个导入；原 CS 客户端：3 个节，映像 1,294,892 字节，89 个导入。
- 原服务端 mp.dll：32 位 PE；已选取 GetNewDLLFunctions、CEnvSpark::SparkStop、CGrenade::DetonateUse 三个真实导出函数进行转换。
- 生成代码编译为 Mach-O ARM64 与 Wasm32；两目标共 64 组受控用例与 Unicorn x86 参考一致。对照包括 8 个通用寄存器、所检查的已定义标志位、64 KiB 工作内存与映像未被函数意外改写。
- 实验在受控内存中设置一个原有全局指针，生成代码不含改写过的函数指令；测试固定当前 DLL 哈希和导出地址。

## 原启动流程的 ARM64 实测

- 原始 hl.exe CRT 入口已在 ARM64 诊断宿主执行，原加载器实际读取并解码 hw.dll，运行进入引擎地址 `0x01d01f90` 的初始化代码。
- 原始 FileSystem_Stdio.dll、Steam.dll、vgui.dll、Mss32.dll、dbg.dll 的 DllMain 均返回成功。引擎 DllMain／初始化尚未整体完成。
- 文件系统原 CreateInterface 工厂支持有效接口请求，正确拒绝无效请求；真实导出按名称／序号查找及重复加载检查通过。
- 补充原 PE 的版本资源读取，代替“没有版本资源”的继承占位实现。Steam.dll 固定版本字段和 FileVersion 字符串与独立 pefile 解析一致，缺失查询与缺失文件正确失败；随后 Steam 和 VGUI 初始化通过，未跳过原异常或修改原游戏返回值。
- 修正生成代码跨代码块 JMP 造成的宿主栈累积；原 Steam 初始化原先发生的栈溢出已不再复现。此实现使用原生 Clang musttail，尚未验证完整 Wasm 宿主。
- 已接通 USER32、GDI 和 OpenGL 桥接。原引擎创建了 `Valve001` 640×480 原生窗口，建立实际 OpenGL 上下文，读出 Apple M1 Pro 驱动信息。原客户端开始执行，ParticleMan 和 GameUI 的原 DllMain 也返回成功。
- 窗口／类生命周期、真实字体共享位图、GPU 三角形像素回读、double 参数桥接和随机 provider 边界检查通过。原浮点控制环境函数的 5 组 x86 对照通过；完整 x87 精度／异常尚未实现。
- 补上真实 IAT 验证的导入跳转 thunk 入口发现，修复传给 VGUI 的回调入口遗漏。
- 当前停在原 SteamOpenFile 抛出的 `Grid::CLibraryNotInitializedException`，宿主尚不支持原 C++／SEH 展开和捕获。是否为正常可捕获回退或初始化问题仍需调查。没有原游戏缓冲区交换或游戏画面。

上一阶段启动与两个独立检查的精简结果：[original-startup.json](evidence/original-startup.json)。复现与限制：[诊断宿主说明](LAUNCHER.md)。

当前窗口与图形证据：[native-display-startup.json](evidence/native-display-startup.json)，复现说明：[NATIVE_DISPLAY.md](NATIVE_DISPLAY.md)。

## 批量静态转换实验

| 模块 | 已生成候选函数 | 生成错误 | 含未实现指令的函数 | 不可靠的终止边界 |
| --- | ---: | ---: | ---: | ---: |
| mp | 6644 | 0 | 18 | 26 |
| hl | 478 | 0 | 0 | 0 |
| filesystem | 839 | 0 | 0 | 0 |
| vgui | 1796 | 0 | 0 | 0 |
| steam | 32886 | 0 | 18 | 19 |
| mss32 | 1764 | 0 | 17 | 8 |
| dbg | 439 | 0 | 0 | 0 |
| hw | 15089 | 0 | 27 | 6 |
| cstrike-client | 5908 | 0 | 16 | 2 |
| particleman | 555 | 0 | 0 | 0 |
| vgui2 | 2133 | 0 | 16 | 1 |
| gameui | 11783 | 0 | 17 | 2 |
| demoplayer | 671 | 0 | 0 | 0 |

候选入口存在误识别的可能，未实现指令中部分可能来自代码边界／数据混入，不能直接按指令名补写语义。mp.dll 导出表同时含数据导出，已将不在代码范围内的数据符号排除出代码入口。所有缺口仍保存在各模块 result.json。表中除 mp 外的模块已在诊断模式编译链接，只有上述实际执行的启动路径得到验证，不能视为完整模块验证。

## 当前边界与下一步

完整 CS 1.6 尚未启动；没有已验证的 Mac 应用或可玩 Web 页面。下一步追踪原 Steam 文件接口初始化与 C++ 异常处理，继续核对菜单、原客户端回调和图形缺口。C++／SEH、完整 DLL 生命周期、Wasm 内存布局与尾调用兼容仍需完成。

上游基础检查的 16 项通过和之前的源码引擎编译属于已否决的 Xash 路线，不计入本项目原程序迁移证据。红警仓库保持不变。

精简机器可读证据：[initial-original-audit.json](evidence/initial-original-audit.json)。详细导入、函数目录和实验产物保留在本机忽略目录，不提交游戏程序与资源。
