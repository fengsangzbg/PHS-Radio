# 第三方依赖与源码

PHS Radio 的项目源码按 GPL-3.0 发布；下列第三方项目保留其原始许可、版权声明及平台标识权利。

Windows 发行包包含 Qt、FFmpeg、ICU、MinGW 运行库、Node.js、酷狗本机服务和锁定的 npm 依赖。npm 服务的 JavaScript 源码及许可证包含在 `services/kugou` 中；其它许可原文位于软件包的 `licenses` 目录。

本次使用的 FFmpeg 为启用 GPL 组件的 MSYS2 构建，不能按纯 LGPL 版本描述。对应库版本、完整源码包、构建配方及 SHA256 清单在同版本 GitHub Release 的第三方源码附件中提供。应用构建步骤见 README；MSYS2 依赖构建步骤与包内 PKGBUILD、补丁和上游源码一同提供。

这些依赖采用动态链接，可使用兼容构建替换随包 DLL。软件不限制修改、调试、逆向工程或运行修改后的版本；没有设备锁定或安装密钥。

FFmpeg 的许可与分发说明见 [FFmpeg 官方说明](https://ffmpeg.org/legal.html)，Qt 的开源许可说明见 [Qt 官方说明](https://www.qt.io/development/open-source-lgpl-obligations)。

酷狗标识来自官网 favicon，仅用于显示歌曲来源，权利归原权利人所有。本软件是独立项目。Wallpaper Engine 及用户导入的壁纸不包含在发行包中，分别由其权利人和用户管理。
