# 开发与构建说明

公开发行版 v0.2.1 只开放酷狗，使用 PHSRADIO_KUGOU_ONLY=ON 构建。关闭该选项会启用未开放平台的开发代码；这不代表发布版已支持这些平台。

## 模块

- main.cpp 管理窗口、页面、账号、搜索与播放控制。
- MusicProvider 隔离平台接口；酷狗客户端处理扫码登录、歌单、分页、曲库搜索、推荐、音源与歌词。
- MusicSearch 管理文本、别名、简繁体与拼音索引；PlaybackQueue 管理顺序、随机与历史队列。
- AeroWidgets 负责玻璃外观、Dock、滑出歌单、卡片、时钟、唱片及流星动画。
- AeroSurface 按屏幕实际像素绘制背景并共享模糊纹理；WallpaperEngineCapture 只创建、捕获和关闭软件自己的动态壁纸窗口。

## Windows 工具链

发行构建使用 MSYS2 UCRT64、Qt 6.11.2、FFmpeg 9.0.2、ICU 78 和 Node.js 24.21.0；所需 DLL 由打包脚本检查并收集。

使用同一工具链的 C++17 编译器、Qt 与 ICU，按 README 构建。运行 npm ci --prefix services/kugou --omit=dev --ignore-scripts 安装锁定依赖。测试使用本地模拟数据和静音媒体，不需要真实平台账号；目前共22项。

PHSRADIO_APP_OUTPUT_DIR 可设置独立 EXE 输出目录，以免预览窗口锁住需要重新链接的文件。打包时可传 -ExePath 明确选择发布EXE，脚本拒绝旧版本和未启用酷狗门控的配置。

## 便携包

打包工具只收集应用、运行库、锁定生产依赖和许可文档，不复制用户设置、运行缓存、调试日志、私人壁纸、音乐或开发构建目录。必须完整解压；不能只分发 EXE。

## 对应依赖源码

同版本 Release 提供第三方源码附件，包含与实际 MSYS2 包版本匹配的完整源码、PKGBUILD、补丁和 SHA256 清单。应用源码由同版本Git标签提供。可用兼容动态库替换随包 DLL；未设置安装锁定或签名校验限制。