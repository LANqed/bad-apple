@echo off
setlocal EnableExtensions

set "FRAMES_URL=https://cdn.jsdelivr.net/gh/Toni4819/BadApple-ASCII-Terminal@main/BadApple_ASCII.ps1"
set "AUDIO_URL=https://cdn.jsdelivr.net/gh/EmirXK/bad_apple@master/bad_apple.mp3"
set "TMP_DIR=%TEMP%\bad-apple-%RANDOM%-%RANDOM%"
set "AUDIO_PID_FILE=%TMP_DIR%\audio.pid"

where curl.exe >nul 2>&1 || (echo 需要 curl.exe & exit /b 1)
where pwsh.exe >nul 2>&1 || (echo 需要 PowerShell 7 pwsh.exe & exit /b 1)
where mpv.exe >nul 2>&1 || where ffplay.exe >nul 2>&1 || (echo 需要 mpv.exe 或 ffplay.exe & exit /b 1)
mkdir "%TMP_DIR%" >nul 2>&1 || exit /b 1

curl.exe -L --fail --silent --show-error "%FRAMES_URL%" -o "%TMP_DIR%\frames.ps1"
if errorlevel 1 goto :cleanup
curl.exe -L --fail --silent --show-error "%AUDIO_URL%" -o "%TMP_DIR%\bad-apple.mp3"
if errorlevel 1 goto :cleanup

for /f %%P in ('pwsh.exe -NoProfile -Command "$p=if (Get-Command mpv.exe -ErrorAction SilentlyContinue) { Start-Process mpv.exe -ArgumentList '--no-video','--really-quiet','%TMP_DIR%\bad-apple.mp3' -PassThru } else { Start-Process ffplay.exe -ArgumentList '-nodisp','-autoexit','-loglevel','quiet','%TMP_DIR%\bad-apple.mp3' -PassThru }; $p.Id"') do echo %%P > "%AUDIO_PID_FILE%"
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%TMP_DIR%\frames.ps1"

:cleanup
if exist "%AUDIO_PID_FILE%" for /f %%P in (%AUDIO_PID_FILE%) do taskkill /PID %%P /T /F >nul 2>&1
if exist "%TMP_DIR%" rmdir /s /q "%TMP_DIR%"
endlocal
