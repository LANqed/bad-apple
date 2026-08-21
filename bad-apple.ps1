# Bad Apple terminal player for Windows (PowerShell 5.1+ / Windows RT 8.1).
# Self-contained: downloads ASCII frames + MP3, plays MP3 through the built-in
# MCI layer and renders frames locked to the audio clock so video never drifts.
# If MCI cannot open the MP3, it falls back to mpv/ffplay with a calibration
# offset so audio and video still start together.
$ErrorActionPreference = 'Stop'

$framesUrl = 'https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1'
$audioUrl = 'https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3'
$audioSeconds = 219.09

$tmpDir = Join-Path ([IO.Path]::GetTempPath()) ('bad-apple-' + [Guid]::NewGuid().ToString('N'))
$framesFile = Join-Path $tmpDir 'frames.ps1'
$audioFile = Join-Path $tmpDir 'audio.mp3'
$openDelim = [string][char]64 + [char]34
$closeDelim = [string][char]34 + [char]64
$mciAlias = 'ba' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$mciOpen = $false
$audioProcess = $null
$cursorVisible = $null

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class BadAppleMci {
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)]
    public static extern int mciSendString(string command, StringBuilder output, int outputLength, IntPtr callback);
}
'@

function Send-Mci {
    param([string]$Command)
    $sb = New-Object System.Text.StringBuilder 256
    $code = [BadAppleMci]::mciSendString($Command, $sb, $sb.Capacity, [IntPtr]::Zero)
    if ($code -ne 0) { throw ('MCI error {0}: {1}' -f $code, $Command) }
    return $sb.ToString()
}

function Start-NoWindowProcess {
    param([string]$Exe, [string[]]$Arguments)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $argString = ($Arguments | ForEach-Object {
        if ($_ -match '\s') { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
    }) -join ' '
    $psi.Arguments = $argString
    return [System.Diagnostics.Process]::Start($psi)
}

function Download-File {
    param([string]$Url, [string]$Path)
    if (Get-Command 'curl.exe' -ErrorAction SilentlyContinue) {
        & curl.exe -L --fail --silent --show-error --ssl-no-revoke --max-time 180 $Url -o $Path
        return ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $Path))
    }
    try {
        $web = New-Object Net.WebClient
        $web.DownloadFile($Url, $Path)
        return (Test-Path -LiteralPath $Path)
    } catch {
        return $false
    }
}

function Reset-Frame {
    param([string]$Text, [int]$Width, [int]$Rows)
    $sb = New-Object System.Text.StringBuilder
    $parts = $Text -split "`n"
    foreach ($line in $parts) {
        [void]$sb.Append($line)
        if ($line.Length -lt $Width) { [void]$sb.Append(' ' * ($Width - $line.Length)) }
        [void]$sb.Append("`n")
    }
    for ($i = $parts.Count; $i -lt $Rows; $i++) {
        [void]$sb.Append(' ' * $Width)
        [void]$sb.Append("`n")
    }
    try {
        [Console]::SetCursorPosition(0, 0)
    } catch {
        [void]$sb.Insert(0, "$([char]27)[H")
    }
    [Console]::Write($sb.ToString())
}

