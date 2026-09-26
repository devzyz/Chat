Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native-toolchain.ps1')
$root = Join-Path ([IO.Path]::GetTempPath()) ('chat-native-test-' + [guid]::NewGuid())
try {
    $toolsets = Join-Path $root 'source/msvc'
    $sdk = Join-Path $root 'source/sdk'
    $targetTools = Join-Path $root 'target/msvc'
    $targetSdk = Join-Path $root 'target/sdk'
    $archive = Join-Path $root 'native.tar'
    $lock = [pscustomobject]@{ msvc = [pscustomobject]@{ toolset = '14.44.35207'; sdk = '10.0.26100.0' } }
    foreach ($file in @('14.44.35207/bin/Hostx64/x64/cl.exe', '14.44.35207/lib/x64/runtime.lib')) {
        $path = Join-Path $toolsets $file
        [void](New-Item -ItemType Directory -Path (Split-Path $path) -Force)
        [IO.File]::WriteAllText($path, 'weekly compiler')
    }
    foreach ($part in @('Include', 'Lib', 'bin')) {
        $path = Join-Path $sdk "$part/10.0.26100.0/fixture"
        [void](New-Item -ItemType Directory -Path (Split-Path $path) -Force)
        [IO.File]::WriteAllText($path, 'weekly SDK')
    }
    $digest = Save-NativeToolchain $toolsets $sdk $lock $archive
    $compiler = Join-Path $targetTools '14.44.35207/bin/Hostx64/x64/cl.exe'
    [void](New-Item -ItemType Directory -Path (Split-Path $compiler) -Force)
    [IO.File]::WriteAllText($compiler, 'new runner compiler')
    $obsolete = Join-Path $targetTools '14.44.35207/obsolete.lib'
    [IO.File]::WriteAllText($obsolete, 'must not survive')
    Restore-NativeToolchain $targetTools $targetSdk $lock $archive $digest
    if ([IO.File]::ReadAllText($compiler) -ne 'weekly compiler' -or (Test-Path $obsolete)) {
        throw 'Runner compiler drift was not replaced by the exact weekly snapshot.'
    }
    if ([IO.File]::ReadAllText((Join-Path $targetSdk 'Include/10.0.26100.0/fixture')) -ne 'weekly SDK') {
        throw 'SDK snapshot was not restored.'
    }
    [IO.File]::WriteAllText($compiler, 'preserve on integrity failure')
    try {
        Restore-NativeToolchain $targetTools $targetSdk $lock $archive ('0' * 64)
        throw 'Expected digest rejection.'
    } catch {
        if ($_.Exception.Message -notmatch 'digest mismatch') { throw }
    }
    if ([IO.File]::ReadAllText($compiler) -ne 'preserve on integrity failure') {
        throw 'Integrity failure changed the installation.'
    }
    $lock.msvc.toolset = '../outside'
    try {
        Restore-NativeToolchain $targetTools $targetSdk $lock $archive $digest
        throw 'Expected path rejection.'
    } catch {
        if ($_.Exception.Message -notmatch 'Invalid native toolchain version') { throw }
    }
    Write-Host 'Native toolchain snapshot regression: PASS'
} finally {
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
}
