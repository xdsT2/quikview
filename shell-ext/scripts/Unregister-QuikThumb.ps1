# 反注册 QuikView 缩略图提供程序。对称清理 64/32 两个注册表视图；
# 只删除仍指向本产品 CLSID 的 ShellEx 项，别家 Handler 不动。
# 需管理员权限（写 HKLM）。仅交付脚本，本任务约定不实际执行。
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '需要管理员权限：请以管理员身份运行 PowerShell 后重试。'
}

# 与 Register-QuikThumb.ps1 / src/module.cpp 保持一致
$Clsid    = '{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}'
$Category = '{e357fccd-a995-4576-b01f-234630154e96}'
$Exts     = @('.png', '.jpg', '.jpeg', '.bmp', '.gif', '.webp', '.tif', '.tiff')

# 1) 扩展名 ShellEx：共享视图 + WOW6432Node 兜底对称清理（交办单硬约束 2）
foreach ($ext in $Exts) {
    foreach ($base in @('HKLM:\SOFTWARE\Classes', 'HKLM:\SOFTWARE\Classes\WOW6432Node')) {
        $key = "$base\$ext\ShellEx\$Category"
        if (Test-Path $key) {
            $val = [string](Get-Item -Path $key).GetValue('')
            if ($val -ieq $Clsid) {
                Remove-Item -Path $key -Force
            } else {
                Write-Warning "$key 的值是 '$val'，不是本产品 CLSID，保留不删。"
            }
        }
    }
}

# 2) CLSID 两个视图整体删除
foreach ($base in @('HKLM:\SOFTWARE\Classes\CLSID', 'HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID')) {
    $key = Join-Path $base $Clsid
    if (Test-Path $key) {
        Remove-Item -Path $key -Recurse -Force
    }
}

Write-Host '反注册完成。如缩略图仍显示旧内容，重启 explorer.exe 或清理图标缓存。'
