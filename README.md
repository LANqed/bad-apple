# Bad Apple

在 Windows 或 Linux 终端中播放 Bad Apple ASCII 帧并同步播放 MP3。运行模式不需要 MP4；Linux 提供 `amd64`、`arm64` 和 `arm32/armhf` 原生二进制下载器。

## 环境要求

- Windows 脚本：PowerShell 7、`curl.exe`，以及 `mpv` 或 `ffplay`
- Linux 二进制：`curl`，以及 `mpv` 或 `ffplay`；支持 `amd64`、`arm64`、`arm32/armhf`

脚本和二进制会把 ASCII 帧与 MP3 下载到临时目录，退出时删除，不会下载视频文件。

## 下载执行模式

如果不想使用 MP4，可以使用下面的脚本。它们只下载 ASCII 帧数据和 MP3，不下载视频文件。资源来自 jsDelivr，适合不能访问 `archive.org` 的网络环境。

Linux 自动识别 x64/arm64/arm32：

```bash
curl -fsSL https://raw.githubusercontent.com/LANqed/bad-apple/main/install.sh | bash
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

### Windows RT 8/8.1（ARM32）

`bad-apple-rt.ps1` 只使用 Windows PowerShell 4.0、`.NET WebClient` 和系统 `winmm.dll` 的 MCI MP3 播放能力；不需要 `curl.exe`、PowerShell 7、FFmpeg、MPV 或第三方 ARM32 EXE。设备已越狱时可以直接使用此入口：

```powershell
powershell -ExecutionPolicy Bypass -Command "iex ((New-Object Net.WebClient).DownloadString('https://cdn.jsdelivr.net/gh/LANqed/bad-apple@main/bad-apple-rt.ps1'))"
```

本地运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\bad-apple-rt.ps1
```

该入口从 jsDelivr 下载 ASCII 帧和 MP3 到临时目录，用 MCI 同步播放音频，结束或中断时关闭音频并删除临时文件。若 Windows RT 映像缺少 MCI 的 `mpegvideo` 编解码支持，脚本会报出 MCI 错误码，此时需要改用 WAV 版本或原生桌面播放器。

上面的地址需要替换为实际托管仓库。创建 `v*` 标签后，`.github/workflows/release.yml` 会构建并发布：

```text
bad-apple-linux-amd64
bad-apple-linux-arm64
bad-apple-linux-arm32
```

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

使用 winget 安装 FFmpeg：

```powershell
winget install Gyan.FFmpeg
```

关闭并重新打开终端，然后确认命令可用：

```powershell
ffmpeg -version
ffplay -version
```

建议使用 Windows Terminal 或新版 PowerShell，以正确显示 Unicode 半块字符。

### Linux

Debian/Ubuntu：

```bash
sudo apt update
sudo apt install ffmpeg python3
```

Fedora：

```bash
sudo dnf install ffmpeg python3
```

Arch Linux：

```bash
sudo pacman -S ffmpeg python
```

## 运行

Windows：

```powershell
python .\bad_apple.py .\bad_apple.mp4
```

Linux：

```bash
python3 ./bad_apple.py ./bad_apple.mp4
```

按 `Ctrl+C` 退出。播放器默认占满当前终端，并自动维持音画同步。

常用选项：

```text
--fps 24             降低帧率，减少 CPU 占用
--width 100          固定画面字符宽度
--height 30          固定画面字符高度
--threshold 160      调整黑白分界值
--invert             反转黑白
--mute               不播放声音
```

例如：

```bash
python3 bad_apple.py bad_apple.mp4 --fps 24 --width 100 --height 30
```

终端输出跟不上时，程序会跳过迟到的画面，而不是拖慢音乐。减小 `--width`、`--height` 或 `--fps` 可以降低负载。
