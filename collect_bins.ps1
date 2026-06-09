# collect_bins.ps1
# 将构建产物复制到 E:\bin_file
# 用法: .\collect_bins.ps1
$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot "build"
$DestDir     = "E:\bin_file"

$Files = @(
    @{ src = "bootloader\bootloader.bin";          dst = "bootloader.bin" },
    @{ src = "partition_table\partition-table.bin"; dst = "partition-table.bin" },
    @{ src = "smart_child_bed.bin";                dst = "smart_child_bed.bin" }
)

if (-not (Test-Path $DestDir)) {
    New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    Write-Host "[创建目录] $DestDir"
}

foreach ($f in $Files) {
    $src = Join-Path $BuildDir $f.src
    $dst = Join-Path $DestDir   $f.dst
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $dst -Force
        $size = (Get-Item $dst).Length
        Write-Host "[复制] $($f.dst)  ($size bytes)"
    } else {
        Write-Host "[跳过] $($f.dst)  (源文件不存在: $src)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "完成。目标目录: $DestDir"
Get-ChildItem $DestDir -Filter *.bin | Format-Table Name, Length, LastWriteTime
