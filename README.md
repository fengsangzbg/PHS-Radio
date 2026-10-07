# PHS Radio新大地播放器

<img src="resources/app_logo.svg" width="96" alt="PHS Radio 玻璃台风 P 标志">

**实用且美观的 Frutiger Aero 播放器，支持免费收听酷狗平台提供的可播放音乐。目前仅开放酷狗平台。**

液态玻璃、水光、柔和发光与弹性回馈，让听歌界面也成为桌面的一部分。支持导入 **Wallpaper Engine 动态壁纸**，搭配自定义背景、主题色和遮暗程度，从复古到明亮，自由组合多种风格。

[下载 Windows 64 位版本](https://github.com/fengsangzbg/PHS-Radio/releases/latest) · [查看发布记录](https://github.com/fengsangzbg/PHS-Radio/releases) · [反馈问题](https://github.com/fengsangzbg/PHS-Radio/issues)

## 可以做什么

- 酷狗扫码登录，读取账号歌单和歌单歌曲，汇总全部音乐。
- 搜索酷狗在线曲库，或单独搜索自己的歌单；支持部分名称、拼音及已收录别名。
- 首页每日推荐、跟随电脑时区的玻璃时钟、日期与年份。
- 自动弹出的播放 Dock：顺序／随机播放、暂停、切歌、可拖动进度条与固定按钮。
- 同步滚动歌词，以及带高清封面的旋转玻璃唱片。
- 左侧滑出歌单、独立玻璃歌曲卡片、悬停高光、弹性按钮与多颗蓝色播放流星。
- 导入图片、GIF、视频和 Wallpaper Engine 壁纸，自定义背景与配色，组合复古、明亮等风格。
- 壁纸保持静音，音乐独立播放；窗口隐藏或不活跃时减少背景开销。
- 跟随 Windows 默认音频设备切换，支持耳机与扬声器切换。

软件免费使用。音乐能否播放由酷狗返回的音源及账号权限决定，会员、付费、地区与下架限制仍适用。目前发布版只开放酷狗，网易云与 QQ 音乐留待后续接入。

## 安装与使用

1. 到 [Releases](https://github.com/fengsangzbg/PHS-Radio/releases/latest) 下载 `PHS-Radio-0.2.1-windows-x64.zip`。
2. **完整解压**后双击 `PHSRadio.exe`。保留 DLL、Qt 插件、`runtime`、`services` 等全部文件；无需另装 Qt、Node.js 或 npm。
3. 使用手机酷狗 App 扫描软件中的二维码，确认登录。
4. 歌单会在后台逐步加载。未加载完整时可从歌单面板重试；双击歌曲即可播放。

账号凭据使用当前 Windows 用户的 DPAPI 加密保存。发布包不包含开发者账号、登录信息或导入的私人壁纸。

## Wallpaper Engine 与自定义风格

点击顶部“主题”导入背景，调整背景遮暗程度；Dock 的调色按钮可选择预设或自选颜色。使用不同壁纸与配色，便可搭配复古、明亮、水蓝、薄荷等视觉风格。

支持自动发现已安装的 Wallpaper Engine 与已下载项目。视频壁纸可直接循环播放；场景与网页壁纸使用本机 Wallpaper Engine 创建软件专属窗口并连续捕获动态画面，需要先安装并运行 Wallpaper Engine。原工程文件与桌面壁纸设置不会被修改，软件背景在加载前配置静音。Wallpaper Engine 本体和壁纸作品不随软件分发。

壁纸与封面保留源图，按屏幕实际像素显示；实际清晰度、动态帧率仍取决于原始素材、Wallpaper Engine 设置与电脑性能。

## 构建

需要 CMake 3.21+、C++17、Qt 6.6+（Widgets、Multimedia、Network）及 ICU。Windows 发布使用 MSYS2 UCRT64：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPHSRADIO_KUGOU_ONLY=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 -BuildDir build -Version 0.2.1
```

本机服务源码与依赖锁位于 `services/kugou`，开发或重新打包前运行 `npm ci --prefix services/kugou --omit=dev --ignore-scripts`。服务只监听本机 `127.0.0.1:3737`。

`PHSRADIO_KUGOU_ONLY=ON` 生成仅开放酷狗的发布版。未开放平台的开发代码保留在仓库，测试不会使用真实账号。更多实现细节见 [开发说明](docs/development.md)。

## 许可与依赖源码

PHS Radio 按 [GPL-3.0](LICENSE) 发布。第三方库与平台标识保留各自许可及权利归属。

发布版使用 Qt、GPL 版 FFmpeg、ICU、Node.js 和酷狗本机 API 服务。许可证、对应版本的依赖源码及构建配方随同版本 Release 提供；详见 [第三方说明](THIRD_PARTY.md)。
