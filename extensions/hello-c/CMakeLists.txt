# Extension de ejemplo en C: inserta texto en el buffer, suscribe eventos y
# publica un servicio.  No tiene dependencias externas, asi que basta con la
# utilidad simple que ofrece el CMake de extensiones.
add_coffee_extension(coffee_hello "${CMAKE_CURRENT_SOURCE_DIR}/hello.c")
