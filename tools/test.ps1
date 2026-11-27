# Configure, build and run the tests on Windows.
# The default is an optimised development build (RelWithDebInfo, test hooks kept): the suite
# runs several times faster than in Debug. Use Debug to step through a failure.
# Local defaults: build.local.psd1, with the same precedence as build.ps1.
[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet("RelWithDebInfo", "Debug")][string]$Configuration = "RelWithDebInfo",
    # CTest selection: -Label is ctest -L, -ExcludeLabel is -LE, -Regex is -R. None selects every test,
    # including those that open windows on the desktop.
    [string]$Label,
    [string]$ExcludeLabel,
    [string]$Regex,
    # Graphics backends the GPU and native entries run on: "default" is the client's own on this platform (Vulkan on
    # Windows), "all" every backend, or a list such as "d3d11,opengl". Changing it reconfigures, so not with -NoBuild.
    [string]$Backends = "default",
    [string]$QtDir = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    # An already installed x64-windows tree; must contain include/, lib/, bin/, debug/.
    [string]$DependencyRoot,
    [string]$BuildRoot,
    [ValidateRange(1, 1024)][int]$Jobs,
    # Concurrent test processes; defaults to the build job limit.
    [ValidateRange(1, 1024)][int]$TestJobs,
    [string]$ConfigFile,
    # Run the tests of an existing build. CTest never rebuilds, so use this only after a build of the current sources.
    [switch]$NoBuild
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
# One tree per configuration: Debug links the dependency tree's debug libraries, the others its release ones.
# The Debug tree is build.ps1's; build.ps1's release tree has no test hooks, so it is not used here.
$build = Join-Path $BuildRoot $Configuration.ToLowerInvariant()
$cache = Join-Path $build "CMakeCache.txt"
if ((Test-Path $cache) -and !(Select-String -Path $cache -Quiet "^CMAKE_GENERATOR:INTERNAL=Ninja$")) {
    throw "Build tree uses another generator; choose a new BuildRoot. Nothing removed."
}
$dependencyBin = Join-Path $DependencyRoot "bin"
$dependencyLib = Join-Path $DependencyRoot "lib"
$zlib = Join-Path $dependencyLib "z.lib"
if ($Configuration -eq "Debug") {
    $dependencyBin = Join-Path $DependencyRoot "debug/bin"
    $dependencyLib = Join-Path $DependencyRoot "debug/lib"
    $zlib = Join-Path $dependencyLib "zd.lib"
}
$openmpt = Join-Path $dependencyLib "openmpt.lib"
$qtBin = Join-Path $QtDir "bin"
if (!(Test-Path $qtBin -PathType Container)) { throw "Qt DLL directory not found: $qtBin" }
if (!(Test-Path $dependencyBin -PathType Container)) { throw "Dependency DLL directory not found: $dependencyBin" }
foreach ($library in @($zlib, $openmpt)) {
    if (!(Test-Path $library -PathType Leaf)) { throw "Dependency import library not found: $library" }
}

function Invoke-Checked([string]$Program, [string[]]$Arguments, $Environment) {
    $saved = @{}
    try {
        foreach ($name in $Environment.Keys) {
            $saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
            [Environment]::SetEnvironmentVariable($name, $Environment[$name], "Process")
        }
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
    } finally {
        foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], "Process") }
    }
}
if (!$Jobs) {
    $Jobs = & cmake -P "$root/cmake/BuildJobs.cmake"
    if ($LASTEXITCODE -ne 0) { throw "Could not determine build job limit." }
}
if (!$TestJobs) { $TestJobs = $Jobs }
if (!$NoBuild) {
    $compilerEnv = @{}
    foreach ($entry in (& "$PSScriptRoot/msvc-env.ps1" | ConvertFrom-Json).PSObject.Properties) {
        $compilerEnv[$entry.Name] = $entry.Value
    }
    Write-Host "Ninja / MSVC / ${Configuration}: $Jobs build jobs"
    Invoke-Checked "cmake" @(
        "-S", $root, "-B", $build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_CXX_COMPILER=cl",
        "-DCMAKE_PREFIX_PATH=$QtDir;$DependencyRoot",
        "-DOPENMPT_LIBRARY=$openmpt", "-DZLIB_LIBRARY_DEBUG=$zlib", "-DZLIB_LIBRARY_RELEASE=$zlib",
        "-DFUGUE_RELEASE=OFF", "-DFUGUE_OPENMPT=ON", "-DFUGUE_HEADLESS_ONLY=OFF", "-DFUGUE_BUILD_SHELL=ON",
        "-DFUGUE_TEST_GPU_BACKENDS=$($Backends -replace ',', ';')"
    ) $compilerEnv
    # CTest does not rebuild: a failed build stops here, before stale binaries could be tested.
    Invoke-Checked "cmake" @("--build", $build, "--parallel", "$Jobs") $compilerEnv
} elseif (!(Test-Path $cache)) {
    throw "No build tree at $build; run without -NoBuild first."
}
$ctest = @("--test-dir", $build, "-j", "$TestJobs", "--no-tests=error", "--output-on-failure")
if ($Label) { $ctest += @("-L", $Label) }
if ($ExcludeLabel) { $ctest += @("-LE", $ExcludeLabel) }
if ($Regex) { $ctest += @("-R", $Regex) }
# The tests load Qt and the dependency DLLs from the SDK directories; nothing is deployed.
Invoke-Checked "ctest" $ctest @{ PATH = "$qtBin;$dependencyBin;$env:PATH" }
