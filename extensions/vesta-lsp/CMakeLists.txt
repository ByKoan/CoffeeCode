# =============================================================================
# Extension vesta-lsp: cliente del language server de Vesta (Vex).
# -----------------------------------------------------------------------------
# Arranca el servidor LSP de Vesta (vesta_lsp.exe) como subproceso del core,
# sincroniza el documento .vex activo y muestra los diagnosticos de compilacion
# en vivo (gutter + fondo de linea + panel "Problemas").  Publica el servicio
# "coffee.svc.lsp" para que otras extensiones consulten el servidor.
#
# Enlaza el SDK comun de extensiones (coffee_ext_common: lsp_client + jsonrpc +
# cJSON).  No depende de libvesta ni de SDL: solo del header de la API del IDE
# (lo anyade coffee_extension_finalize) y del SDK comun.  El servidor LSP es un
# ejecutable externo que el core lanza por pipes en runtime.
# =============================================================================

# El target se crea a mano (no con add_coffee_extension) para poder enlazar el
# SDK comun antes de finalizar la extension.
add_library(coffee_vesta_lsp SHARED "${CMAKE_CURRENT_SOURCE_DIR}/vesta_lsp_ext.c")

# SDK comun (JSON-RPC / LSP).  Sus headers publicos (lsp_client.h, svc_lsp.h,
# cJSON.h) llegan via target_include_directories PUBLIC del propio SDK.
if(TARGET coffee_ext_common)
    target_link_libraries(coffee_vesta_lsp PRIVATE coffee_ext_common)
else()
    message(WARNING "extension vesta-lsp: falta coffee_ext_common (SDK comun); se omite")
    return()
endif()

# Coloca la DLL en <build>/extensions/vesta-lsp/, genera el manifiesto y la
# encadena al ejecutable.
coffee_extension_finalize(coffee_vesta_lsp)
