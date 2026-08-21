# Bad Apple

在 Windows 或 Linux 终端中播放 Bad Apple ASCII 帧并同步播放 MP3。运行模式不需要 MP4；Linux 提供 `amd64`、`arm64` 和 `arm32/armhf` 原生二进制下载器，Windows 提供 x64 原生程序与纯 PowerShell/CMD 脚本。

## 音画同步

Windows 播放器（脚本和原生程序）用系统 MCI 播放 MP3，并以**音频时钟驱动画面**：每一帧的显示时刻由音频当前位置换算，视频始终跟随音乐，不会越跑越偏。每帧渲染前会按最大宽度补齐并清掉上一帧残留，避免画面出现遮挡残留。

## 环境要求

- Windows 脚本：`curl.exe`（Win10+ 自带）和 `powershell.exe`（系统自带），不需要 FFmpeg、MPV 或 PowerShell 7
- Windows 原生程序：仅依赖系统自带的 `curl.exe` 与 MCI
- Linux 二进制：`curl`，以及 `mpv` 或 `ffplay`；支持 `amd64`、`arm64`、`arm32/armhf`

脚本和二进制会把 ASCII 帧与 MP3 下载到临时目录，退出时删除，不会下载视频文件。

## 下载执行模式

如果不想使用 MP4，可以使用下面的脚本。它们只下载 ASCII 帧数据和 MP3，不下载视频文件。资源来自 jsDelivr，适合不能访问 `archive.org` 的网络环境。

Linux 自动识别 x64/arm64/arm32：

```bash
curl -fsSL https://raw.githubusercontent.com/LANqed/bad-apple/main/install.sh | bash
```

`install.sh` 会从 **GitHub Releases** 下载对应架构的二进制。如果报 `curl: (22) ... 404`，说明仓库还没有发布任何 Release（或 Release 缺少该架构资产）。解决办法：修改 `VERSION` 并推送到 `main`，等 GitHub Actions 自动发布；或者直接从源码编译，无需 Release：

```bash
curl -fsSL https://raw.githubusercontent.com/LANqed/bad-apple/main/src/bad_apple_linux.cpp -o /tmp/ba.cpp
g++ -std=c++17 -O2 /tmp/ba.cpp -o ~/.local/bin/bad-apple
~/.local/bin/bad-apple
```

国内 CDN：

```bash
BAD_APPLE_BASE_URL=https://github.com/LANqed/bad-apple/releases/latest/download \
  bash -c "$(curl -fsSL https://cdn.jsdelivr.net/gh/LANqed/bad-apple@main/install.sh)"
```

Windows PowerShell：

```powershell
irm https://raw.githubusercontent.com/LANqed/bad-apple/main/bad-apple.ps1 | iex
```

Windows CMD：

```cmd
curl.exe -fsSL https://raw.githubusercontent.com/LANqed/bad-apple/main/bad-apple.cmd -o "%TEMP%\bad-apple.cmd" && call "%TEMP%\bad-apple.cmd" & del "%TEMP%\bad-apple.cmd"
```

Windows 原生程序（下载即运行，同样使用 MCI 音频时钟同步）：

```cmd
curl.exe -fL https://github.com/LANqed/bad-apple/releases/latest/download/bad-apple-windows-amd64.exe -o "%TEMP%\bad-apple.exe" && "%TEMP%\bad-apple.exe"
```

### Windows RT 8/8.1（ARM32）

Windows RT 的 PowerShell 运行在 **Constrained Language Mode**（受限语言模式），该模式禁止 `Add-Type`/P-Invoke、任意 `New-Object` 和属性赋值，因此纯 PowerShell 无法在 RT 上播放音频。请在越狱后的 RT 上使用原生 ARM32 播放器：

```text
bad-apple-windows-arm32.exe
```

下载地址：

```text
https://github.com/LANqed/bad-apple/releases/latest/download/bad-apple-windows-arm32.exe
```

把 `bad-apple-windows-arm32.exe` 放到 `bad-apple-rt.ps1` 同目录后直接运行脚本，或在桌面上直接运行该 exe。`bad-apple-rt.ps1` 会自动检测受限模式：有原生播放器就用它，否则给出上述提示。只有在 Full Language Mode 下才会回退到 `bad-apple.ps1` 的 PowerShell 播放器。

版本号由根目录 `VERSION` 管理。修改版本号（例如从 `0.1.0` 改成 `0.2.0`）并推送到 `main` 后，`.github/workflows/release.yml` 会自动校验版本、构建三套 Linux 架构与 Windows x64/ARM32、创建 `v0.2.0` 标签与 GitHub Release，并上传：

```text
bad-apple-linux-amd64
bad-apple-linux-arm64
bad-apple-linux-arm32
bad-apple-windows-amd64.exe
bad-apple-windows-arm32.exe
install.sh
bad-apple.sh
bad-apple.ps1
bad-apple.cmd
bad-apple-rt.ps1
SHA256SUMS
```

已存在的版本标签或 Release 不会重复发布。也可以在 GitHub Actions 页面手动运行同一工作流；仍以 `VERSION` 文件为准。

ARM32 产物使用硬浮点 ABI `arm-linux-gnueabihf`，适用于常见的 `armv7l`/`armhf` Linux。ARMv6、软浮点 ABI 和无 MMU 系统不在这个产物的兼容范围内。

当前 Linux 脚本的本地运行方式：

```bash
bash ./install.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File .\bad-apple.ps1
```

```cmd
call bad-apple.cmd
```

### Windows

Windows 脚本和原生程序都不需要安装 FFmpeg、MPV 或 PowerShell 7：

- 脚本使用系统自带的 `powershell.exe`、`curl.exe` 和 MCI 播放 MP3
- 原生程序仅使用系统自带的 `curl.exe` 和 MCI

建议使用 Windows Terminal，以获得更好的终端渲染效果。

### Linux

音频播放依赖 `mpv` 或 `ffplay`，例如：

```bash
sudo apt update
sudo apt install ffmpeg        # 提供 ffplay
```

```bash
sudo dnf install ffmpeg
```

```bash
sudo pacman -S ffmpeg
```

`mpv` 和 `ffplay` 会按系统自动选择 PipeWire、PulseAudio 或 ALSA 输出。两者都没有时播放器降级为终端响铃（非原曲）。

## 运行

Windows（推荐先调大终端窗口）：

```powershell
powershell -ExecutionPolicy Bypass -File .\bad-apple.ps1
```

或使用原生程序：

```powershell
.\bad-apple-windows-amd64.exe
```

Linux：

```bash
bash ./install.sh
```

按 `Ctrl+C` 退出。画面和声音由同一个音频时钟驱动，自动维持音画同步；终端输出跟不上时画面会跳帧，但不会拖慢音乐。
