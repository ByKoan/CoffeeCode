# =============================================================================
# Tests unitarios de CoffeeCode con la librería ctests (ctests::ctests).
# -----------------------------------------------------------------------------
# Cada test es un ejecutable independiente que enlaza ctests::ctests y solo los
# .c del módulo que prueba (todos módulos "puros", sin SDL). Se registran en
# CTest, así que se lanzan con:  ctest --test-dir <build> --output-on-failure
# =============================================================================

# Fuentes de la librería genérica de estructuras. Por simplicidad, cada test de
# structs enlaza toda la librería (son pocos .c y evita listar dependencias
# cruzadas, p. ej. stack sobre vec).
set(STRUCTS_SRC
    "${CMAKE_SOURCE_DIR}/src/structs/vec.c"
    "${CMAKE_SOURCE_DIR}/src/structs/str.c"
    "${CMAKE_SOURCE_DIR}/src/structs/list.c"
    "${CMAKE_SOURCE_DIR}/src/structs/ring.c"
    "${CMAKE_SOURCE_DIR}/src/structs/stack.c"
    "${CMAKE_SOURCE_DIR}/src/structs/hashmap.c")

# add_coffee_test(<nombre> <fuente_test> [fuentes .c extra del módulo...])
#   Crea el ejecutable, lo enlaza con ctests::ctests y la cabecera include/,
#   y lo registra como test de CTest.
function(add_coffee_test name test_src)
    add_executable(${name} "${test_src}" ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(${name} PRIVATE ctests::ctests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- structs ---------------------------------------------------------------
add_coffee_test(test_vec     structs/test_vec.c     ${STRUCTS_SRC})
add_coffee_test(test_str     structs/test_str.c     ${STRUCTS_SRC})
add_coffee_test(test_list    structs/test_list.c    ${STRUCTS_SRC})
add_coffee_test(test_ring    structs/test_ring.c    ${STRUCTS_SRC})
add_coffee_test(test_stack   structs/test_stack.c   ${STRUCTS_SRC})
add_coffee_test(test_hashmap structs/test_hashmap.c ${STRUCTS_SRC})

# --- buffer y lexer (puros; dependen de structs/vec) -----------------------
add_coffee_test(test_buffer  buffer/test_buffer.c
    "${CMAKE_SOURCE_DIR}/src/buffer/buffer.c"
    "${CMAKE_SOURCE_DIR}/src/utf8/utf8.c"
    "${CMAKE_SOURCE_DIR}/src/structs/vec.c")
add_coffee_test(test_lexer   lexer/test_lexer.c
    "${CMAKE_SOURCE_DIR}/src/lexer/lexer.c"
    "${CMAKE_SOURCE_DIR}/src/structs/vec.c")

# --- encoding (puro, sin dependencias) -------------------------------------
add_coffee_test(test_encoding encoding/test_encoding.c
    "${CMAKE_SOURCE_DIR}/src/encoding/encoding.c")

# --- extension host (puro: ext_host + buffer + utf8 + vec; carga DLLs) ------
# Smoke test HEADLESS del cargador de extensiones: carga la DLL hello-c,
# dispara su comando, manipula el buffer y verifica la descarga limpia.
if(COFFEE_BUILD_EXTENSIONS)
    add_coffee_test(test_ext_host ext/test_ext_host.c
        "${CMAKE_SOURCE_DIR}/src/ext/ext_host.c"
        "${CMAKE_SOURCE_DIR}/src/buffer/buffer.c"
        "${CMAKE_SOURCE_DIR}/src/utf8/utf8.c"
        "${CMAKE_SOURCE_DIR}/src/structs/vec.c")
    # Ruta del directorio de la extension (manifiesto + DLL) inyectada en compile.
    target_compile_definitions(test_ext_host PRIVATE
        "COFFEE_HELLO_EXT_DIR=\"${CMAKE_SOURCE_DIR}/extensions/hello-c\"")
    # Carga dinamica: en POSIX el host usa dlopen/dlclose (libdl).
    if(UNIX AND NOT APPLE)
        target_link_libraries(test_ext_host PRIVATE dl)
    endif()
    # El test necesita que la DLL de la extension exista antes de ejecutarse.
    add_dependencies(test_ext_host coffee_hello)
endif()
