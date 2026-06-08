#!/usr/bin/env python3
"""Compila y ejecuta las pruebas unitarias de las estructuras (structs).

Uso:
    python test/structs/run_tests.py          # ejecuta todas
    CC=clang python test/structs/run_tests.py # cambiar de compilador

Cada test_*.c se enlaza con todos los src/structs/*.c y se ejecuta. Devuelve
código de salida distinto de cero si alguna prueba falla o emite warnings de
compilación.
"""
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
CC = os.environ.get("CC", "gcc")
FLAGS = ["-std=c11", "-Wall", "-Wextra", "-O2", "-I", "include"]
EXE_EXT = ".exe" if os.name == "nt" else ""


def main():
    os.chdir(ROOT)
    src = sorted(glob.glob(os.path.join("src", "structs", "*.c")))
    tests = sorted(glob.glob(os.path.join("test", "structs", "test_*.c")))
    if not tests:
        print("No se encontraron pruebas en test/structs/")
        return 1

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for test in tests:
            name = os.path.splitext(os.path.basename(test))[0]
            exe = os.path.join(tmp, name + EXE_EXT)

            build = subprocess.run([CC, *FLAGS, test, *src, "-o", exe],
                                   capture_output=True, text=True)
            if build.returncode != 0:
                print(f"BUILD FAIL: {name}\n{build.stderr}")
                failures += 1
                continue
            if build.stderr.strip():
                print(f"WARN ({name}):\n{build.stderr}")
                failures += 1  # tratamos los warnings como fallo (código limpio)

            run = subprocess.run([exe], capture_output=True, text=True)
            sys.stdout.write(run.stdout)
            if run.returncode != 0:
                print(f"TEST FAIL: {name}\n{run.stderr}")
                failures += 1

    print("== todas las pruebas OK ==" if failures == 0
          else f"== {failures} fallo(s) ==")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
