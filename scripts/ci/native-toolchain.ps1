Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

<# .SYNOPSIS 校验快照目录版本，防止版本值成为目录穿越路径。 #>
function Assert-NativeToolchainVersion($Lock) {
    if ($Lock.msvc.toolset -notmatch '^14\.\d+\.\d+$' -or $Lock.msvc.sdk -notmatch '^10\.0\.\d+\.0$') {
        throw 'Invalid native toolchain version.'
    }
}

<# .SYNOPSIS 验证目录树不存在重解析点，避免复制或清理穿越链接。 #>
function Assert-NativeToolchainTree([string]$Root) {
    $item = Get-Item -LiteralPath $Root -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point: $Root" }
    foreach ($child in Get-ChildItem -LiteralPath $Root -Force -Recurse) {
        if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point: $($child.FullName)" }
    }
}

<# .SYNOPSIS 保存完整 MSVC 工具集及版本化 SDK 文件并返回归档摘要。 #>
function Save-NativeToolchain([string]$Toolsets, [string]$Sdk, $Lock, [string]$Archive) {
    Assert-NativeToolchainVersion $Lock
    Assert-NativeToolchainTree (Join-Path $Toolsets $Lock.msvc.toolset)
    foreach ($part in @('Include', 'Lib', 'bin')) { Assert-NativeToolchainTree (Join-Path $Sdk "$part/$($Lock.msvc.sdk)") }
    [void](New-Item -ItemType Directory -Path (Split-Path $Archive) -Force)
    & tar.exe -cf $Archive -C $Toolsets $Lock.msvc.toolset -C $Sdk `
        "Include/$($Lock.msvc.sdk)" "Lib/$($Lock.msvc.sdk)" "bin/$($Lock.msvc.sdk)"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot capture native toolchain snapshot.' }
    return (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
}

<# .SYNOPSIS 从已验证快照替换指定版本目录；拒绝越界和重解析目标。 #>
function Copy-NativeToolchainDirectory([string]$Source, [string]$Root, [string]$Relative) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $target = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $target.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Native toolchain target is outside its installation root.'
    }
    $ancestor = Split-Path $target
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if (((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse ancestor: $ancestor"
            }
        }
        $ancestor = Split-Path $ancestor
    }
    if (Test-Path -LiteralPath $target) {
        Assert-NativeToolchainTree $target
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    [void](New-Item -ItemType Directory -Path (Split-Path $target) -Force)
    Copy-Item -LiteralPath $Source -Destination $target -Recurse -Force
}

<# .SYNOPSIS 摘要验证后恢复精确 MSVC 和 SDK；完整校验解压目录后才替换安装目录。 #>
function Restore-NativeToolchain([string]$Toolsets, [string]$Sdk, $Lock, [string]$Archive, [string]$Digest) {
    Assert-NativeToolchainVersion $Lock
    if ($Digest -notmatch '^[a-f0-9]{64}$' -or
        (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Digest) {
        throw 'Native toolchain snapshot digest mismatch.'
    }
    $parent = [IO.Path]::GetFullPath((Split-Path $Archive))
    $staging = Join-Path $parent ('native-restore-' + [guid]::NewGuid())
    try {
        [void](New-Item -ItemType Directory -Path $staging)
        & tar.exe -xf $Archive -C $staging
        if ($LASTEXITCODE -ne 0) { throw 'Cannot extract native toolchain snapshot.' }
        Assert-NativeToolchainTree $staging
        foreach ($relative in @($Lock.msvc.toolset, "Include/$($Lock.msvc.sdk)", "Lib/$($Lock.msvc.sdk)", "bin/$($Lock.msvc.sdk)")) {
            if (-not (Test-Path -LiteralPath (Join-Path $staging $relative) -PathType Container)) { throw "Incomplete snapshot: $relative" }
        }
        Copy-NativeToolchainDirectory (Join-Path $staging $Lock.msvc.toolset) $Toolsets $Lock.msvc.toolset
        foreach ($part in @('Include', 'Lib', 'bin')) {
            $relative = "$part/$($Lock.msvc.sdk)"
            Copy-NativeToolchainDirectory (Join-Path $staging $relative) $Sdk $relative
        }
    } finally {
        if ([IO.Path]::GetFullPath($staging).StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
