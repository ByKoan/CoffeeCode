# Extension "lsp-highlight": resaltado de sintaxis via semantic tokens de un
# language server real.  Necesita el SDK comun (jsonrpc + lsp_client + cJSON),
# asi que NO usa add_coffee_extension() (esa es para extensiones sin deps):
# crea el target a mano y lo enlaza contra coffee_ext_common.
add_library(coffee_lsp_highlight SHARED lsp_highlight.c)
target_link_libraries(coffee_lsp_highlight PRIVATE coffee_ext_common)
coffee_extension_finalize(coffee_lsp_highlight)

# servers.json viaja junto al DLL/manifiesto: en runtime se localiza via
# ext_dir(host). Editarlo (anyadir/quitar lenguajes y sus LSP) no requiere
# tocar ni recompilar lsp_highlight.c.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/servers.json"
    "${COFFEE_EXT_OUTDIR}/servers.json"
    COPYONLY)
