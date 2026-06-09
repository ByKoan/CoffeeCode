@echo off
setlocal EnableExtensions

REM ============================================================
REM  CoffeeCode - Script de compilacion
REM
REM  Uso:  Compile.bat [modo]
REM
REM    release   (por defecto)  Optimizado y portable      (-O3 + LTO)
REM    debug                    Depuracion completa         (-g3 -O0, consola)
REM    native                   Optimizado para ESTA CPU    (-march=native + LTO)
REM    asan                     Debug + AddressSanitizer/UBSan
REM    test                     Compila y ejecuta los tests unitarios (CTest)
REM    clean                    Borra todos los directorios de build
REM
REM  El modo 'test' no compila la app ni SDL: solo la libreria de tests
REM  (ctests, auto-descargada) y los modulos puros. Tras compilar lanza CTest.
REM
REM  Cada modo compila en su propio directorio (build\<modo>), por lo que
REM  cambiar de modo no obliga a recompilar/redescargar las dependencias.
REM ============================================================

set "MODE=%~1"
if "%MODE%"=="" set "MODE=release"

set "GEN=MinGW Makefiles"
set "ROOT=%~dp0"

REM  Variables por modo:  BUILD_TYPE  NATIVE  SANITIZE  APP(COFFEE_BUILD_APP)
REM                       TESTS(COFFEE_BUILD_TESTS)  RUN_TESTS(lanzar CTest)
if /I "%MODE%"=="clean"   goto clean
if /I "%MODE%"=="release" ( set "BUILD_TYPE=Release" & set "NATIVE=OFF" & set "SANITIZE=OFF" & set "APP=ON"  & set "TESTS=OFF" & set "RUN_TESTS=0" & goto build )
if /I "%MODE%"=="debug"   ( set "BUILD_TYPE=Debug"   & set "NATIVE=OFF" & set "SANITIZE=OFF" & set "APP=ON"  & set "TESTS=OFF" & set "RUN_TESTS=0" & goto build )
if /I "%MODE%"=="native"  ( set "BUILD_TYPE=Release" & set "NATIVE=ON"  & set "SANITIZE=OFF" & set "APP=ON"  & set "TESTS=OFF" & set "RUN_TESTS=0" & goto build )
if /I "%MODE%"=="asan"    ( set "BUILD_TYPE=Debug"   & set "NATIVE=OFF" & set "SANITIZE=ON"  & set "APP=ON"  & set "TESTS=OFF" & set "RUN_TESTS=0" & goto build )
if /I "%MODE%"=="test"    ( set "BUILD_TYPE=Debug"   & set "NATIVE=OFF" & set "SANITIZE=OFF" & set "APP=OFF" & set "TESTS=ON"  & set "RUN_TESTS=1" & goto build )

echo [ERROR] Modo desconocido: "%MODE%"
echo Uso: Compile.bat [release ^| debug ^| native ^| asan ^| test ^| clean]
exit /b 1

:clean
echo [CoffeeCode] Limpiando directorios de build...
if exist "%ROOT%build" rmdir /s /q "%ROOT%build"
echo Hecho.
goto end

:build
set "BUILD_DIR=%ROOT%build\%MODE%"

echo ============================================================
echo  CoffeeCode  -  modo: %MODE%
echo  CMAKE_BUILD_TYPE = %BUILD_TYPE%    NATIVE = %NATIVE%    SANITIZE = %SANITIZE%
echo  app = %APP%   tests = %TESTS%
echo  build dir        = %BUILD_DIR%
echo ============================================================

cmake -B "%BUILD_DIR%" -G "%GEN%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCOFFEE_NATIVE=%NATIVE% -DCOFFEE_SANITIZE=%SANITIZE% -DCOFFEE_BUILD_APP=%APP% -DCOFFEE_BUILD_TESTS=%TESTS% -DFETCHCONTENT_QUIET=OFF
if errorlevel 1 goto fail

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% -j
if errorlevel 1 goto fail

if "%RUN_TESTS%"=="1" (
    echo.
    echo [CoffeeCode] Ejecutando los tests...
    REM Lanzamos cada binario directamente para ver el informe completo de
    REM ctests (suites, progreso, resumen). CTest tambien los conoce: puedes
    REM usar  ctest --test-dir "%BUILD_DIR%"  para un resumen agregado.
    set "TEST_FAIL="
    pushd "%BUILD_DIR%\test"
    for %%T in (test_*.exe) do (
        echo.
        "%%~fT"
        if errorlevel 1 set "TEST_FAIL=1"
    )
    popd
    if defined TEST_FAIL goto fail
    echo.
    echo [OK] Todos los tests pasaron.
    goto end
)

echo.
echo [OK] Compilacion completada.
echo      Binario: %BUILD_DIR%\CoffeeCode.exe
goto end

:fail
echo.
echo [ERROR] Fallo la compilacion en modo %MODE%.
pause
exit /b 1

:end
pause
