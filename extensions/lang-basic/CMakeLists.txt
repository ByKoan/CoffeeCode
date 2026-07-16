# Extension "lang-basic": resaltado sincrono inmediato (sin LSP) para todo
# lenguaje declarado en languages.json.  Enlaza coffee_ext_common solo por
# cJSON (parsear languages.json); no usa jsonrpc/lsp_client de ahi.
add_library(coffee_lang_basic SHARED lang_basic.c)
target_link_libraries(coffee_lang_basic PRIVATE coffee_ext_common)
coffee_extension_finalize(coffee_lang_basic)

# languages.json viaja junto al DLL/manifiesto (mismo sitio del que el IDE
# carga la extension): en runtime se localiza via ext_dir(host).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/languages.json"
    "${COFFEE_EXT_OUTDIR}/languages.json"
    COPYONLY)
