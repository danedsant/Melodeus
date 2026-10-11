# fetch_deps.ps1 — puebla external/ con dependencias versionadas (no se versionan).
# Idempotente: si external/<n>/.melodeus-ref coincide con el ref pedido, se omite.
#   .\scripts\fetch_deps.ps1            # deps del grupo f0 (imgui, JUCE, doctest)
#   .\scripts\fetch_deps.ps1 -Group all # todas las registradas (fases posteriores)
#   .\scripts\fetch_deps.ps1 -Force     # re-descarta y vuelve a clonar
[CmdletBinding()]
param(
    [ValidateSet('f0', 'all')]
    [string]$Group = 'f0',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$ext = Join-Path $repoRoot 'external'
New-Item -ItemType Directory -Force -Path $ext | Out-Null

# kind 'git' (defecto): clone superficial del tag/commit.
# kind 'zip'  : descarga y expande; si el zip contiene un unico directorio
#               superior, sus contenidos se colocan planos en external/<n>/.
# kind 'file' : descarga un archivo suelto en external/<n>/<file>.
# kind 'files': descarga la lista `files` desde `base`+nombre a external/<n>/.
$deps = @(
    @{ name = 'imgui';       url = 'https://github.com/ocornut/imgui.git';       ref = 'v1.92.9b'; group = 'f0' }
    @{ name = 'JUCE';        url = 'https://github.com/juce-framework/JUCE.git'; ref = '9.0.3';    group = 'f0' }
    @{ name = 'doctest';     url = 'https://github.com/doctest/doctest.git';     ref = 'v2.5.3';   group = 'f0' }
    @{ name = 'onnxruntime'; kind = 'zip'
       url  = 'https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg'
       ref = '1.24.4'; group = 'f0' }
    @{ name = 'yt-dlp';      kind = 'file'; file = 'yt-dlp.exe'
       url  = 'https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp.exe'
       ref = '2026.08.19'; group = 'f0' }
    @{ name = 'ffmpeg';      kind = 'zip'
       url  = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n9.0-latest-win64-lgpl-9.0.zip'
       ref = 'n9.0'; group = 'f0' }
    @{ name = 'dr_libs';     kind = 'files'
       base = 'https://raw.githubusercontent.com/mackron/dr_libs/dfe8377631000664666519fdb83da193fd8037f4/'
       files = @('dr_wav.h', 'dr_flac.h')
       ref = 'dfe8377'; group = 'f1' }
    @{ name = 'minimp3';     kind = 'files'
       base = 'https://raw.githubusercontent.com/lieff/minimp3/ea99364f61c14656440e8d77e9c233ccf3124633/'
       files = @('minimp3.h', 'minimp3_ex.h')
       ref = 'ea99364'; group = 'f1' }
)

$wanted = $deps | Where-Object { $Group -eq 'all' -or $_.group -eq $Group }
if (-not $wanted) { throw "Ninguna dependencia para el grupo '$Group'" }

$failures = @()
foreach ($d in $wanted) {
    $dir = Join-Path $ext $d.name
    $marker = Join-Path $dir '.melodeus-ref'
    if (-not $Force -and (Test-Path $marker) -and
        ((Get-Content $marker -Raw).Trim() -eq $d.ref)) {
        Write-Host "[skip] $($d.name)@$($d.ref)"
        continue
    }
    if (Test-Path $dir) {
        Write-Host "[drop] $($d.name) (ref anterior o sin marcador)"
        Remove-Item -Recurse -Force $dir
    }
    Write-Host "[get ] $($d.name)@$($d.ref) ..."
    $tmp = Join-Path $env:TEMP "melodeus-$($d.name).bin"
    $tmpDir = Join-Path $env:TEMP "melodeus-extract-$($d.name)"
    try {
        if ($d.kind -eq 'file') {
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
            Invoke-WebRequest -Uri $d.url -OutFile (Join-Path $dir $d.file) -TimeoutSec 600
        }
        elseif ($d.kind -eq 'files') {
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
            foreach ($f in $d.files) {
                Invoke-WebRequest -Uri ($d.base + $f) -OutFile (Join-Path $dir $f) -TimeoutSec 120
            }
        }
        elseif ($d.kind -eq 'zip') {
            Invoke-WebRequest -Uri $d.url -OutFile $tmp -TimeoutSec 600
            if (Test-Path $tmpDir) { Remove-Item -Recurse -Force $tmpDir }
            Expand-Archive -Path $tmp -DestinationPath $tmpDir -Force
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
            $top = @(Get-ChildItem -Force $tmpDir)
            $src = if ($top.Count -eq 1 -and $top[0].PSIsContainer) { $top[0].FullName } else { $tmpDir }
            Get-ChildItem -Force $src | Move-Item -Destination $dir -Force
            Remove-Item -Recurse -Force $tmpDir
        }
        else {
            git -c core.longpaths=true -c advice.detachedHead=false clone --depth 1 --branch $d.ref --quiet $d.url $dir 2>&1 |
                ForEach-Object { Write-Host "       $_" }
            if ($LASTEXITCODE -ne 0) {
                $failures += $d.name
                Write-Warning "clone falló: $($d.name)"
                continue
            }
        }
    }
    catch {
        $failures += $d.name
        Write-Warning "descarga falló: $($d.name) — $_"
        continue
    }
    finally {
        if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue }
        if (Test-Path $tmpDir) { Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue }
    }
    Set-Content -Path $marker -Value $d.ref -NoNewline
    Write-Host "[ ok ] $($d.name)"
}

if ($failures.Count -gt 0) { throw "Dependencias fallidas: $($failures -join ', ')" }
Write-Host "OK: $($wanted.Count) dependencia(s) listas en external/"
