# macOS 内嵌网页组件

原 hw.dll 的 HTML 面板需要 Windows WebBrowser COM 控件。本轮增加宿主接口适配，后端使用系统 WKWebView；引擎、VGUI、菜单与客户端逻辑仍执行上传的原代码。此实现仅用于 macOS 诊断宿主，完整 WebAssembly 后端仍未建立。

## 本轮实际进展

原程序成功创建了 WebKit 对象，越过此前 `0x01d06e70` 的停止点，随后读取 CS 的 MOTD、队伍选择、计分板、旁观界面和 config.cfg。当前停止于 Mss32 音频初始化调用的 `DuplicateHandle`。原音频初始化还报告没有可用的 DirectSoundCreate；尚无原游戏缓冲区交换或游戏画面。

原程序经过网页构造路径，只能证明当前初始化链已经推进；完整网页加载和绘制由独立兼容层检查验证，不能据此声称原 MOTD、服务器浏览器或游戏菜单已经显示正常。

## 实现范围

- CLSID WebBrowser 的宿主工厂通过 CoCreateInstance 注册。每个支持的接口使用独立访客虚表；64 位 WebKit 指针只留在宿主。QueryInterface 维护 IUnknown 身份和引用计数，未知接口失败，聚合请求失败。
- 当前提供原构造路径需要的 IUnknown、IViewObject、IOleObject、IPersistStreamInit、IOleInPlaceObject、IWebBrowser2，以及单个 DWebBrowserEvents2 连接点。它们均为部分实现，未实现的方法保留明确停止入口。
- IPersistStreamInit::InitNew 发起真实 about:blank 加载。Navigate／Navigate2 支持当前已接入的 URL 形式，带额外请求参数的调用返回未实现；实际验证只覆盖本地文件。刷新、停止和历史操作交给 WKWebView。
- 由 WKNavigationDelegate 的真实完成事件触发 NavigateComplete2／DocumentComplete，先排队，再在访客消息泵里调用原事件接收者。Objective-C 回调不直接进入原代码。加载失败不发送成功完成通知。
- 系统网页使用独立屏幕外窗口作为合成后备，不覆盖原菜单。IViewObject::Draw 取得真实 WebKit 快照，再写入原选中的 GDI 位图；支持当前验证的内容视图与 16／32 位目标。打印目标、继续回调等尚未实现，相关请求失败。
- 通过真实 WebKit DOM 接通当前文档的 body 和 IHTMLTextContainer 的滚动位置／尺寸。初次文档尚未加载完成时，get_Document 返回失败与空指针。其余 MSHTML 方法明确停止。
- 网页存储为当前进程的临时存储，JavaScript 弹窗不另开原生模态窗口。此组件的存储与项目未来手机网页资源缓存是两件事。

接口槽位与 GUID 按 Microsoft SDK 声明核对。参考：[IWebBrowser2](https://learn.microsoft.com/en-us/windows/win32/api/exdisp/nn-exdisp-iwebbrowser2)、[IOleObject](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-ioleobject)、[IViewObject](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-iviewobject)。后端使用 Apple SDK 中 WKWebView 的导航、脚本和快照接口，见 [WKWebView](https://developer.apple.com/documentation/webkit/wkwebview)。没有提交 SDK 头文件或原游戏转换代码。

## 验证

```sh
.venv/bin/python tools/build_launcher.py --diagnostic
.venv/bin/python tools/run_launcher.py --probe-browser
.venv/bin/python tools/run_launcher.py
```

独立检查通过公共 CoCreateInstance 创建对象，验证接口身份、未知 IID 和聚合拒绝、生命周期、视图通知、连接点订阅／退订；随后让系统 WebKit 加载临时本地 HTML，核对实际 DOM 标题、滚动位置和加载通知，并调用 IViewObject::Draw 写入访客共享 DIB。

检查页面上半为红色、下半为蓝色，回读的两个像素分别是 `0x00ff0000` 和 `0x000000ff`；这也发现并修正了最初快照上下颠倒的问题。页面仅为独立图形检查，不是 CS 的游戏画面。检查最后释放连接点、通知接收者和 WebKit 对象，并清理临时文件。

## 仍需完成

- 原 HTML 面板的完整使用路径、输入和焦点转发、剪裁与遮挡、持续重绘及导航取消语义尚未验证。
- DOM 接口仅映射当前文档；跨导航保留旧文档对象的行为未实现，完整 MSHTML、脚本自动化、文档写入和持久化接口均未实现。
- 真正远程页面、重定向、HTTP 请求参数、失败回调和历史导航需要继续验证。
- WebKit 操作依赖 macOS 主线程；不能将此 Objective-C 后端直接编译到浏览器。
- 原程序现在进入下一处系统接口缺口，仍不可玩。精简结果与限制见 [webbrowser-startup.json](evidence/webbrowser-startup.json)。