function Write-FrameAt {
    param([string]$Text, [int]$Width, [int]$Rows, [int]$Index, [ref]$Last)
    if ($Index -eq $Last.Value) { return }
    $Last.Value = $Index
    Reset-Frame $Text $Width $Rows
}

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    New-Item -ItemType Directory -Path $tmpDir | Out-Null
    [Console]::Write('Downloading ASCII frames... ')
    if (-not (Download-File $framesUrl $framesFile)) { throw 'Failed to download frames.' }
    [Console]::WriteLine('done.')
    [Console]::Write('Downloading audio... ')
    if (-not (Download-File $audioUrl $audioFile)) { throw 'Failed to download audio.' }
    [Console]::WriteLine('done.')

    $allLines = [IO.File]::ReadAllLines($framesFile)
    $frames = New-Object System.Collections.Generic.List[string]
    $capturing = $false
    $buffer = New-Object System.Collections.Generic.List[string]
    foreach ($line in $allLines) {
        if ($capturing) {
            if ($line -eq $closeDelim) {
                $frames.Add(($buffer -join "`n"))
                $buffer.Clear()
                $capturing = $false
            } else {
                $buffer.Add($line)
            }
        } elseif ($line -eq $openDelim) {
            $capturing = $true
        }
    }
    if ($frames.Count -eq 0) { throw 'No frames found in downloaded data.' }

    $maxWidth = 0
    $maxRows = 0
    foreach ($frame in $frames) {
        $lineLengths = @($frame -split "`n" | ForEach-Object { $_.Length })
        foreach ($len in $lineLengths) { if ($len -gt $maxWidth) { $maxWidth = $len } }
        if ($lineLengths.Count -gt $maxRows) { $maxRows = $lineLengths.Count }
    }
    if ($maxWidth -le 0 -or $maxRows -le 0) { throw 'Invalid frame data.' }

    try {
        $cursorVisible = [Console]::CursorVisible
        [Console]::CursorVisible = $false
    } catch {
        $cursorVisible = $null
    }

    $useMci = $true
    $fps = 12.0
    try {
        $escapedAudio = $audioFile.Replace('"', '""')
        Send-Mci ('open "{0}" type mpegvideo alias {1}' -f $escapedAudio, $mciAlias)
        $mciOpen = $true
        Send-Mci ('play {0}' -f $mciAlias)
        try {
            $durationMs = [int](Send-Mci ('status {0} length' -f $mciAlias))
            if ($durationMs -gt 0) {
                $computed = $frames.Count / ($durationMs / 1000.0)
                if ($computed -ge 1 -and $computed -le 60) { $fps = $computed }
            }
        } catch {
            $fps = 12.0
        }
    } catch {
        $useMci = $false
    }

    if (-not $useMci) {
        if (Get-Command 'mpv.exe' -ErrorAction SilentlyContinue) {
            $audioProcess = Start-NoWindowProcess 'mpv.exe' @('--no-video', '--really-quiet', $audioFile)
        } elseif (Get-Command 'ffplay.exe' -ErrorAction SilentlyContinue) {
            $audioProcess = Start-NoWindowProcess 'ffplay.exe' @('-nodisp', '-autoexit', '-loglevel', 'quiet', $audioFile)
        } else {
            throw 'MCI unavailable and no mpv/ffplay found.'
        }
        $fps = $frames.Count / $audioSeconds
        Start-Sleep -Milliseconds 600
    }

    $lastIndex = -1
    if ($useMci) {
        while ($true) {
            $position = 0
            $mode = 'playing'
            try {
                $position = [int](Send-Mci ('status {0} position' -f $mciAlias))
                $mode = Send-Mci ('status {0} mode' -f $mciAlias)
            } catch {
                break
            }
            $index = [int][Math]::Floor($position / 1000.0 * $fps)
            if ($index -lt 0) { $index = 0 }
            if ($index -ge $frames.Count) { break }
            Write-FrameAt $frames[$index] $maxWidth $maxRows $index ([ref]$lastIndex)
            if ($mode -eq 'stopped' -or $mode -eq 'not ready') { break }
            Start-Sleep -Milliseconds 40
        }
    } else {
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            if ($audioProcess.HasExited) { break }
            $index = [int][Math]::Floor($stopwatch.Elapsed.TotalSeconds * $fps)
            if ($index -lt 0) { $index = 0 }
            if ($index -ge $frames.Count) { break }
            Write-FrameAt $frames[$index] $maxWidth $maxRows $index ([ref]$lastIndex)
            Start-Sleep -Milliseconds 40
        }
    }
} finally {
    if ($mciOpen) {
        [void][BadAppleMci]::mciSendString(('stop {0}' -f $mciAlias), $null, 0, [IntPtr]::Zero)
        [void][BadAppleMci]::mciSendString(('close {0}' -f $mciAlias), $null, 0, [IntPtr]::Zero)
    }
    if ($audioProcess -and -not $audioProcess.HasExited) {
        Stop-Process -Id $audioProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($cursorVisible -ne $null) { try { [Console]::CursorVisible = $cursorVisible } catch {} }
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
