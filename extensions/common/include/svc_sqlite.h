/**
 * @file svc_sqlite.h
 * @brief Contrato del servicio sqlite compartido entre extensiones.
 *
 * Define la vtable que una extension con sqlite embebido publica con
 * register_service() para que otras extensiones reusen su base de datos sin
 * enlazar su propia copia de sqlite.
 *
 * Es solo el contrato (el header): la IMPLEMENTACION del proveedor sqlite no es
 * de esta fase y se conectara cuando exista una extension que la provea.  La
 * API es minima: abrir, ejecutar SQL con callback de filas, y cerrar.
 */
#ifndef COFFEE_EXT_SVC_SQLITE_H
#define COFFEE_EXT_SVC_SQLITE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Nombre con el que se registra/obtiene este servicio. */
#define COFFEE_SVC_SQLITE_NAME "coffee.svc.sqlite"

/** Version del contrato; el consumidor comprueba version >= la que espera. */
#define COFFEE_SVC_SQLITE_VERSION 1u

/**
 * @brief Callback de fila para CoffeeSvcSqlite::exec().
 *
 * Se invoca una vez por fila del resultado.  Las cadenas @p col_names y
 * @p col_values son propiedad del proveedor y solo validas durante la llamada.
 *
 * @param ud         Dato de usuario pasado a exec().
 * @param n_cols     Numero de columnas.
 * @param col_values Valores de la fila (como texto; un NULL = columna NULL).
 * @param col_names  Nombres de las columnas.
 * @return 0 para continuar, valor != 0 para abortar el recorrido del resultado.
 */
typedef int (*CoffeeSqliteRowFn)(void *ud, int n_cols,
                                 const char *const *col_values,
                                 const char *const *col_names);

/**
 * @brief Vtable del servicio sqlite compartido.
 *
 * El primer campo es la version del contrato.  @c self es el estado opaco del
 * proveedor y se pasa como primer argumento de cada metodo.
 */
typedef struct CoffeeSvcSqlite {
    /** Version de este contrato (== COFFEE_SVC_SQLITE_VERSION del proveedor). */
    unsigned int version;

    /**
     * @brief Abre (o crea) una base de datos en @p path.
     * @param self   Estado opaco del proveedor.
     * @param path   Ruta del fichero de base de datos (":memory:" para en RAM).
     * @param out_db [out] Recibe el handle opaco de la base de datos.
     * @return 0 en exito, valor < 0 en error.
     */
    int (*open)(void *self, const char *path, void **out_db);

    /**
     * @brief Ejecuta SQL e invoca @p row por cada fila del resultado.
     * @param self  Estado opaco del proveedor.
     * @param db    Handle devuelto por open().
     * @param sql   Sentencia(s) SQL a ejecutar.
     * @param row   Callback de fila, o NULL si no se esperan filas.
     * @param ud    Dato de usuario para @p row.
     * @return 0 en exito, valor < 0 en error.
     */
    int (*exec)(void *self, void *db, const char *sql, CoffeeSqliteRowFn row,
                void *ud);

    /**
     * @brief Cierra una base de datos abierta con open().
     * @param self Estado opaco del proveedor.
     * @param db   Handle a cerrar.
     * @return 0 en exito, valor < 0 en error.
     */
    int (*close)(void *self, void *db);

    /** Puntero opaco al estado del proveedor (primer argumento de los metodos). */
    void *self;
} CoffeeSvcSqlite;

#ifdef __cplusplus
}
#endif

#endif /* COFFEE_EXT_SVC_SQLITE_H */
