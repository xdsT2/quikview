@echo off
chcp 65001 >nul
REM ============================================================================
REM 已作废（2026-11-20）：本脚本指向旧的 build\libquikview_shell.dll，
REM 且写入的 CLSID 与正式 quikthumb.dll 同一组，误执行会覆盖正式注册，
REM 导致缩略图/右键菜单错乱。因此本脚本只拒绝执行，不写任何注册表。
REM
REM 正确入口（64+32 位双视图，管理员 PowerShell）：
REM   shell-ext\scripts\Register-QuikThumb.ps1   / Unregister-QuikThumb.ps1
REM   说明见 shell-ext\README.md「注册 / 反注册（管理员 PowerShell）」
REM ============================================================================
echo [QuikView] shell\register.bat 已作废（2026-11-20），未执行任何注册表写入。
echo 请改用正式注册流程（管理员 PowerShell）：
echo   powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Register-QuikThumb.ps1
echo   powershell -ExecutionPolicy Bypass -File shell-ext\scripts\Unregister-QuikThumb.ps1
echo 详见 shell-ext\README.md 的「注册 / 反注册」一节。
exit /b 1

REM ============================================================================
REM 以下为历史内容（2026-11 之前），保留仅作参考，永不执行。
REM ============================================================================
REM 注册/注销 QuikView 缩略图扩展（需管理员权限）
REM 用法: register.bat [unregister]

set DLL=%~dp0build\libquikview_shell.dll

if "%1"=="unregister" (
    echo 正在注销 QuikView 缩略图扩展...
    regsvr32 /u /s "%DLL%"
) else (
    echo 正在注册 QuikView 缩略图扩展...
    regsvr32 /s "%DLL%"
)
echo 完成。若资源管理器未刷新，请重启 explorer.exe 或注销重登录。