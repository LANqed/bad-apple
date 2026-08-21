# Windows RT 8.1 launcher.
# Windows RT runs PowerShell in Constrained Language Mode, which forbids
# Add-Type/P-Invoke, arbitrary New-Object and property setters, so a pure
# PowerShell player cannot play audio there. Prefer the native ARM32 player
# (jailbroken RT can run unsigned ARM32 desktop binaries). Falls back to the
# PowerShell player only when running in Full Language Mode.
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

function Test-ConstrainedLanguage {
    try {
        return $ExecutionContext.SessionState.LanguageMode -eq 'ConstrainedLanguage'
    } catch {
        return $false
    }
}

$native = $null
foreach ($candidate in @(
    (Join-Path $scriptDir 'bad-apple-windows-arm32.exe'),
    (Join-Path $env:TEMP 'bad-apple-windows-arm32.exe')
)) {
    if (Test-Path -LiteralPath $candidate) {
        $native = $candidate
        break
    }
}
if ($native) {
    & $native
    exit $LASTEXITCODE
}

if (Test-ConstrainedLanguage) {
    Write-Host ''
    Write-Host 'Windows RT runs PowerShell in Constrained Language Mode, so this'
    Write-Host 'script cannot play audio by itself in that mode.'
    Write-Host ''
    Write-Host 'Download the native ARM32 player and put it next to this script:'
    Write-Host '  bad-apple-windows-arm32.exe'
    Write-Host '  https://github.com/LANqed/bad-apple/releases/latest/download/bad-apple-windows-arm32.exe'
    Write-Host ''
    Write-Host 'Then run this script again, or run the exe directly.'
    exit 1
}

$local = Join-Path $scriptDir 'bad-apple.ps1'
if (Test-Path -LiteralPath $local) {
    & $local
    exit $LASTEXITCODE
}

$playerUrl = 'https://cdn.jsdelivr.net/gh/LANqed/bad-apple@main/bad-apple.ps1'
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
} catch {
    # Keep the default protocol if the setter is unavailable.
}
$player = Join-Path ([IO.Path]::GetTempPath()) ('bad-apple-player-' + [Guid]::NewGuid().ToString('N') + '.ps1')
$web = New-Object Net.WebClient
$web.DownloadFile($playerUrl, $player)
try {
    & $player
} finally {
    Remove-Item -LiteralPath $player -Force -ErrorAction SilentlyContinue
}
