@echo off
setlocal
set "CHECK_EXE=%~dp0..\generated\outputs\Release\CG2_Setup.exe"
if not exist "%CHECK_EXE%" (
    echo Release executable was not found. Build Release first.
    pause
    exit /b 1
)
pushd "%~dp0"
start "" "%CHECK_EXE%" --scene-transition-check
popd
