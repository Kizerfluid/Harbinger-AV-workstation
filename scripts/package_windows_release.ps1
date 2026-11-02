# Package an existing Harbinger v0.6.3 Release/x64 Visual Studio build.
# Run from PowerShell in the Harbinger repo: .\scripts\package_windows_release.ps1
# Requires matching Qt6 windeployqt (normally installed through vcpkg).
param(
    [string]$WindeployQt = "",
    [string]$FFmpegBinDir = ""
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = "0.6.3"
$builtExe = Join-Path $root "build-vcpkg\Release\Harbinger.exe"
if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "Release executable not found at $builtExe. Build Harbinger Release/x64 in Visual Studio first."
}

if (-not $WindeployQt) {
    $cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($cmd) { $WindeployQt = $cmd.Source }
}
if (-not $WindeployQt) {
    $roots = @(
        (Join-Path $root "vcpkg_installed"),
        (Join-Path $env:USERPROFILE "tools\vcpkg\installed")
    )
    foreach ($r in $roots) {
        if (Test-Path -LiteralPath $r) {
            $candidate = Get-ChildItem -LiteralPath $r -Filter "windeployqt.exe" -File -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -notmatch '[\\/](debug|Debug)[\\/]' } |
                Select-Object -First 1
            if ($candidate) { $WindeployQt = $candidate.FullName; break }
        }
    }
}
if (-not $WindeployQt -or -not (Test-Path -LiteralPath $WindeployQt)) {
    throw "Cannot locate windeployqt.exe. Run with -WindeployQt 'C:\full\path\to\windeployqt.exe'."
}

$releases = Join-Path $root 'release'
$baseName = "Harbinger-v$version-Windows-x64"
$stage = Join-Path $releases $baseName
$archive = Join-Path $releases "$baseName.zip"
New-Item -ItemType Directory -Force -Path $releases | Out-Null
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $builtExe -Destination (Join-Path $stage 'Harbinger.exe')

# Ensure the Qt tools' location is in PATH, which helps vcpkg-based Qt deployments.
$deployToolDir = Split-Path -Parent $WindeployQt
$oldPath = $env:PATH
try {
    $env:PATH = "$deployToolDir;$oldPath"
    & $WindeployQt --release --no-compiler-runtime (Join-Path $stage 'Harbinger.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with code $LASTEXITCODE" }
} finally {
    $env:PATH = $oldPath
}

if (-not (Test-Path -LiteralPath (Join-Path $stage 'platforms\qwindows.dll'))) {
    throw 'Windows Qt platform plugin platforms\qwindows.dll was not deployed. Do not publish this archive.'
}

if ($FFmpegBinDir) {
    $ffmpegExe = Join-Path $FFmpegBinDir 'ffmpeg.exe'
    $ffprobeExe = Join-Path $FFmpegBinDir 'ffprobe.exe'
    if (!(Test-Path -LiteralPath $ffmpegExe) -or !(Test-Path -LiteralPath $ffprobeExe)) {
        throw '-FFmpegBinDir must contain both ffmpeg.exe and ffprobe.exe.'
    }
    Copy-Item -LiteralPath $ffmpegExe,$ffprobeExe -Destination $stage
    Write-Warning 'FFmpeg binaries are included: YOU must include their applicable license, attribution, and corresponding-source compliance before publishing.'
}

foreach ($file in @('LICENSE', 'THIRD_PARTY_NOTICES.md')) {
    $path = Join-Path $root $file
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $stage }
}
@'
Harbinger v0.6.3 — Windows x64

1. Extract the entire ZIP. Do not run the EXE directly from inside the ZIP.
2. Run Harbinger.exe.
3. FFmpeg and FFprobe are required. Install them and add to PATH, or use Harbinger's Tools menu to select the executable paths.
4. For Hive worker mode, OpenSSL CLI (openssl.exe) must also be available on PATH to generate a TLS worker identity.
5. Install the official Microsoft Visual C++ 2015–2022 x64 Redistributable if the runtime is missing:
   https://aka.ms/vs/17/release/vc_redist.x64.exe
6. Test Smart Encoding, MKV muxing and Hive TLS pairing before distributing this build.

This package contains the Harbinger application and Qt deployment libraries.
If FFmpeg is not included, install it separately; do not assume it is bundled.
The NVIDIA GPU driver, if any, must be installed separately.
Read LICENSE and THIRD_PARTY_NOTICES.md for licence information.
'@ | Set-Content -LiteralPath (Join-Path $stage 'README_FIRST.txt') -Encoding UTF8

if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$sha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$sha  $baseName.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Host "Release archive: $archive"
Write-Host "SHA-256: $sha"
Write-Host 'IMPORTANT: Test on a clean Windows machine. windeployqt may not supply plugins loaded only at runtime (especially TLS).'
