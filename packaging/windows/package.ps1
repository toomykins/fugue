# Run after a Release build. Qt and vcpkg dependencies must match the compiler/architecture.
param(
    [Parameter(Mandatory = $true)][string]$QtDir,
    [string]$BuildDir = "build",
    [string]$DependencyBin = "build/vcpkg_installed/x64-windows/bin",
    [string]$Destination = "dist"
)
$ErrorActionPreference = "Stop"
$deploy = Join-Path $QtDir "bin/windeployqt.exe"
if (!(Test-Path $deploy)) { throw "windeployqt not found: $deploy" }
if (!(Test-Path $DependencyBin)) { throw "Dependency DLL directory not found: $DependencyBin" }
cmake --install $BuildDir --prefix $Destination
if ($LASTEXITCODE -ne 0) { throw "Install failed" }
$bin = Join-Path $Destination "bin"
& $deploy --release --compiler-runtime (Join-Path $bin "fugue-qt.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed" }
# Use the dedicated manifest install tree, not a shared vcpkg installation with unrelated DLLs.
Copy-Item (Join-Path $DependencyBin "*.dll") $bin
$root = Resolve-Path (Join-Path $PSScriptRoot "../..")
Copy-Item (Join-Path $root "LICENSE"), (Join-Path $root "THIRD-PARTY-NOTICES.md") $Destination
$share = Join-Path (Split-Path $DependencyBin) "share"
Get-ChildItem $share -Directory | ForEach-Object {
    $notice = Join-Path $_.FullName "copyright"
    if (Test-Path $notice) {
        $out = Join-Path $Destination ("licenses/" + $_.Name)
        New-Item -ItemType Directory -Force $out | Out-Null
        Copy-Item $notice $out
    }
}
Write-Host "Portable build: $bin/fugue-qt.exe (keep the entire directory together)"
