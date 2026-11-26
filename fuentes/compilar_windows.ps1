$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot
$iconDir = Join-Path ([System.IO.Path]::GetTempPath()) ('ce3x-icon-' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $iconDir | Out-Null
try {
    python (Join-Path $packageRoot 'scripts/generar_icono.py') (Join-Path $iconDir 'app.ico')
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo generar el icono.' }
    Set-Content -Path (Join-Path $iconDir 'app.rc') -Encoding ascii -Value '101 ICON "app.ico"'
    Push-Location $iconDir
    try {
        & zig rc /fo app.res app.rc
        if ($LASTEXITCODE -ne 0) { throw 'No se pudo compilar el recurso del icono.' }
    } finally { Pop-Location }
    & zig cc -target x86_64-windows-gnu -O2 -municode '-Wl,--subsystem,windows' `
        (Join-Path $PSScriptRoot 'launcher.c') (Join-Path $iconDir 'app.res') -o (Join-Path $packageRoot 'Ordenador_CE3X.exe') `
        -lcomdlg32 -lshell32 -lgdi32 -luser32 -lcomctl32
    if ($LASTEXITCODE -ne 0) { throw 'Ha fallado la compilacion de la interfaz.' }
} finally {
    Remove-Item $iconDir -Recurse -Force
}
