# Builds a self-contained release folder (dist/Phoenix) plus a portable zip:
#   1. CMake configure + build with -DPHOENIX_RELEASE=ON (windowed exe)
#   2. Stage Phoenix.exe + the Qt runtime next to it via windeployqt
#   3. Zip the folder as dist/Phoenix-<ver>-portable.zip
#
# Optional: set the environment variable ISCC to the Inno Setup 6 compiler
# path to also build the installer (tools/release/Phoenix.iss).

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$QtBin = "C:\msys64\mingw64\bin"
$Ninja = Join-Path $QtBin "ninja.exe"
$Windeployqt = Join-Path $QtBin "windeployqt.exe"
$Version = "0.9.0"

$BuildDir = Join-Path $Root "build-release"
$DistDir = Join-Path $Root "dist\Phoenix"
$ZipPath = Join-Path $Root "dist\Phoenix-$Version-portable.zip"

if (-not (Test-Path $Ninja)) { throw "Ninja not found at $Ninja (is MSYS2 MinGW installed?)" }
if (-not (Test-Path $Windeployqt)) { throw "windeployqt not found at $Windeployqt" }

# 1) Configure + build the windowed release exe.
& cmake -S $Root -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DPHOENIX_RELEASE=ON -DCMAKE_PREFIX_PATH=$QtBin
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& $Ninja -C $BuildDir Phoenix
if ($LASTEXITCODE -ne 0) { throw "ninja build failed" }

# 2) Stage the exe + Qt runtime.
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item (Join-Path $BuildDir "Phoenix.exe") (Join-Path $DistDir "Phoenix.exe")
# windeployqt prints a harmless dxcompiler warning to stderr; keep it from
# tripping $ErrorActionPreference="Stop" and rely on $LASTEXITCODE instead.
$oldEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $Windeployqt --release --no-translations --no-system-d3d-compiler `
    --no-opengl-sw (Join-Path $DistDir "Phoenix.exe") 2>&1 | ForEach-Object { "$_" }
$ErrorActionPreference = $oldEap
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# windeployqt does not grab the MinGW C/C++ runtime - copy it explicitly.
foreach ($rt in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $src = Join-Path $QtBin $rt
    if (-not (Test-Path $src)) { throw "missing runtime DLL: $src" }
    Copy-Item $src (Join-Path $DistDir $rt)
}

# 3) Portable zip.
New-Item -ItemType Directory -Force -Path (Split-Path $ZipPath) | Out-Null
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path $DistDir -DestinationPath $ZipPath
Write-Output "Portable build: $ZipPath"

# 4) Optional installer (only when ISCC points at Inno Setup 6).
if ($env:ISCC) {
    & $env:ISCC (Join-Path $PSScriptRoot "Phoenix.iss")
    if ($LASTEXITCODE -ne 0) { throw "installer build failed" }
}