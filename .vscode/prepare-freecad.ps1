param(
    [string]$BuildDirectory = "build/python311",
    [string]$ProfileDirectory = "build/python311/vscode-profile"
)

$ErrorActionPreference = "Stop"
$workspace = Split-Path -Parent $PSScriptRoot
$buildPath = [IO.Path]::GetFullPath((Join-Path $workspace $BuildDirectory))
$profilePath = [IO.Path]::GetFullPath((Join-Path $workspace $ProfileDirectory))
$workspacePrefix = $workspace.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
foreach ($targetPath in @($buildPath, $profilePath)) {
    if (-not $targetPath.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build and profile directories must be inside this workspace."
    }
}

$cacheFile = Join-Path $buildPath "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $cacheFile)) {
    throw "Configure $BuildDirectory for the matching FreeCAD Python SDK first. See README.md."
}
$cache = Get-Content -LiteralPath $cacheFile
function Get-CacheValue([string]$name) {
    foreach ($line in $cache) {
        if ($line -match ('^' + [regex]::Escape($name) + ':[^=]+=(.*)$')) {
            return $Matches[1]
        }
    }
    throw "Missing $name in $cacheFile"
}

$cmake = Get-CacheValue "CMAKE_COMMAND"
$compiler = Get-CacheValue "CMAKE_CXX_COMPILER"
if ($compiler -notmatch '^(.*)[/\\]VC[/\\]Tools[/\\]MSVC[/\\]') {
    throw "This Windows launch task requires an MSVC CMake build."
}
$vcvars = Join-Path $Matches[1] "VC/Auxiliary/Build/vcvars64.bat"
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw "MSVC developer environment not found: $vcvars"
}

# Use the toolchain already selected by CMake; no developer shell is required
# for the VS Code process. Keep paths quoted through cmd's batch invocation.
$buildCommand = 'call "{0}" >nul && "{1}" --build "{2}" --parallel 6' -f $vcvars, $cmake, $buildPath
& $env:ComSpec /d /s /c $buildCommand
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --install $buildPath --prefix (Join-Path $profilePath "Mod/DXFCAD")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
