$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot
& zig cc -target x86_64-windows-gnu -O2 -municode '-Wl,--subsystem,windows' `
    (Join-Path $PSScriptRoot 'launcher.c') -o (Join-Path $packageRoot 'Ordenador_CE3X.exe') `
    -lcomdlg32 -lshell32 -lgdi32 -luser32 -lcomctl32
if ($LASTEXITCODE -ne 0) { throw 'Ha fallado la compilacion de la interfaz.' }
