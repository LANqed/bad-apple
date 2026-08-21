# Windows RT 8.1 launcher: downloads the canonical player and runs it.
# Uses only Windows PowerShell 4.0, .NET WebClient and the system MCI layer.
$ErrorActionPreference = 'Stop'
$playerUrl = 'https://cdn.jsdelivr.net/gh/LANqed/bad-apple@main/bad-apple.ps1'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$player = Join-Path ([IO.Path]::GetTempPath()) ('bad-apple-player-' + [Guid]::NewGuid().ToString('N') + '.ps1')
$web = New-Object Net.WebClient
$web.DownloadFile($playerUrl, $player)
try {
    & $player
} finally {
    Remove-Item -LiteralPath $player -Force -ErrorAction SilentlyContinue
}
