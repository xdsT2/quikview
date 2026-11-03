# 注册 QuikView 壳扩展（缩略图提供程序 IThumbnailProvider + 上下文菜单
# IContextMenu）。需管理员权限（写 HKLM）。DLL 路径相对本脚本位置解析，
# 可用 -Dll64/-Dll32 覆盖。仅交付脚本，本任务约定不实际执行（不写注册表）。
[CmdletBinding()]
param(
    [string]$Dll64 = '',
    [string]$Dll32 = ''
)

$ErrorActionPreference = 'Stop'

# 交办单硬约束 2：注册需要管理员
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '需要管理员权限：请以管理员身份运行 PowerShell 后重试。'
}

# 与 src/module.cpp 的自注册常量保持一致（改动必须同步两处）
$Clsid    = '{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}'   # 缩略图提供程序
$CtxClsid = '{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}'   # 上下文菜单 Handler
# 缩略图 ShellEx 类别 GUID（= IID_IThumbnailProvider）。
# 注意：交办单阶段 2 写的 {8895b1c6-...} 是预览处理器（IPreviewHandler）类别，
# 缩略图应用 {e357fccd-...}，见 shell-ext/README.md 说明。
$Category = '{e357fccd-a995-4576-b01f-234630154e96}'
$Exts     = @('.png', '.jpg', '.jpeg', '.bmp', '.gif', '.webp', '.tif', '.tiff')

# DLL 路径相对化：默认 <仓库根>\build-shell64(32)\quikthumb.dll
# 仓库根 = 本脚本目录(shell-ext/scripts) 的上两级
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Dll64) { $Dll64 = Join-Path $repoRoot 'build-shell64\quikthumb.dll' }
if (-not $Dll32) { $Dll32 = Join-Path $repoRoot 'build-shell32\quikthumb.dll' }

if (-not (Test-Path $Dll64)) {
    throw "未找到 64 位 DLL：$Dll64 （请先构建，或用 -Dll64 指定路径）"
}
$Dll64 = (Resolve-Path $Dll64).Path
$has32 = Test-Path $Dll32
if ($has32) {
    $Dll32 = (Resolve-Path $Dll32).Path
} else {
    Write-Warning "未找到 32 位 DLL：$Dll32 —— 只注册 64 位视图（32 位进程看不到本 Handler，不影响 64 位资源管理器）。"
}

# 1) 文件扩展名 → 缩略图 Handler。
#    HKLM\Software\Classes 下的 .ext 键是 32/64 位共享视图，只写一份；
#    位数差异由 CLSID 的 InprocServer32 双视图解决。
foreach ($ext in $Exts) {
    $key = "HKLM:\SOFTWARE\Classes\$ext\ShellEx\$Category"
    New-Item -Path $key -Force | Out-Null
    Set-Item -Path $key -Value $Clsid
}

# 2) CLSID → InprocServer32：64 位视图 + 32 位视图（WOW6432Node），各指对应位数 DLL；
#    缩略图与上下文菜单两个 CLSID 共用同一个 DLL，都注册
$views = @(
    @{ Base = 'HKLM:\SOFTWARE\Classes\CLSID';             Dll = $Dll64 }
    @{ Base = 'HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID'; Dll = $Dll32 }
)
foreach ($view in $views) {
    if (-not $view.Dll) { continue }
    foreach ($c in @($Clsid, $CtxClsid)) {
        $clsidKey = Join-Path $view.Base $c
        New-Item -Path (Join-Path $clsidKey 'InprocServer32') -Force | Out-Null
        Set-Item -Path $clsidKey -Value 'QuikView Shell Extension'
        Set-Item -Path (Join-Path $clsidKey 'InprocServer32') -Value $view.Dll
        Set-ItemProperty -Path (Join-Path $clsidKey 'InprocServer32') `
            -Name 'ThreadingModel' -Value 'Apartment'
    }
}

# 3) 上下文菜单 Handler（阶段 1 IContextMenu）：
#    HKLM\Software\Classes\*\shellex\ContextMenuHandlers\QuikView = $CtxClsid。
#    `*`（全部文件类型）是字面键名，PowerShell 会把路径里的 * 当通配符，
#    必须用 -LiteralPath；64/32 双视图都写（32 位进程读 WOW6432Node）。
foreach ($base in @('HKLM:\SOFTWARE\Classes', 'HKLM:\SOFTWARE\Classes\WOW6432Node')) {
    $key = "$base\*\shellex\ContextMenuHandlers\QuikView"
    New-Item -LiteralPath $key -Force | Out-Null
    Set-Item -LiteralPath $key -Value $CtxClsid
}

Write-Host ''
Write-Host '注册完成：'
Write-Host "  64 位 DLL: $Dll64"
if ($has32) { Write-Host "  32 位 DLL: $Dll32" }
Write-Host "  扩展名: $($Exts -join ' ')"
Write-Host "  上下文菜单: *\shellex\ContextMenuHandlers\QuikView -> $CtxClsid"
Write-Host ''
Write-Host '生效方法：重启 explorer.exe，或先执行 ie4uinit.exe -show 再刷新。'
