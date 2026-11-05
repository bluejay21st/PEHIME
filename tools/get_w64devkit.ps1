# PEHIME 编译器下载/解压脚本
#
# 用法（在项目根目录或任意目录）：
#   powershell -ExecutionPolicy Bypass -File tools\get_w64devkit.ps1
#   powershell -ExecutionPolicy Bypass -File tools\get_w64devkit.ps1 -Force
#   powershell -ExecutionPolicy Bypass -File tools\get_w64devkit.ps1 -Version 2.11.0
#   powershell -ExecutionPolicy Bypass -File tools\get_w64devkit.ps1 -Url "file:///D:/w64devkit-x64-2.11.0.7z.exe"
#
# 默认下载 w64devkit 2.11.0（x64，约 66 MB），解压到 tools\w64devkit\w64devkit\。
# 目录最终结构：tools\w64devkit\w64devkit\bin\x86_64-w64-mingw32-gcc.exe

[CmdletBinding()]
param(
    [string]$Version = '2.11.0',
    [string]$Url = '',
    [string]$Destination = '',
    [string]$Sha256 = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $repo 'tools\w64devkit'
}

# 默认版本/默认地址时使用发布资产的内置 SHA256
$usingDefaultUrl = [string]::IsNullOrWhiteSpace($Url)
if ($usingDefaultUrl) {
    $Url = "https://github.com/skeeto/w64devkit/releases/download/v$Version/w64devkit-x64-$Version.7z.exe"
}
if ([string]::IsNullOrWhiteSpace($Sha256) -and $usingDefaultUrl -and $Version -eq '2.11.0') {
    $Sha256 = '54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b'
}

Write-Host "PEHIME toolchain bootstrap"
Write-Host "  version    : $Version"
Write-Host "  url        : $Url"
Write-Host "  destination: $Destination"

if (Test-Path $Destination) {
    if (-not $Force) {
        throw "目录已存在：$Destination`n如需重新下载，请加 -Force（会删除该目录）。"
    }
    Write-Host "  -Force: 删除旧目录 $Destination"
    Remove-Item $Destination -Recurse -Force
}

$archive = Join-Path $env:TEMP ("w64devkit-x64-$Version.7z.exe")
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Write-Host "下载中（约 66 MB）..."
    Invoke-WebRequest -Uri $Url -OutFile $archive -UseBasicParsing

    if (-not [string]::IsNullOrWhiteSpace($Sha256)) {
        Write-Host "校验 SHA256..."
        $actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $Sha256.ToLower()) {
            throw "SHA256 不匹配：`n  期望 $($Sha256.ToLower())`n  实际 $actual"
        }
    }

    Write-Host "解压中..."
    $p = Start-Process -FilePath $archive -ArgumentList @('-y', "-o$Destination") -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) {
        throw "解压失败，退出码 $($p.ExitCode)"
    }

    $gcc = Join-Path $Destination 'w64devkit\bin\x86_64-w64-mingw32-gcc.exe'
    if (-not (Test-Path $gcc)) {
        $gcc = Join-Path $Destination 'bin\x86_64-w64-mingw32-gcc.exe'
    }
    if (-not (Test-Path $gcc)) {
        throw "解压完成但找不到 gcc：$gcc"
    }

    Write-Host "OK:"
    & $gcc --version | Select-Object -First 1
    Write-Host "现在可以运行：build_words.cmd / build_x64.cmd / build_x86.cmd"
}
finally {
    if (Test-Path $archive) {
        Remove-Item $archive -Force -ErrorAction SilentlyContinue
    }
}
