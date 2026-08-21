# Windows RT 8/8.1 entry point. Compatible with Windows PowerShell 4.0.
$ErrorActionPreference = 'Stop'

$framesUrl = 'https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1'
$audioUrl = 'https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3'
$tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ('bad-apple-rt-' + [Guid]::NewGuid().ToString())
$framesPath = Join-Path $tempDirectory 'frames.ps1'
$audioPath = Join-Path $tempDirectory 'bad-apple.mp3'
$mciAlias = 'badapple' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$mciOpened = $false

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class BadAppleMci
{
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
    public static extern int mciSendString(string command, StringBuilder output, int outputLength, IntPtr callback);
}
'@

function Invoke-MciCommand {
    param([string]$Command)

    $result = [BadAppleMci]::mciSendString($Command, $null, 0, [IntPtr]::Zero)
    if ($result -ne 0) {
        throw "MCI 音频命令失败，错误码: $result"
    }
}

try {
    # jsDelivr supports TLS 1.2; older Windows PowerShell otherwise defaults to TLS 1.0.
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    New-Item -ItemType Directory -Path $tempDirectory | Out-Null
    $webClient = New-Object Net.WebClient
    $webClient.DownloadFile($framesUrl, $framesPath)
    $webClient.DownloadFile($audioUrl, $audioPath)

    $escapedAudioPath = $audioPath.Replace('"', '""')
    Invoke-MciCommand ('open "{0}" type mpegvideo alias {1}' -f $escapedAudioPath, $mciAlias)
    $mciOpened = $true
    Invoke-MciCommand ('play {0}' -f $mciAlias)

    # The downloaded file contains precomputed ASCII frames and its own timing loop.
    & $framesPath
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "ASCII 帧播放失败，退出码: $LASTEXITCODE"
    }
} finally {
    if ($mciOpened) {
        [void][BadAppleMci]::mciSendString(('stop {0}' -f $mciAlias), $null, 0, [IntPtr]::Zero)
        [void][BadAppleMci]::mciSendString(('close {0}' -f $mciAlias), $null, 0, [IntPtr]::Zero)
    }
    if (Test-Path -LiteralPath $tempDirectory) {
        Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}
