# Workspace Instructions

- Project: PHS Radio（新大地播放器）, a C++17 / Qt 6 desktop music player built with CMake.
- Keep the UI labels in Simplified Chinese and follow the existing Qt Widgets style.
- Keep music-provider integrations behind `MusicProvider`. The user approved an opt-in local KuGouMusicApi bridge for personal experimentation; do not expose it beyond localhost or store account tokens in plaintext. Other providers remain mock until separately integrated.
- Build with CMake and Qt 6 Widgets + Multimedia; update README build instructions when dependencies or workflows change.

## Setup Status

- [x] Requirements clarified: C++ desktop app with search and QQ Music, NetEase Cloud Music, and Kugou playlist data.
- [x] Qt 6 / CMake starter scaffold and mock provider implemented.
- [x] README build instructions and project documentation added.
- [x] No extensions installed; the project setup did not require any.
- [x] Compile: configured and built successfully with the D-drive MSYS2 UCRT64 toolchain.
- [x] No VS Code task added; the README documents direct CMake configure/build commands.
- [ ] Launch: waiting for user confirmation before starting a debug session.
