# PHS Radio新大地播放器

<img src="resources/app_logo.svg" width="96" alt="PHS Radio 玻璃台风 P 标志">

**实用且美观的 Frutiger Aero 播放器。v0.4.0 正式版重塑 Dock 播放流星，支持酷狗与网易云独立扫码登录、歌单浏览与聚合搜索。**

液态玻璃、水光、柔和发光与弹性回馈，让听歌界面也成为桌面的一部分。支持导入 **Wallpaper Engine 动态壁纸**，自定义背景、主题色和遮暗程度，从复古到明亮自由搭配。

[下载 v0.4.0 正式版](https://github.com/fengsangzbg/PHS-Radio/releases/tag/v0.4.0) · [最新稳定版](https://github.com/fengsangzbg/PHS-Radio/releases/latest) · [反馈问题](https://github.com/fengsangzbg/PHS-Radio/issues)

酷狗与网易云均支持登录、歌单、搜索与播放。网易云播放已由用户实测确认恢复可用；具体歌曲的音源、音质与权限仍以平台返回为准。

v0.4.0 重做 Dock 播放流星。原先的绿色固定直线被替换为矢量绘制的玻璃光条：细长、带长尾、逐条错开掠过，冰蓝玻璃体配白色焦散芯与边缘色散；「火流星」保持原版固定光束不动。触发时机不变，仍然只在按下播放键后出现。

## 可以做什么

- 酷狗与网易云独立扫码登录，读取各自账号歌单和歌单歌曲，汇总全部音乐。
- 聚合搜索酷狗与网易云在线曲库，或单独搜索当前平台的自己的歌单；支持部分名称、拼音及已收录别名。
- 首页每日推荐、跟随电脑时区的玻璃时钟、日期与年份。
- 自动弹出的播放 Dock：顺序／随机播放、暂停、切歌、可拖动进度条、独立音乐音量／静音与固定按钮。「音质」菜单，可为酷狗、网易云分别选择标准、高品质、无损；切换时保留进度，实际音质以平台及账号权限为准。
- 同步滚动歌词，以及带高清封面的旋转玻璃唱片。
- 左侧滑出歌单、独立玻璃歌曲卡片、悬停高光、弹性按钮与固定闪现的分层播放激光。
- 导入图片、GIF、视频和 Wallpaper Engine 壁纸，自定义背景与配色，组合复古、明亮等风格；内置默认水光与「火流星」主题。
- 壁纸保持静音，音乐独立播放；窗口隐藏或不活跃时减少背景开销。
- 跟随 Windows 默认音频设备切换，支持耳机与扬声器切换。
- 标题栏“更新”检查新版本，下载校验后安装并自动重启，保留登录及主题设置。

软件免费使用。音乐能否播放取决于对应平台接口返回的音源及账号权限；不同账号可能取得不同结果。第三方接口与官方 App 的返回不一定一致，取得音源不代表歌曲没有会员或地区限制。当前支持酷狗和网易云账号，QQ 音乐留待后续接入。

## 安装与使用

1. 到 [v0.4.0 正式版发布页](https://github.com/fengsangzbg/PHS-Radio/releases/tag/v0.4.0) 下载 Windows x64 完整包；也可选择[最新稳定版](https://github.com/fengsangzbg/PHS-Radio/releases/latest)。
2. **完整解压**后双击 `PHSRadio.exe`。保留 DLL、Qt 插件、`runtime`、`services` 等全部文件；无需另装 Qt、Node.js 或 npm。
3. 启动时点击 MP3 上的 logo，在登录页选择酷狗或网易云并扫码；点击「完成登录」回到 MP3，再次点击 logo 进入播放器。进入后仍可通过“平台连接”管理账号，两平台可同时保持登录。
4. 歌单会在后台逐步加载。未加载完整时可从歌单面板重试；双击歌曲即可播放。

旧版用户先关闭播放器，再将新完整包解压到一个新文件夹，运行其中的 `PHSRadio.exe`；账号和主题设置仍保存在当前 Windows 用户下。无需再使用单独的设备验证测试补丁。

v0.2.3 会保存当前用户的酷狗设备身份，让登录、搜索和播放请求使用一致的设备信息；切换账号时清理旧设备状态并隔离过期响应。搜索直接请求在线曲库，不再先等待播放设备注册。7 项相关回归测试和便携包启动检查通过；用户反馈同一日本网络下的另一台电脑已恢复搜索与播放。其它设备的服务端拒绝仍需结合具体错误排查。

同时改进 Wallpaper Engine 窗口初始化：等待窗口尺寸稳定再开始捕获，捕获暂时失败时有限重连并保留错误说明；停止、切换壁纸或暂停会取消旧任务。静音、透明与高清显示保持原有设置。本机使用真实 Wallpaper Engine 的捕获恢复检查及相关自动测试已通过；另一台电脑报告的捕获“参数错误”仍待复测，不能据此保证所有壁纸在所有设备上均可用。

v0.2.1 首次升级需要手动下载新版完整包；v0.2.2 及之后的旧版和 0.2.3 beta 系列可通过稳定版通道升级至 v0.2.3 正式版：点击标题栏“更新”，有新版本时选择“下载并更新”。更新会正常退出播放器、替换应用文件并自动重启，保留账号和主题设置；下载或安装失败时保留或恢复旧版。请将便携包放在当前用户可写的文件夹。

当前更新按钮检查稳定版通道，v0.2.3 正式版通过该通道提供；beta 版本的更新按钮也查询稳定版通道，可升级至正式版，不会自动安装 beta。

账号凭据使用当前 Windows 用户的 DPAPI 加密保存。发布包不包含开发者账号、登录信息或导入的私人壁纸。

## Wallpaper Engine 与自定义风格

点击顶部“主题”导入背景，调整背景遮暗程度；Dock 的调色按钮可选择预设或自选颜色。使用不同壁纸与配色，便可搭配复古、明亮、水蓝、薄荷等视觉风格。

支持自动发现已安装的 Wallpaper Engine 与已下载项目。视频壁纸可直接循环播放；场景与网页壁纸使用本机 Wallpaper Engine 创建软件专属窗口并连续捕获动态画面，需要先安装并运行 Wallpaper Engine。原工程文件与桌面壁纸设置不会被修改，软件背景在加载前配置静音。Wallpaper Engine 本体和壁纸作品不随软件分发。

beta.2 的兼容路径由实际捕获结果触发：保留正常连接方式；遇到目标窗口参数错误时，尝试解除软件辅助窗口的归属，必要时在屏幕外完成捕获目标初始化，再恢复透明状态继续渲染。若仍无法连接，主题面板保留具体失败阶段，可点击“复制连接信息”用于反馈。不同 Windows 环境与壁纸类型仍需验证。

壁纸与封面保留源图，按屏幕实际像素显示；实际清晰度、动态帧率仍取决于原始素材、Wallpaper Engine 设置与电脑性能。

场景与网页壁纸的源帧率使用 Wallpaper Engine 的全局设置：在 Wallpaper Engine 的“设置 → 性能”中修改并应用，会同时影响桌面和播放器中的该类背景。当前 Windows 捕获后端最高为 60 FPS，实际呈现还受屏幕刷新率与绘制开销影响；将 WE 设为 165 FPS 不代表播放器能输出 165 FPS。视频壁纸保持原视频帧率，调整 WE 的限帧不会插入额外视频帧。

Dock 音量只控制软件内的音乐，不改变 Windows 主音量；背景继续静音。音量和静音状态会在下次启动时恢复。

## 构建

需要 CMake 3.21+、C++17、Qt 6.6+（Widgets、Multimedia、Network、OpenGL、OpenGLWidgets）及 ICU。Windows 发布使用 MSYS2 UCRT64：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPHSRADIO_KUGOU_ONLY=OFF
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 -BuildDir build -Version 0.4.0
```

本机服务源码与依赖锁位于 `services/kugou` 和 `services/netease`，开发或重新打包前分别运行 `npm ci --prefix services/kugou --omit=dev --ignore-scripts` 和 `npm ci --prefix services/netease --omit=dev --ignore-scripts`。服务只监听本机端口 3737 和 3738；完整包已包含服务和 Node.js。

`PHSRADIO_KUGOU_ONLY=OFF` 生成酷狗、网易云双平台预览版；`ON` 保留仅酷狗构建用于兼容验证。未开放平台的开发代码保留在仓库，测试不会使用真实账号。更多实现细节见 [开发说明](docs/development.md)。

当前开发构建与打包默认为 `0.4.0` 正式版，`PHSRADIO_PRERELEASE` 为空，`PHSRADIO_KUGOU_ONLY=OFF`。搬迁过的项目应使用新构建目录。Beta 版的更新检查也查询正式 Release。

## 许可与依赖源码

PHS Radio 按 [GPL-3.0](LICENSE) 发布。第三方库与平台标识保留各自许可及权利归属。

发布版使用 Qt、GPL 版 FFmpeg、ICU、Node.js 和酷狗本机 API 服务。许可证、对应版本的依赖源码及构建配方随同版本 Release 提供；详见 [第三方说明](THIRD_PARTY.md)。
