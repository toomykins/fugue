# Configure, build and deploy a directly runnable Windows application.
# Local defaults: build.local.psd1. Explicit parameters override local defaults,
# which override QTDIR / VCPKG_ROOT.
[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [string]$QtDir = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    # An already installed x64-windows tree; must contain include/, lib/, bin/, debug/.
    [string]$DependencyRoot,
    [string]$BuildRoot,
    [ValidateRange(1, 1024)][int]$Jobs,
    [string]$ConfigFile
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (!$ConfigFile) { $ConfigFile = Join-Path $root "build.local.psd1" }
if (Test-Path -LiteralPath $ConfigFile -PathType Leaf) {
    $local = Import-PowerShellDataFile -LiteralPath $ConfigFile
    $keys = @("QtDir", "VcpkgRoot", "DependencyRoot", "BuildRoot")
    foreach ($key in $local.Keys) {
        if ($key -notin $keys) { throw "Unknown setting '$key' in $ConfigFile" }
        if ($local[$key] -isnot [string] -or [string]::IsNullOrWhiteSpace($local[$key])) {
            throw "Setting '$key' in $ConfigFile must be a nonempty string."
        }
        if (!$PSBoundParameters.ContainsKey($key)) { Set-Variable -Name $key -Value $local[$key] }
    }
} elseif ($PSBoundParameters.ContainsKey("ConfigFile")) {
    throw "Build configuration not found: $ConfigFile"
}
# Resolve relative SDK/output paths against the repository, not the caller's directory.
foreach ($key in @("QtDir", "VcpkgRoot", "DependencyRoot", "BuildRoot")) {
    $value = Get-Variable -Name $key -ValueOnly
    if ($value -and ![IO.Path]::IsPathRooted($value)) {
        Set-Variable -Name $key -Value (Join-Path $root $value)
    }
}
if (!$BuildRoot) { $BuildRoot = Join-Path $root "build/windows" }
if (!$QtDir) { throw "Set QtDir in build.local.psd1, supply -QtDir, or set QTDIR." }
if (!$DependencyRoot) {
    if (!$VcpkgRoot) { throw "Set VcpkgRoot or DependencyRoot in build.local.psd1, supply the matching parameter, or set VCPKG_ROOT." }
    $DependencyRoot = Join-Path $VcpkgRoot "installed/x64-windows"
}
$QtDir = (Resolve-Path $QtDir).Path
$DependencyRoot = (Resolve-Path $DependencyRoot).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$build = Join-Path $BuildRoot $Configuration.ToLowerInvariant()
$cache = Join-Path $build "CMakeCache.txt"
if ((Test-Path $cache) -and !(Select-String -Path $cache -Quiet "^CMAKE_GENERATOR:INTERNAL=Ninja$")) {
    throw "Build tree uses another generator; choose a new BuildRoot. Nothing removed."
}
$bin = Join-Path $build "app"
$exe = Join-Path $bin "fugue-qt.exe"
$deploy = Join-Path $QtDir "bin/windeployqt.exe"
$dependencyBin = Join-Path $DependencyRoot "bin"
if ($Configuration -eq "Debug") { $dependencyBin = Join-Path $DependencyRoot "debug/bin" }
if (!(Test-Path $deploy -PathType Leaf)) { throw "windeployqt not found: $deploy" }
if (!(Test-Path $dependencyBin -PathType Container)) { throw "Dependency DLL directory not found: $dependencyBin" }

# Do this BEFORE configuring/building, including WebEngine's deployed helper.
# Windows refuses to delete running executables. Never kill the user's processes
# or swallow this failure: otherwise a successful-looking build can leave stale binaries.
if (Test-Path $bin) {
    foreach ($file in Get-ChildItem $bin -Filter "*.exe" -File -Recurse) {
        Write-Host "Removing $($file.FullName)"
        try { Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop }
        catch { throw "Cannot delete '$($file.FullName)'. Close the application/helper and retry. $($_.Exception.Message)" }
    }
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $saved = @{}
    try {
        foreach ($entry in $compilerEnv.PSObject.Properties) {
            $saved[$entry.Name] = [Environment]::GetEnvironmentVariable($entry.Name, "Process")
            [Environment]::SetEnvironmentVariable($entry.Name, $entry.Value, "Process")
        }
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
    } finally {
        foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], "Process") }
    }
}
$release = if ($Configuration -eq "Release") { "ON" } else { "OFF" }
$dependencyLib = Join-Path $DependencyRoot "lib"
$zlib = Join-Path $dependencyLib "z.lib"
if ($Configuration -eq "Debug") {
    $dependencyLib = Join-Path $DependencyRoot "debug/lib"
    $zlib = Join-Path $dependencyLib "zd.lib"
}
$openmpt = Join-Path $dependencyLib "openmpt.lib"
foreach ($library in @($zlib, $openmpt)) {
    if (!(Test-Path $library -PathType Leaf)) { throw "Dependency import library not found: $library" }
}
$compilerEnv = & "$PSScriptRoot/msvc-env.ps1" | ConvertFrom-Json
if (!$Jobs) {
    $Jobs = & cmake -P "$root/cmake/BuildJobs.cmake"
    if ($LASTEXITCODE -ne 0) { throw "Could not determine build job limit." }
}
Write-Host "Ninja / MSVC: $Jobs build jobs"
Invoke-Checked "cmake" @(
    "-S", $root, "-B", $build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_CXX_COMPILER=cl",
    "-DCMAKE_PREFIX_PATH=$QtDir;$DependencyRoot",
    "-DOPENMPT_LIBRARY=$openmpt", "-DZLIB_LIBRARY_DEBUG=$zlib", "-DZLIB_LIBRARY_RELEASE=$zlib",
    "-DFUGUE_RELEASE=$release", "-DFUGUE_OPENMPT=ON", "-DFUGUE_HEADLESS_ONLY=OFF", "-DFUGUE_BUILD_SHELL=ON"
)
Invoke-Checked "cmake" @("--build", $build, "--target", "fugue-qt", "--parallel", "$Jobs")
$mode = "--" + $Configuration.ToLowerInvariant()
Invoke-Checked $deploy @($mode, "--compiler-runtime", $exe)
# Copy the selected dependency tree's DLLs, including transitive dependencies.
# Prefer a project-specific installed tree to avoid deploying unrelated packages.
Get-ChildItem $dependencyBin -Filter "*.dll" -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $bin -Force
}
# Check the deployed directory without adding SDK directories to PATH.
Invoke-Checked $exe @("--version")
Write-Host ""
Write-Host "Built and deployed: $exe"
if ($Configuration -eq "Debug") {
    Write-Host "Debug defaults to the game client. Use '$exe pager' to open the launcher."
}
Write-Host "Keep the executable, DLLs and deployed subdirectories together."
