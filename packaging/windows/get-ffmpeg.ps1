# Descarga FFmpeg para Windows (lo llama build.bat): la compilación estática «gpl» de BtbN
# (https://github.com/BtbN/FFmpeg-Builds, la de GitHub), comprobada con la suma SHA-256 que publica
# junto a ella, y deja ffmpeg.exe y su licencia en -Dest. Kaint lo usa (junto a kaint.exe) para leer
# el audio que no es WAV en el editor de GIF (MP3, OGG, FLAC...) y para exportar vídeo (MP4, WebM):
# la «gpl» trae libx264, libvpx, libopus y libwebp, que usa la exportación.
#   powershell -File get-ffmpeg.ps1 -Arch x64|arm64 -Dest <carpeta>
param(
    [ValidateSet("x64", "arm64")][string]$Arch = "x64",
    [Parameter(Mandatory = $true)][string]$Dest
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$release = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest"
$name = if ($Arch -eq "arm64") { "ffmpeg-master-latest-winarm64-gpl.zip" } else { "ffmpeg-master-latest-win64-gpl.zip" }
$work = Join-Path ([IO.Path]::GetTempPath()) ("kaint-ffmpeg-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    $curl = Join-Path $env:SystemRoot "System32\curl.exe"
    if (-not (Test-Path $curl)) { $curl = "curl.exe" }
    $sums = Join-Path $work "checksums.sha256"
    $zip = Join-Path $work $name
    & $curl -L --fail -s -o $sums "$release/checksums.sha256"
    if ($LASTEXITCODE -ne 0) { throw "No se pudo descargar checksums.sha256." }
    Write-Host "Descargando $name..."
    & $curl -L --fail --progress-bar -o $zip "$release/$name"
    if ($LASTEXITCODE -ne 0) { throw "No se pudo descargar $name." }

    # La suma del archivo («<sha256>  <nombre>» en cada línea).
    $line = Get-Content $sums | Where-Object { ($_ -split "\s+")[-1] -eq $name } | Select-Object -First 1
    if (-not $line) { throw "checksums.sha256 no trae la suma de $name." }
    $want = ($line -split "\s+")[0].ToLowerInvariant()
    $have = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
    if ($want -ne $have) { throw "La suma SHA-256 de $name no coincide (se publicó otra versión mientras se descargaba: vuelve a intentarlo)." }

    # Con el tar de Windows (rápido); si no está, con Expand-Archive.
    $out = Join-Path $work "x"
    New-Item -ItemType Directory -Force $out | Out-Null
    $tar = Join-Path $env:SystemRoot "System32\tar.exe"
    if (Test-Path $tar) {
        & $tar -xf $zip -C $out
        if ($LASTEXITCODE -ne 0) { throw "No se pudo descomprimir $name." }
    } else {
        Expand-Archive -Path $zip -DestinationPath $out -Force
    }
    $exe = Get-ChildItem $out -Recurse -Filter "ffmpeg.exe" | Select-Object -First 1
    if (-not $exe) { throw "$name no trae ffmpeg.exe." }
    New-Item -ItemType Directory -Force $Dest | Out-Null
    Copy-Item $exe.FullName (Join-Path $Dest "ffmpeg.exe") -Force
    $license = Get-ChildItem $out -Recurse -Filter "LICENSE.txt" | Select-Object -First 1
    if ($license) { Copy-Item $license.FullName (Join-Path $Dest "ffmpeg-LICENSE.txt") -Force }
    Write-Host "FFmpeg listo: $(Join-Path $Dest 'ffmpeg.exe')"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
