# Windows build-script regression tests; no Qt, compiler or CMake invocation required.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$script = Join-Path (Split-Path $PSScriptRoot -Parent) "build.ps1"
$temp = Join-Path ([IO.Path]::GetTempPath()) ("fugue-build-test-" + [guid]::NewGuid())
$lock = $null
try {
    $qt = Join-Path $temp "qt"
    $deps = Join-Path $temp "deps"
    $build = Join-Path $temp "build"
    $bin = Join-Path $build "release/app"
    New-Item -ItemType Directory -Force "$qt/bin", "$deps/bin", $bin | Out-Null
    [IO.File]::WriteAllText("$qt/bin/windeployqt.exe", "unused")
    $config = Join-Path $temp "build.local.psd1"
    [IO.File]::WriteAllText($config, "@{ QtDir = '$qt'; DependencyRoot = '$deps'; BuildRoot = '$build' }")
    $exe = Join-Path $bin "fugue-qt.exe"
    [IO.File]::WriteAllText($exe, "locked executable")
    $cache = Join-Path $build "release/CMakeCache.txt"
    [IO.File]::WriteAllText($cache, "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022")
    $failure = ""
    try { & $script Release -ConfigFile $config }
    catch { $failure = $_.Exception.Message }
    if ($failure -notlike "Build tree uses another generator;*") { throw "Unexpected failure: $failure" }
    if (!(Test-Path $exe)) { throw "Existing generator's executable was removed" }
    Remove-Item $cache
    $lock = [IO.File]::Open($exe, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $failure = ""
    try { & $script Release -ConfigFile $config }
    catch { $failure = $_.Exception.Message }
    if ($failure -notlike "Cannot delete *") { throw "Expected locked-executable failure with local config, got: $failure" }
    if (!(Test-Path $exe)) { throw "Locked executable was removed" }
    $lock.Dispose()
    $lock = $null

    # A helper alone must also stop the build, even if the main executable was removed.
    Remove-Item $exe
    $helper = Join-Path $bin "QtWebEngineProcess.exe"
    [IO.File]::WriteAllText($helper, "locked helper")
    $lock = [IO.File]::Open($helper, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $failure = ""
    [IO.File]::WriteAllText($config, "@{ QtDir = 'missing-qt'; DependencyRoot = '$deps'; BuildRoot = '$build' }")
    try { & $script Release -ConfigFile $config -QtDir $qt }
    catch { $failure = $_.Exception.Message }
    if ($failure -notlike "Cannot delete *") { throw "Expected locked-helper failure with explicit Qt override, got: $failure" }
    $lock.Dispose()
    $lock = $null

    # Unlocked executables are deleted before configure (missing import libraries stop this fixture).
    $failure = ""
    try { & $script Release -ConfigFile $config -QtDir $qt }
    catch { $failure = $_.Exception.Message }
    if ($failure -notlike "Dependency import library not found:*") { throw "Unexpected failure: $failure" }
    if (Test-Path $helper) { throw "Unlocked helper was not removed" }
    Write-Host "PASS: locked executables/helpers stop the build; unlocked executables are removed first."

    # Run a copy with fake tools to check scheduling, paths and environment restoration.
    New-Item -ItemType Directory -Force "$temp/tools", "$deps/lib" | Out-Null
    Copy-Item $script "$temp/tools/build.ps1"
    [IO.File]::WriteAllText($config, "@{}")
    [IO.File]::WriteAllText("$temp/tools/msvc-env.ps1", "'{`"FUGUE_BUILD_TEST`":`"child`"}'")
    [IO.File]::WriteAllText("$deps/lib/z.lib", "unused")
    [IO.File]::WriteAllText("$deps/lib/openmpt.lib", "unused")
    $commands = [Collections.Generic.List[object]]::new()
    $before = $env:FUGUE_BUILD_TEST
    function cmake {
        if ($env:FUGUE_BUILD_TEST -ne "child") { throw "Compiler environment missing" }
        $commands.Add(@($args))
        if ($args[0] -eq "--build") { throw "fixture: stop before deployment" }
        $global:LASTEXITCODE = 0
    }
    $failure = ""
    try { & "$temp/tools/build.ps1" Release -QtDir $qt -DependencyRoot $deps -Jobs 3 }
    catch { $failure = $_.Exception.Message }
    if ($failure -ne "fixture: stop before deployment") { throw "Unexpected failure: $failure" }
    if ($env:FUGUE_BUILD_TEST -ne $before) { throw "Compiler environment leaked into caller" }
    $configure, $compile = $commands
    if ($configure[$configure.IndexOf("-G") + 1] -ne "Ninja" -or $configure -notcontains "-DCMAKE_BUILD_TYPE=Release" -or
        $configure -contains "-A") {
        throw "Expected single-config Ninja (Release) without Visual Studio architecture flags"
    }
    if ($compile -contains "--config") { throw "A single-config build takes no --config" }
    if ($compile[$compile.IndexOf("--parallel") + 1] -ne "3" -or
        $compile[1] -ne (Join-Path $temp "build/windows/release")) { throw "Wrong build jobs/path" }
    Write-Host "PASS: Ninja is the default; job overrides and child-only compiler environment work."
} finally {
    if ($null -ne $lock) { $lock.Dispose() }
    Remove-Item -LiteralPath $temp -Recurse -Force
}
