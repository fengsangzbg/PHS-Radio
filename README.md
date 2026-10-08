# PHS Radio新大地播放器

<img src="resources/app_logo.svg" width="96" alt="PHS Radio 玻璃台风 P 标志">

**实用且美观的 Frutiger Aero 播放器，支持免费收听酷狗平台提供的可播放音乐。目前仅开放酷狗平台。**

液态玻璃、水光、柔和发光与弹性回馈，让听歌界面也成为桌面的一部分。支持导入 **Wallpaper Engine 动态壁纸**，搭配自定义背景、主题色和遮暗程度，从复古到明亮，自由组合多种风格。

[下载 v0.2.3 正式版](https://github.com/fengsangzbg/PHS-Radio/releases/tag/v0.2.3) · [下载 Windows 64 位最新稳定版](https://github.com/fengsangzbg/PHS-Radio/releases/latest) · [查看发布记录](https://github.com/fengsangzbg/PHS-Radio/releases) · [反馈问题](https://github.com/fengsangzbg/PHS-Radio/issues)

推荐新用户下载 **v0.2.3 正式版完整包**：保留 beta 系列的界面与性能改进，已包含酷狗设备身份保存、账号切换与搜索请求修复，无需另外下载或覆盖补丁。

**v0.2.4-beta.1 为 Wallpaper Engine 兼容性测试版。** 修复打开主题窗口时误暂停壁纸连接的问题；Qt 捕获失败后自动尝试原生 Windows 捕获，并在主题面板保留连接状态与“复制连接信息”按钮。本机真实动态壁纸、高清缩放、暂停恢复和退出检查已通过；另一台电脑的“参数错误”仍需复测。测试版需要手动下载，软件的更新按钮仍使用正式版通道。

## 可以做什么

- 酷狗扫码登录，读取账号歌单和歌单歌曲，汇总全部音乐。
- 搜索酷狗在线曲库，或单独搜索自己的歌单；支持部分名称、拼音及已收录别名。
- 首页每日推荐、跟随电脑时区的玻璃时钟、日期与年份。
- 自动弹出的播放 Dock：顺序／随机播放、暂停、切歌、可拖动进度条、独立音乐音量／静音与固定按钮。
- 同步滚动歌词，以及带高清封面的旋转玻璃唱片。
- 左侧滑出歌单、独立玻璃歌曲卡片、悬停高光、弹性按钮与多颗蓝色播放流星。
- 导入图片、GIF、视频和 Wallpaper Engine 壁纸，自定义背景与配色，组合复古、明亮等风格。
- 壁纸保持静音，音乐独立播放；窗口隐藏或不活跃时减少背景开销。
- 跟随 Windows 默认音频设备切换，支持耳机与扬声器切换。
- 标题栏“更新”检查新版本，下载校验后安装并自动重启，保留登录及主题设置。

软件免费使用。音乐能否播放取决于酷狗接口返回的音源及账号权限；不同账号可能取得不同结果。第三方接口与官方 App 的返回不一定一致，取得音源不代表歌曲没有会员或地区限制。目前发布版只开放酷狗，网易云与 QQ 音乐留待后续接入。

## 安装与使用

1. 到 [v0.2.3 正式版发布页](https://github.com/fengsangzbg/PHS-Radio/releases/tag/v0.2.3) 下载 Windows x64 完整包；也可选择[最新稳定版](https://github.com/fengsangzbg/PHS-Radio/releases/latest)。
2. **完整解压**后双击 `PHSRadio.exe`。保留 DLL、Qt 插件、`runtime`、`services` 等全部文件；无需另装 Qt、Node.js 或 npm。
3. 使用手机酷狗 App 扫描软件中的二维码，确认登录。
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

壁纸与封面保留源图，按屏幕实际像素显示；实际清晰度、动态帧率仍取决于原始素材、Wallpaper Engine 设置与电脑性能。

场景与网页壁纸的源帧率使用 Wallpaper Engine 的全局设置：在 Wallpaper Engine 的“设置 → 性能”中修改并应用，会同时影响桌面和播放器中的该类背景。当前 Windows 捕获后端最高为 60 FPS，实际呈现还受屏幕刷新率与绘制开销影响；将 WE 设为 165 FPS 不代表播放器能输出 165 FPS。视频壁纸保持原视频帧率，调整 WE 的限帧不会插入额外视频帧。

Dock 音量只控制软件内的音乐，不改变 Windows 主音量；背景继续静音。音量和静音状态会在下次启动时恢复。

## 构建

需要 CMake 3.21+、C++17、Qt 6.6+（Widgets、Multimedia、Network）及 ICU。Windows 发布使用 MSYS2 UCRT64：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPHSRADIO_KUGOU_ONLY=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 -BuildDir build -Version 0.2.4-beta.1
```

本机服务源码与依赖锁位于 `services/kugou`，开发或重新打包前运行 `npm ci --prefix services/kugou --omit=dev --ignore-scripts`。服务只监听本机 `127.0.0.1:3737`。

`PHSRADIO_KUGOU_ONLY=ON` 生成仅开放酷狗的发布版。未开放平台的开发代码保留在仓库，测试不会使用真实账号。更多实现细节见 [开发说明](docs/development.md)。

当前开发构建与打包默认为 `0.2.4-beta.1`。已有构建目录需显式传入 `-DPHSRADIO_PRERELEASE=beta.1` 更新缓存。构建正式版时传入 `-DPHSRADIO_PRERELEASE=` 清空预发布后缀，并以相同的基础版本号打包。Beta 版的更新检查仍查询正式 Release，可升级到版本号更高的正式版本。

## 许可与依赖源码

PHS Radio 按 [GPL-3.0](LICENSE) 发布。第三方库与平台标识保留各自许可及权利归属。

发布版使用 Qt、GPL 版 FFmpeg、ICU、Node.js 和酷狗本机 API 服务。许可证、对应版本的依赖源码及构建配方随同版本 Release 提供；详见 [第三方说明](THIRD_PARTY.md)。
