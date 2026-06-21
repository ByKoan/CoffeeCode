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

# --- layout (puro: logica de divisores arrastrables, sin SDL) --------------
# Solo enlaza layout_core.c (hit-test + clampeo sobre enteros crudos); los
# envoltorios sobre Editor viven en layout.c y necesitan SDL, asi que no entran.
add_coffee_test(test_layout layout/test_layout.c
    "${CMAKE_SOURCE_DIR}/src/layout/layout_core.c")

# --- dock (puro: arbol de paneles del editor, sin SDL) ---------------------
# Solo enlaza dock.c (construccion + mutacion + layout + hit del arbol); el
# wireado al Editor (render/input) vive aparte y necesita SDL.
add_coffee_test(test_dock dock/test_dock.c
    "${CMAKE_SOURCE_DIR}/src/dock/dock.c")

# --- float (puro: geometria + hit-test de los paneles flotantes, sin SDL) ---
# Solo enlaza float.c (sub-rects + hit-test + clamp del marco flotante).  El
# wireado al Editor (render/input) vive aparte y necesita SDL.
add_coffee_test(test_float dock/test_float.c
    "${CMAKE_SOURCE_DIR}/src/dock/float.c")

# --- float-dock (puro: deteccion + acople de un flotante al dock, sin SDL) ---
# Verifica la logica pura del re-acople por arrastre: dock_drop_zone (deteccion
# del destino), el movimiento de todas las pestanas del grupo (caso CENTER) y la
# division de la hoja destino (caso de borde).  Enlaza dock.c (arbol) y float.c
# (geometria del marco); el wireado al Editor vive en input/render y necesita SDL.
add_coffee_test(test_float_dock dock/test_float_dock.c
    "${CMAKE_SOURCE_DIR}/src/dock/dock.c"
    "${CMAKE_SOURCE_DIR}/src/dock/float.c")

# --- detached (puro: geometria de las ventanas desprendidas, sin SDL) -------
# Solo enlaza detached_geom.c (tira de pestanas + area de contenido de una
# ventana desprendida a partir de su tamano).  El ciclo de vida de la ventana del
# SO, el enrutado de eventos por SDL_WindowID y el render multi-ventana viven
# aparte y necesitan SDL.
add_coffee_test(test_detached detached/test_detached.c
    "${CMAKE_SOURCE_DIR}/src/detached/detached_geom.c")

# --- tab_membership (puro: invariante "pestana activa por grupo", sin SDL) --
# Evita el bug del "buffer compartido": el indice activo de un grupo nunca debe
# apuntar a una pestana de otro grupo (p.ej. una movida a un flotante).
add_coffee_test(test_tab_membership editor/test_tab_membership.c
    "${CMAKE_SOURCE_DIR}/src/editor/tab_membership.c")

# --- tab_reorder (puro: reordenado/insercion de pestanas en una barra) ------
# Verifica el calculo de la posicion de insercion por X y el reordenado del
# array de pestanas con remapeo de los indices guardados (active_tab +
# group_active_tab[]).  Logica pura sobre enteros, sin SDL ni la struct Editor.
add_coffee_test(test_tab_reorder editor/test_tab_reorder.c
    "${CMAKE_SOURCE_DIR}/src/editor/tab_reorder.c")

# --- tabmove (puro: mover pestanas entre ventanas, sin SDL) -----------------
# Verifica la aritmetica del desprender/fusionar multi-ventana: anyadir al array
# destino + quitar del origen compactando + reparar los indices guardados.  La
# misma logica que editor_transfer_group/editor_merge_all sobre los structs
# reales, aqui sobre arrays planos de group_id.  Solo enlaza tabmove.c (puro).
add_coffee_test(test_tabmove app/test_tabmove.c
    "${CMAKE_SOURCE_DIR}/src/app/tabmove.c")

# --- layout_persist (puro: (de)serializacion de la disposicion, sin SDL) ----
# Verifica el round-trip de la struct POD LayoutData (pestanas, arbol de dock,
# flotantes y tamanos de la UI) a/desde texto, y la robustez ante texto
# vacio/basura.  Solo enlaza layout_persist.c (capa pura); el wireado al Editor
# vive en layout_persist_editor.c y necesita SDL.
add_coffee_test(test_layout_persist session/test_layout_persist.c
    "${CMAKE_SOURCE_DIR}/src/session/layout_persist.c")

# --- panel (puro: almacen de canales del panel inferior, sin SDL) ----------
# Solo enlaza panel_core.c (registro de canales + scrollback acotado).  El
# render y el input del panel viven sobre el Editor y necesitan SDL.
add_coffee_test(test_panel panel/test_panel.c
    "${CMAKE_SOURCE_DIR}/src/panel/panel_core.c")

