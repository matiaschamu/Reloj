$ErrorActionPreference = 'Stop'

$archiveUrl = 'https://github.com/espressif/openocd-esp32/releases/download/v0.12.0-esp32-20260703/openocd-esp32-win64-0.12.0-esp32-20260703.zip'
$expectedSha256 = 'a49147705e49be9cab4af473e58b5730d99a0220d10f0976a04bf725cd0aa960'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cacheRoot = Join-Path $projectRoot '.pio'
$archivePath = Join-Path $cacheRoot 'openocd-esp32-win64-20260703.zip'
$extractRoot = Join-Path $cacheRoot 'openocd-esp32-20260703'
$toolRoot = Join-Path $extractRoot 'openocd-esp32'
$executablePath = Join-Path $toolRoot 'bin\openocd.exe'
$manifestSource = Join-Path $PSScriptRoot 'openocd-package.json'
$manifestDestination = Join-Path $toolRoot 'package.json'

if (-not (Test-Path -LiteralPath $cacheRoot)) {
    New-Item -ItemType Directory -Path $cacheRoot | Out-Null
}

if (-not (Test-Path -LiteralPath $executablePath)) {
    Invoke-WebRequest $archiveUrl -OutFile $archivePath
    $actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $expectedSha256) {
        throw "Checksum SHA-256 incorrecto: $actualSha256"
    }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force
}

Copy-Item -LiteralPath $manifestSource -Destination $manifestDestination -Force
Write-Host "OpenOCD listo en: $toolRoot"
