# genera_programa_grande.py

TOTAL_FUNCIONES = 10000
ARCHIVO_SALIDA = "programa_grande.py"

with open(ARCHIVO_SALIDA, "w", encoding="utf-8") as f:
    f.write('"""Archivo gigante para pruebas de IDE"""\n\n')

    for i in range(TOTAL_FUNCIONES):
        f.write(f"def funcion_{i}():\n")
        f.write(f"    valor = {i}\n")
        f.write(f"    return valor * 2\n\n")

print("Archivo generado:", ARCHIVO_SALIDA)