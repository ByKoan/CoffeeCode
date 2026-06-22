# =============================================================================
# SDK comun de extensiones de CoffeeCode  (libreria estatica coffee_ext_common)
# -----------------------------------------------------------------------------
# Protocolo puro + JSON, sin dependencias del IDE ni de SDL: una extension lo
# enlaza para hablar JSON-RPC / LSP con un language server y para compartir
# funcionalidad via contratos de servicio (svc_lsp.h, svc_sqlite.h).  El
# transporte (escritura y bytes entrantes) lo cablea la extension al facility de
# subprocesos del core; el SDK no lo conoce.
#
# cJSON (DaveGamble/cJSON, v1.7.18, licencia MIT) va VENDORIZADO en src/include
# para que el build sea robusto sin red.  La libreria es STATIC y solo arrastra
# cJSON; nunca SDL ni el core.
# =============================================================================

add_library(coffee_ext_common STATIC
    src/cJSON.c
    src/jsonrpc.c
    src/lsp_client.c)

# Headers publicos del SDK (cJSON.h, jsonrpc.h, lsp_client.h, svc_*.h).
target_include_directories(coffee_ext_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

set_target_properties(coffee_ext_common PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
