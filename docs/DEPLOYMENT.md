# GitHub 与 Vercel · 2026-10-23

源码仓库：https://github.com/tinypoint/cs16-native 。macOS 和 Web 源码在同一仓库，游戏原文件与生成代码不提交。

Vercel 项目为 cs16-web，根目录设置为 web/。固定地址：https://cs16-native.vercel.app/ 。当前为入口 v0.0.1，manifest.ready=false；完整游戏没有接入，不把入口成功发布当成游戏可玩。

## 发布

凭据在 .local/web-access/，被 Git 忽略。部署脚本读取 vercel-project.json 与 vercel-auth.json，或通过 CS16_VERCEL_AUTH 指定已有 auth JSON 的路径。凭据内容不写入命令参数、源码或构建输出。

```sh
.venv/bin/python tools/deploy_web.py
.venv/bin/python tools/deploy_web.py --status
```

先确认返回 READY，再核对公开 HTML、manifest 版本和响应头。当前脚本只上传 web/；拒绝原始 EXE、DLL、资源格式及超过 1 MiB 的单文件。后续大程序／资源交付需单独接入内容寻址的存储与缓存。

Vercel 的 Git 仓库连接 API 当前返回需要安装 GitHub 集成，因此未建立自动部署连接。源码 push 与网页直接发布分别执行；不能声称每次 push 已自动上线。当前部署方式无需用户在手机安装集成。

## 浏览器基础

页面提供 HTTPS、COOP same-origin、COEP require-corp、CORP same-origin。入口与清单需重新验证缓存；未来 artifact/ 与 resources/ 路径为一年不可变缓存，只允许使用内容哈希路径。没有注册 Service Worker，也没有自动刷新循环。

尚未上传任何 CS 游戏资源到 Vercel。手机无需重新下载／上传原安装包。原程序、Web 图形与输入运行路径具备后，再发布可玩版本并更新清单和版本号。