# --- extension host (puro: ext_host + buffer + utf8 + vec; carga DLLs) ------
# Smoke test HEADLESS del cargador de extensiones: carga la DLL hello-c,
# dispara su comando, manipula el buffer y verifica la descarga limpia.
if(COFFEE_BUILD_EXTENSIONS)
    add_coffee_test(test_ext_host ext/test_ext_host.c
        "${CMAKE_SOURCE_DIR}/src/ext/ext_host.c"
        "${CMAKE_SOURCE_DIR}/src/buffer/buffer.c"
        "${CMAKE_SOURCE_DIR}/src/utf8/utf8.c"
        "${CMAKE_SOURCE_DIR}/src/structs/vec.c")
    # Ruta del directorio de la extension (manifiesto generado + DLL) inyectada
    # en compile.  Apunta al BUILD dir: ahi viven el coffee-extension.toml
    # generado por configure_file y la DLL coffee_hello, no en el arbol fuente
    # (donde solo esta el .toml.in).
    target_compile_definitions(test_ext_host PRIVATE
        "COFFEE_HELLO_EXT_DIR=\"${CMAKE_BINARY_DIR}/extensions/hello-c\"")
    # Carga dinamica: en POSIX el host usa dlopen/dlclose (libdl).
    if(UNIX AND NOT APPLE)
        target_link_libraries(test_ext_host PRIVATE dl)
    endif()
    # El test necesita que la DLL de la extension exista antes de ejecutarse.
    add_dependencies(test_ext_host coffee_hello)

    # --- extension host: feedback de errores de carga ----------------------
    # Verifica que ext_host_last_error captura un motivo legible en cada ruta
    # de fallo (dir inexistente, DLL ausente, simbolo de entrada ausente).  Los
    # fixtures se generan en runtime bajo COFFEE_ERR_TMP_DIR (el build dir).
    add_coffee_test(test_ext_errors ext/test_ext_errors.c
        "${CMAKE_SOURCE_DIR}/src/ext/ext_host.c"
        "${CMAKE_SOURCE_DIR}/src/buffer/buffer.c"
        "${CMAKE_SOURCE_DIR}/src/utf8/utf8.c"
        "${CMAKE_SOURCE_DIR}/src/structs/vec.c")
    target_compile_definitions(test_ext_errors PRIVATE
        "COFFEE_ERR_TMP_DIR=\"${CMAKE_BINARY_DIR}/test_ext_errors_tmp\"")
    if(UNIX AND NOT APPLE)
        target_link_libraries(test_ext_errors PRIVATE dl)
    endif()
    # Asegurar que el dir temporal de fixtures existe antes de ejecutar.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test_ext_errors_tmp")

    # --- extension vesta (IDE -> libvesta -> VM de Vex) --------------------
    # Solo si la extension vesta se construyo (libvesta presente).  El test
    # carga coffee_vesta.dll, que a su vez carga libvesta.dll en runtime; por
    # eso libvesta + sus deps (OpenSSL) se copian junto al ejecutable del test.
    if(TARGET coffee_vesta)
        add_coffee_test(test_ext_vesta ext/test_ext_vesta.c
            "${CMAKE_SOURCE_DIR}/src/ext/ext_host.c"
            "${CMAKE_SOURCE_DIR}/src/buffer/buffer.c"
            "${CMAKE_SOURCE_DIR}/src/utf8/utf8.c"
            "${CMAKE_SOURCE_DIR}/src/structs/vec.c")
        target_compile_definitions(test_ext_vesta PRIVATE
            "COFFEE_VESTA_EXT_DIR=\"${CMAKE_BINARY_DIR}/extensions/vesta\"")
        if(UNIX AND NOT APPLE)
            target_link_libraries(test_ext_vesta PRIVATE dl)
        endif()
        add_dependencies(test_ext_vesta coffee_vesta)
        # Las DLLs de runtime (libvesta + OpenSSL) deben estar junto al
        # ejecutable del test para que el cargador de Windows las resuelva al
        # abrir coffee_vesta.dll.
        set(_vesta_rt "${VESTA_DIR}/cmake-build-release")
        add_custom_command(TARGET test_ext_vesta POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_vesta_rt}/libvesta.dll"
                "$<TARGET_FILE_DIR:test_ext_vesta>/libvesta.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_vesta_rt}/libssl-3-x64.dll"
                "$<TARGET_FILE_DIR:test_ext_vesta>/libssl-3-x64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_vesta_rt}/libcrypto-3-x64.dll"
                "$<TARGET_FILE_DIR:test_ext_vesta>/libcrypto-3-x64.dll"
            COMMENT "Copiando libvesta + deps junto al test de la extension vesta")
    endif()
endif()
