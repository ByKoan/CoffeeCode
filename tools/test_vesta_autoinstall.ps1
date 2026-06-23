# Test del auto-install de VestaVM que usa la extension vesta-lsp.
# Replica EXACTAMENTE el script de vl_start_autoinstall (rama feature,
# --recurse-submodules sin shallow, borrar build, MinGW al PATH, cmake MinGW)
# en un dir temporal y valida que produce vesta_lsp.exe.
#
# Uso:  powershell -ExecutionPolicy Bypass -File test_vesta_autoinstall.ps1 [-Build]
#   sin -Build: solo clona + configura (rapido-ish, valida submodulos+generador)
#   con  -Build: ademas compila el target vesta_lsp (lento, validacion total)
param([switch]$Build)
$ErrorActionPreference = 'Stop'
$repo = 'https://github.com/desmonHak/VM'
$br   = 'feature'
$d    = Join-Path $env:TEMP ('vesta_autoinstall_test_' + [guid]::NewGuid().ToString('N').Substring(0,8))
$s    = Join-Path $d 'src'
$b    = Join-Path $d 'build'
$fail = 0
try {
    New-Item -ItemType Directory -Force $d | Out-Null
    Write-Host "[test] destino: $d"

    # --- MinGW al PATH (igual que la extension) ---
    foreach ($m in 'C:\TDM-GCC-64\bin','C:\mingw64\bin','C:\msys64\mingw64\bin','C:\ProgramData\mingw64\mingw64\bin') {
        if (Test-Path (Join-Path $m 'gcc.exe')) { $env:PATH = $m + ';' + $env:PATH; Write-Host "[test] MinGW: $m"; break }
    }

    # --- clone recursivo (sin --shallow-submodules) ---
    Write-Host "[test] clonando $repo ($br) con submodulos..."
    git clone --depth 1 --recurse-submodules --branch $br $repo $s
    if ($LASTEXITCODE -ne 0) { Write-Host "[test] FALLO: git clone exit $LASTEXITCODE"; $fail = 1 }

    # --- validar que los submodulos con CMakeLists existen (lo que fallaba) ---
    $subs = @('preprocessor',
              'libs\SourceCode\capstone',
              'libs\SourceCode\keystone',
              'libs\SourceCode\LibPEparse',
              'libs\SourceCode\ftxui',
              'libs\SourceCode\DistanciaLevenshtein')
    foreach ($sm in $subs) {
        $cm = Join-Path $s (Join-Path $sm 'CMakeLists.txt')
        if (Test-Path $cm) { Write-Host "[test]   OK submodulo: $sm" }
        else { Write-Host "[test]   FALTA CMakeLists: $sm"; $fail = 1 }
    }

    if ($fail -eq 0) {
        # --- configurar con MinGW (build/ limpio) ---
        if (Test-Path $b) { Remove-Item -Recurse -Force $b }
        Write-Host "[test] cmake configure (MinGW Makefiles)..."
        cmake -S $s -B $b -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { Write-Host "[test] FALLO: cmake configure exit $LASTEXITCODE"; $fail = 1 }
    }

    if ($fail -eq 0 -and $Build) {
        Write-Host "[test] cmake build --target vesta_lsp (lento)..."
        cmake --build $b --target vesta_lsp
        if ($LASTEXITCODE -ne 0) { Write-Host "[test] FALLO: build exit $LASTEXITCODE"; $fail = 1 }
        $exe = Join-Path $b 'vesta_lsp.exe'
        if (Test-Path $exe) { Write-Host "[test]   OK binario: $exe" }
        else { Write-Host "[test]   FALTA vesta_lsp.exe"; $fail = 1 }
    }
}
catch {
    Write-Host "[test] EXCEPCION: $($_.Exception.Message)"; $fail = 1
}
finally {
    Write-Host "[test] limpiando $d"
    Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
}
if ($fail -eq 0) { Write-Host "`n[test] RESULTADO: OK" }
else { Write-Host "`n[test] RESULTADO: FALLO" }
exit $fail
