@echo off
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
