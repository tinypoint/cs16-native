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

## v0.0.4 交付核对

部署 `dpl_bnUpGYNbW36gKL2sCjSaCpYnnvrH` 已返回 READY，固定地址已显示 v0.0.4。公开入口、脚本、样式和加载器与验证暂存文件逐字节一致；21 个程序／资源文件的大小、不可变缓存与 CORP 响应头检查通过，入口 COOP／COEP 正确。完整动作测试使用相同字节的本地部署，未重复从公网下载完整资源进行对局；iPhone Safari 仍待实机确认。见 [公开交付记录](evidence/web-offline-map-deployment.json)。

## v0.0.5 菜单修复交付

部署 `dpl_4j3b6KuuNyJAeaxH7q9jcB59n5Ls` 已返回 READY，固定地址确认 v0.0.5。公开前端与移动端操作验证使用的文件一致，21 个程序／资源文件的大小与缓存、隔离头检查通过。游戏资源分块沿用 v0.0.4 的内容哈希，只更新程序和前端。主菜单文字、菜单阶段触控和顶部诊断的变更见 [移动菜单修复](WEB_OFFLINE_MAP.md#v005-菜单与移动端点击修复)。
