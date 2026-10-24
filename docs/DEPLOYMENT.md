# GitHub 与 Vercel

源码仓库：https://github.com/tinypoint/cs16-native 。macOS 和 Web 源码在同一仓库，原游戏文件、生成 C 和生成二进制不提交。

Vercel 项目为 `cs16-web`，根目录为 `web/`。固定地址：https://cs16-native.vercel.app/ 。v0.0.4 为原程序离线地图开发测试，仍保持顶层 `ready=false`，仅开放 `preview`；iPhone Safari 尚未实机验证。

## 发布与证据

```sh
.venv/bin/python tools/prepare_web_preview.py --interactive --local-unverified
# 先启动 tools/serve_web_test.py，再运行 tools/check_web_gameplay.cjs
.venv/bin/python tools/prepare_web_preview.py --interactive
.venv/bin/python tools/refresh_web_auth.py
.venv/bin/python tools/publish_web_preview.py
.venv/bin/python tools/deploy_web.py --status
```

动作检查须验证原地图、人物、移动、视角和弹匣变化。准备工具核对程序、启动资源、地图分块、前端的实际哈希与测试证据，发布工具拒绝未经验证的暂存包。确认 Vercel 返回 READY 后，再检查公开页面、清单及资源响应头。

程序、启动资源和 19 个地图分块在 Vercel 同域托管。首次约 148 MiB；内容寻址上传复用已经上传的文件。只有清单引用的文件会进入部署，更新部署不要求重复上传未变化资源。

## 缓存和隔离

页面提供 HTTPS、COOP same-origin、COEP require-corp 和 CORP same-origin。入口与清单重新验证缓存；`artifact/` 与 `resources/` 使用一年不可变缓存和内容哈希路径。浏览器另用 CacheStorage 校验并保存下载内容，已验证刷新时没有重新请求程序或资源包。未注册 Service Worker。

凭据位于被忽略的 `.local/web-access/`，不进入命令参数或源码。Vercel 的 GitHub 集成尚未安装；源码 push 与直接发布分别执行，不能认为 push 会自动部署。旧 `deploy_web.py` 纯页面入口不应覆盖已验证的程序版本。
