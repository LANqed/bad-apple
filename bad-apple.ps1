$ErrorActionPreference = 'Stop'

# No video is downloaded. The frame script is an ASCII frame sequence.
$framesUrl = 'https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1'
$audioUrl = 'https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('bad-apple-' + [Guid]::NewGuid())
$audio = $null

try {
    if (-not (Get-Command 'curl.exe' -ErrorAction SilentlyContinue)) { throw '需要 curl.exe' }
    if (-not ((Get-Command 'mpv.exe' -ErrorAction SilentlyContinue) -or (Get-Command 'ffplay.exe' -ErrorAction SilentlyContinue))) { throw '需要 mpv.exe 或 ffplay.exe' }
    New-Item -ItemType Directory -Path $tmp | Out-Null
    & curl.exe -L --fail --silent --show-error $framesUrl -o (Join-Path $tmp 'frames.ps1')
    if ($LASTEXITCODE -ne 0) { throw '画面脚本下载失败' }
    & curl.exe -L --fail --silent --show-error $audioUrl -o (Join-Path $tmp 'bad-apple.mp3')
    if ($LASTEXITCODE -ne 0) { throw '音频下载失败' }

    $player = if (Get-Command 'mpv.exe' -ErrorAction SilentlyContinue) { 'mpv.exe' } else { 'ffplay.exe' }
    $arguments = if ($player -eq 'mpv.exe') { @('--no-video', '--really-quiet', (Join-Path $tmp 'bad-apple.mp3')) } else { @('-nodisp', '-autoexit', '-loglevel', 'quiet', (Join-Path $tmp 'bad-apple.mp3')) }
    $audio = Start-Process $player -ArgumentList $arguments -PassThru
    & pwsh.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tmp 'frames.ps1')
    if ($LASTEXITCODE -ne 0) { throw '画面播放失败' }
} finally {
    if ($audio -and -not $audio.HasExited) { Stop-Process -Id $audio.Id -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue }
}
