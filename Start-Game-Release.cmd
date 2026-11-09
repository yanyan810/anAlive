@echo off
setlocal
set "GAME_EXE=%~dp0..\generated\outputs\Release\CG2_Setup.exe"
if not exist "%GAME_EXE%" (
    echo Release executable was not found. Build Release first.
    pause
    exit /b 1
)
pushd "%~dp0"
start "" "%GAME_EXE%" %*
popd
