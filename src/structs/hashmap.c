/**
 * @file hashmap.c
 * @brief Implementación de la tabla hash de direccionamiento abierto (ver
 * hashmap.h).
 *
 * Linear probing con capacidad potencia de 2 y borrado por desplazamiento hacia
 * atrás (algoritmo R de Knuth adaptado), que evita "tombstones" y mantiene las
 * cadenas de sondeo compactas. Factor de carga máximo 3/4.
 *
 * @note Cómo funciona en conjunto. Cada clave produce un hash; su "casa" (home)
 * es @c hash & (cap-1), es decir, la ranura ideal donde le gustaría vivir. Si
 * esa ranura está ocupada por otra clave (colisión), el "linear probing" prueba
 * la siguiente, la siguiente, etc., dando la vuelta al llegar al final (de ahí
 * que sea circular). El conjunto de ranuras que se recorre desde la casa hasta
 * donde realmente se guarda una clave es su "cadena de sondeo". Buscar consiste
 * en recorrer esa cadena hasta encontrar la clave o una ranura vacía (que
 * significa "no está").
 *
 * @note Por qué potencia de 2. Con @c cap potencia de 2, @c hash mod cap es
 * idéntico a @c hash & (cap-1): una operación AND, mucho más barata que una
 * división. Por eso @c cap se mantiene siempre en potencia de 2.
 *
 * @note Disposición SoA. Las claves, los valores, los hashes y el estado
 * (vacía/ocupada) viven en cuatro arrays paralelos indexados por ranura.
 * Cachear el hash en @c hashes evita recalcularlo y permite descartar
 * candidatos en la búsqueda con una simple comparación de enteros antes de
 * comparar las claves.
 */
#include "structs/hashmap.h"

#include <stdlib.h>
#include <string.h>

#define HM_MIN_CAP 8 /**< Capacidad mínima (potencia de 2). */
/**< Centinela "no encontrado" (índice imposible). */
#define HM_NOT_FOUND ((size_t)-1)

/**
 * @brief Redondea @p n hacia arriba a potencia de 2 (mínimo HM_MIN_CAP).
 *
 * Arranca en @c HM_MIN_CAP y duplica (desplazamiento a la izquierda, @c <<= 1)
 * hasta alcanzar o superar @p n. Garantiza así el invariante de capacidad
 * potencia de 2 del que depende el enmascarado @c & (cap-1).
 *
 * @param n Tamaño mínimo deseado.
 * @return Menor potencia de 2 que es >= @p n y >= HM_MIN_CAP.
 */
static size_t next_pow2(size_t n) {
    size_t c = HM_MIN_CAP; /* nunca por debajo del mínimo */
    while (c < n)
        c <<= 1; /* duplica hasta cubrir n */
    return c;
}

/**
 * @brief Hash FNV-1a por defecto sobre @p n bytes de la clave.
 *
 * FNV-1a: parte de un valor base ("offset basis") y, por cada byte, primero
 * hace XOR del byte con el acumulador y luego lo multiplica por un primo grande
 * ("prime"). La mezcla XOR-luego-multiplicar dispersa bien los bits y da buena
 * distribución para claves pequeñas. Trabaja en 64 bits y al final se trunca a
 * @c size_t.
 *
 * @param key Puntero a los bytes de la clave.
 * @param n Número de bytes a hashear.
 * @return Hash de la clave.
 */
static size_t fnv1a(const void *key, size_t n) {
    const unsigned char *p = (const unsigned char *)key; /* leer byte a byte */
    uint64_t h = 1469598103934665603ULL; /* FNV offset basis (64 bits) */
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];             /* 1a: XOR del byte ANTES de multiplicar */
        h *= 1099511628211ULL; /* FNV prime (64 bits) */
    }
    return (size_t)h;
}

/**
 * @brief Igualdad por bytes por defecto.
 *
 * Compara @p n bytes crudos de las dos claves con @c memcmp. Sirve para claves
 * tipo POD (enteros, structs sin padding relevante). Para cadenas hay que
 * inyectar ::hashmap_str_eq.
 *
 * @return 1 si los @p n bytes coinciden, 0 si no.
 */
static int eq_bytes(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n) == 0;
}

/**
 * @brief Puntero a la clave de la ranura @p i.
 *
 * Indexa el array de claves por bytes: ranura @p i empieza en
 * @c keys + i*key_size.
 */
static inline void *hm_key(const HashMap *m, size_t i) {
    return (char *)m->keys + i * m->key_size;
}

/**
 * @brief Puntero al valor de la ranura @p i.
 *
 * Igual que ::hm_key pero sobre el array de valores y con @c val_size.
 */
static inline void *hm_val(const HashMap *m, size_t i) {
    return (char *)m->vals + i * m->val_size;
}

/**
 * @brief Reserva los arrays para @p cap ranuras. @return 1 en éxito.
 *
 * Reserva los cuatro arrays paralelos. El de estado se reserva con @c calloc
 * para que todas las ranuras empiecen como "vacías" (0). Si alguna reserva
 * falla, libera todo lo reservado y deja los punteros a NULL (rollback limpio).
 *
 * @param m Tabla cuyos arrays se reservan.
 * @param cap Número de ranuras (debe ser potencia de 2).
 * @return 1 en éxito, 0 si falló alguna reserva.
 */
static int hm_alloc(HashMap *m, size_t cap) {
    m->keys = malloc(cap * m->key_size); /* claves: contenido sin inicializar */
    /* valores: contenido sin inicializar */
    m->vals = malloc(cap * m->val_size);
    m->hashes = malloc(cap * sizeof(size_t)); /* hashes cacheados por ranura */
    m->state = calloc(cap, 1); /* estado: 0 = todas vacías (importante) */
    if (!m->keys || !m->vals || !m->hashes || !m->state) {
        /* fallo parcial: liberar lo que sí se reservó y dejar todo en NULL */
        free(m->keys);
        free(m->vals);
        free(m->hashes);
        free(m->state);
        m->keys = m->vals = NULL;
        m->hashes = NULL;
        m->state = NULL;
        return 0;
    }
    m->cap = cap; /* capacidad efectiva tras reservar */
    return 1;
}

/**
 * @copydoc hashmap_init
 *
 * Atajo: inicializa con capacidad mínima y las funciones por defecto (FNV-1a /
 * comparación por bytes) delegando en ::hashmap_init_ex.
 */
int hashmap_init(HashMap *m, size_t key_size, size_t val_size) {
    return hashmap_init_ex(m, key_size, val_size, HM_MIN_CAP, NULL, NULL);
}

/**
 * @copydoc hashmap_init_ex
 *
 * Pone todos los campos a un estado vacío conocido, elige hash/igualdad (las
 * dadas o las de por defecto) y reserva los arrays con la capacidad inicial
 * redondeada a potencia de 2.
 */
int hashmap_init_ex(HashMap *m, size_t key_size, size_t val_size,
                    size_t initial_cap, HashFn hash, EqFn eq) {
    m->keys = m->vals = NULL; /* punteros limpios por si falla alloc */
    m->hashes = NULL;
    m->state = NULL;
    m->cap = 0;
    m->count = 0; /* tabla vacía */
    m->key_size = key_size;
    m->val_size = val_size;
    m->hash = hash ? hash : fnv1a; /* hash inyectado o FNV-1a por defecto */
    m->eq = eq ? eq : eq_bytes;    /* igualdad inyectada o por bytes */
    /* capacidad: la sugerida (o el mínimo) redondeada a potencia de 2 */
    return hm_alloc(m, next_pow2(initial_cap ? initial_cap : HM_MIN_CAP));
}

/**
 * @copydoc hashmap_free
 *
 * Libera los cuatro arrays y deja la estructura en estado vacío seguro.
 */
void hashmap_free(HashMap *m) {
    free(m->keys);
    free(m->vals);
    free(m->hashes);
    free(m->state);
    m->keys = m->vals = NULL; /* evitar punteros colgantes */
    m->hashes = NULL;
    m->state = NULL;
    m->cap = 0;
    m->count = 0;
}

/**
 * @copydoc hashmap_clear
 *
 * Vaciar sin liberar: basta con marcar todas las ranuras como vacías (estado a
 * 0) y poner @c count a 0. Las claves/valores antiguos quedan como basura
 * inalcanzable (el estado los protege). Conserva la capacidad.
 */
void hashmap_clear(HashMap *m) {
    if (m->state) memset(m->state, 0, m->cap); /* todas las ranuras a "vacía" */
    m->count = 0;
}

/**
 * @brief Coloca una entrada (clave única) en una tabla con sitio; no comprueba
 *        duplicados ni hace crecer. Usado por put (tras find) y por el rehash.
 *
 * Linear probing de inserción, paso a paso:
 *   1. Calcula la casa: @c i = h & mask  (con @c mask = cap-1).
 *   2. Mientras la ranura @c i esté ocupada, avanza a la siguiente
 *      (@c i = (i+1) & mask), envolviendo al llegar al final.
 *   3. La primera ranura vacía es el destino: márcala ocupada, cachea el hash y
 *      copia clave y valor.
 *
 * @note Asume que SIEMPRE hay al menos una ranura vacía (el factor de carga < 1
 * lo garantiza), por eso el bucle termina. Y asume que la clave NO existe ya
 * (el llamante lo comprobó con ::hm_find), por eso no busca duplicados.
 *
 * @param m Tabla con sitio libre.
 * @param h Hash ya calculado de @p key.
 * @param key Clave a copiar.
 * @param val Valor a copiar.
 */
static void hm_place(HashMap *m, size_t h, const void *key, const void *val) {
    size_t mask = m->cap - 1; /* cap potencia de 2 -> mask = bits bajos */
    size_t i = h & mask;      /* casa: hash mod cap, sin división */
    while (m->state[i])       /* mientras ocupada, sondea linealmente */
        i = (i + 1) & mask;   /* siguiente ranura, envolviendo */

    m->state[i] = 1;  /* marca ocupada */
    m->hashes[i] = h; /* cachea el hash para futuras búsquedas */
    /* copia la clave dentro de la tabla */
    memcpy(hm_key(m, i), key, m->key_size);
    memcpy(hm_val(m, i), val, m->val_size); /* copia el valor */
    m->count++;                             /* una entrada más */
}

/**
 * @brief Reasigna a @p newcap ranuras y reinserta las entradas.
 *
 * Rehash: crear una tabla "hermana" con más ranuras y reinsertar en ella todas
 * las entradas vivas. No se pueden copiar los arrays tal cual porque la casa de
 * cada clave depende de @c cap (cambia @c mask), así que hay que recolocarlas.
 *
 * Paso a paso:
 *   1. Clona los metadatos (tamaños, funciones) en una tabla temporal @c nm con
 *      punteros a NULL, y reserva sus arrays con la nueva capacidad.
 *   2. Recorre la tabla vieja; por cada ranura ocupada, ::hm_place reinserta la
 *      entrada en @c nm reusando el hash cacheado (no se recalcula).
 *   3. Libera los arrays viejos y "adopta" los nuevos en @p m.
 *
 * @param m Tabla a redimensionar.
 * @param newcap Nueva capacidad (potencia de 2, mayor que la actual).
 * @return 1 en éxito, 0 si falló la reserva (la tabla original queda intacta).
 */
static int hm_grow(HashMap *m, size_t newcap) {
    HashMap nm = *m; /* copia metadatos (key_size, hash, eq...) */
    nm.keys = NULL;  /* pero arrays propios y vacíos */
    nm.vals = NULL;
    nm.hashes = NULL;
    nm.state = NULL;
    nm.cap = 0;
    nm.count = 0;
    if (!hm_alloc(&nm, newcap)) return 0; /* sin memoria: m queda intacta */

    /* reinsertar cada entrada viva; hm_place la recoloca según la nueva mask */
    for (size_t i = 0; i < m->cap; i++)
        if (m->state[i])
            hm_place(&nm, m->hashes[i], hm_key(m, i), hm_val(m, i));

    /* descartar los arrays viejos y adoptar los nuevos */
    free(m->keys);
    free(m->vals);
    free(m->hashes);
    free(m->state);
    m->keys = nm.keys;
    m->vals = nm.vals;
    m->hashes = nm.hashes;
    m->state = nm.state;
    m->cap = nm.cap;
    m->count = nm.count; /* igual que antes; solo cambió la disposición */
    return 1;
}

/**
 * @brief Busca la ranura de @p key. @return índice o HM_NOT_FOUND.
 *
 * Recorre la cadena de sondeo desde la casa de la clave:
 *   1. @c i = h & mask (casa).
 *   2. Mientras @c i esté OCUPADA:
 *        - si el hash cacheado coincide Y @c eq confirma la igualdad de claves,
 *          se encontró: devuelve @c i.
 *        - si no, avanza a la siguiente ranura (envolviendo).
 *   3. Una ranura VACÍA corta la búsqueda: por el invariante de las cadenas, si
 *      la clave existiera estaría antes de cualquier hueco. Devuelve
 * HM_NOT_FOUND.
 *
 * @note La comparación rápida de hashes evita llamar a @c eq (potencialmente
 * cara, p. ej. @c strcmp) salvo cuando los hashes ya coinciden.
 *
 * @param m Tabla.
 * @param key Clave buscada.
 * @param h Hash ya calculado de @p key.
 * @return Índice de la ranura, o HM_NOT_FOUND si no está.
 */
static size_t hm_find(const HashMap *m, const void *key, size_t h) {
    size_t mask = m->cap - 1;
    size_t i = h & mask;  /* empieza en la casa */
    while (m->state[i]) { /* recorre ranuras ocupadas */
        /* hash igual (filtro barato) y claves iguales (confirmación) -> hallada
         */
        if (m->hashes[i] == h && m->eq(hm_key(m, i), key, m->key_size))
            return i;
        i = (i + 1) & mask; /* sigue la cadena de sondeo */
    }
    return HM_NOT_FOUND; /* ranura vacía: la clave no existe */
}

/**
 * @copydoc hashmap_put
 *
 * Insertar o actualizar, paso a paso:
 *   1. Calcula el hash y busca la clave. Si ya existe, sobrescribe su valor y
 *      termina (no se inserta duplicado).
 *   2. Si no existe, comprueba el factor de carga: si tras insertar pasaría de
 *      3/4 ( (count+1)*4 > cap*3 ), duplica la capacidad con ::hm_grow antes de
 *      insertar. Mantener carga < 3/4 conserva cadenas cortas y rápidas.
 *   3. Coloca la nueva entrada con ::hm_place.
 *
 * @return 1 en éxito, 0 si el crecimiento falló por falta de memoria.
 */
int hashmap_put(HashMap *m, const void *key, const void *val) {
    size_t h = m->hash(key, m->key_size); /* hash de la clave */

    size_t existing = hm_find(m, key, h); /* ¿ya está? */
    if (existing != HM_NOT_FOUND) {
        /* actualiza el valor in situ */
        memcpy(hm_val(m, existing), val, m->val_size);
        return 1;
    }

    /* crecer si superaríamos el factor de carga 3/4 */
    /* (count+1)*4 > cap*3  <=>  (count+1)/cap > 3/4, sin aritmética
     * fraccionaria */
    if ((m->count + 1) * 4 > m->cap * 3) {
        if (!hm_grow(m, m->cap * 2)) return 0; /* duplica capacidad o falla */
    }
    hm_place(m, h, key, val); /* inserta la entrada nueva */
    return 1;
}

/**
 * @copydoc hashmap_get
 *
 * Localiza la clave y devuelve un puntero a su valor (mutable), o NULL si no
 * está.
 */
void *hashmap_get(const HashMap *m, const void *key) {
    size_t h = m->hash(key, m->key_size);
    size_t i = hm_find(m, key, h);
    return i == HM_NOT_FOUND ? NULL : hm_val(m, i);
}

/**
 * @copydoc hashmap_has
 *
 * Igual que get pero solo informa de presencia (1/0), sin exponer el valor.
 */
int hashmap_has(const HashMap *m, const void *key) {
    size_t h = m->hash(key, m->key_size);
    return hm_find(m, key, h) != HM_NOT_FOUND;
}

/**
 * @brief ¿Está @p home cíclicamente en el intervalo @c (i, j]?
 *
 * Auxiliar del borrado. Responde: la casa @p home, ¿cae estrictamente después
 * de
 * @p i y hasta @p j (incluido), recorriendo el array de forma circular?
 *
 *   - Caso normal (@c i < j, sin envolver): basta con @c home>i && home<=j.
 *   - Caso envuelto (@c i >= j, el intervalo da la vuelta por el final): el
 *     intervalo es la unión "@c home>i" (cola del array) O "@c home<=j"
 * (cabeza), así que vale con que se cumpla cualquiera de las dos.
 *
 * Se usa para decidir si una entrada candidata sigue "bien colocada" respecto
 * al hueco que se está rellenando (ver ::hashmap_remove).
 *
 * @param i Posición del hueco (excluida del intervalo).
 * @param home Casa de la entrada candidata.
 * @param j Posición de la candidata (incluida en el intervalo).
 * @return 1 si @p home está en @c (i, j] cíclico, 0 si no.
 */
static int hm_in_range(size_t i, size_t home, size_t j) {
    if (i < j) return home > i && home <= j; /* intervalo normal */
    return home > i || home <= j; /* intervalo que envuelve por el final */
}

/**
 * @copydoc hashmap_remove
 *
 * Borrado por DESPLAZAMIENTO HACIA ATRÁS (backward-shift), la pieza clave de
 * esta tabla. Borrar a secas (marcar la ranura vacía) rompería las cadenas de
 * sondeo: una clave que colisionó y se guardó MÁS ADELANTE en la cadena
 * quedaría "inalcanzable", porque hm_find se detiene en el primer hueco. La
 * solución clásica son "tombstones" (marcas de borrado), pero ensucian la tabla
 * y degradan la búsqueda con el tiempo. Aquí, en su lugar, se rellena el hueco
 * tirando hacia atrás de entradas posteriores que lo necesiten.
 *
 * Paso a paso:
 *   1. Busca la clave. Si no está, no hay nada que borrar (devuelve 0).
 *   2. Marca su ranura @c s como vacía: ese es el primer "hueco" (@c i = s).
 *   3. Avanza @c j por la cadena (j = (j+1) & mask):
 *        - Si la ranura @c j está VACÍA, la cadena termina ahí: fin del bucle.
 *        - Si @c j está ocupada, mira su casa @c home. Decide con ::hm_in_range
 *          si esa entrada está "bien colocada" respecto al hueco:
 *            * Si @c home cae en @c (i, j] (cíclico), la entrada NO puede subir
 * al hueco sin quedar antes de su propia casa: se deja donde está (continue),
 * el hueco sigue en @c i.
 *            * Si NO (su casa está en @c i o antes, "al otro lado" del hueco),
 *              moverla al hueco @c i la deja igual de alcanzable o mejor: se
 * copia clave/valor/hash de @c j a @c i, se marca @c i ocupada y @c j vacía.
 *              Ahora el hueco se traslada a @c j (@c i = j) y se sigue.
 *   4. Cuando se alcanza una ranura vacía, todas las cadenas afectadas quedan
 *      compactas y sin huecos internos. La tabla no tiene tombstones.
 *
 * @note Coste: O(longitud de la cadena), normalmente muy corto gracias al
 * factor de carga acotado.
 *
 * @return 1 si se eliminó, 0 si la clave no existía.
 */
int hashmap_remove(HashMap *m, const void *key) {
    size_t h = m->hash(key, m->key_size);
    size_t s = hm_find(m, key, h);   /* ranura de la clave a borrar */
    if (s == HM_NOT_FOUND) return 0; /* no existe: nada que hacer */

    size_t mask = m->cap - 1;
    m->state[s] = 0; /* abre el hueco: ranura s vacía */
    m->count--;

    /* desplazamiento hacia atrás: rellenar el hueco con entradas posteriores
       cuyo "home" no quede antes del hueco, manteniendo las cadenas válidas. */
    size_t i = s, j = s; /* i = hueco actual; j = sonda exploradora */
    for (;;) {
        j = (j + 1) & mask;      /* siguiente candidata en la cadena */
        if (!m->state[j]) break; /* hueco natural: cadena terminada */
        size_t home = m->hashes[j] & mask; /* casa ideal de la candidata j */
        if (hm_in_range(i, home, j))
            continue; /* la entrada j está bien colocada respecto al hueco */

        /* j puede subir al hueco i sin romper su alcanzabilidad: muévela */
        memcpy(hm_key(m, i), hm_key(m, j), m->key_size);
        memcpy(hm_val(m, i), hm_val(m, j), m->val_size);
        m->hashes[i] = m->hashes[j]; /* arrastra también el hash cacheado */
        m->state[i] = 1;             /* i pasa a ocupada */
        m->state[j] = 0;             /* y j queda como nuevo hueco */
        i = j;                       /* el hueco se mueve a j */
    }
    return 1;
}

/**
 * @copydoc hashmap_next
 *
 * Iteración por barrido lineal del array: desde el cursor @p iter, avanza hasta
 * la siguiente ranura OCUPADA, devuelve sus punteros a clave/valor y deja el
 * cursor justo después para la próxima llamada. El orden es el físico de las
 * ranuras (no tiene relación con el de inserción).
 *
 * @return 1 si entregó una entrada, 0 cuando no quedan.
 */
int hashmap_next(const HashMap *m, size_t *iter, void **key_out,
                 void **val_out) {
    for (size_t i = *iter; i < m->cap; i++) {
        if (m->state[i]) { /* primera ranura ocupada desde el cursor */
            if (key_out) *key_out = hm_key(m, i);
            if (val_out) *val_out = hm_val(m, i);
            *iter = i + 1; /* reanudar tras esta ranura */
            return 1;
        }
    }
    return 0; /* no quedan entradas */
}

/**
 * @copydoc hashmap_str_hash
 *
 * FNV-1a sobre el CONTENIDO de la cadena, no sobre el puntero. Como la ranura
 * almacena un @c char* (la clave es un puntero a cadena), primero se
 * desreferencia para obtener el @c char* y luego se hashea byte a byte hasta el
 * @c '\0'.
 * @c key_size se ignora (la longitud la marca el terminador nulo).
 */
size_t hashmap_str_hash(const void *key, size_t key_size) {
    (void)key_size; /* no se usa: la cadena es nul-terminada */
    const char *s = *(const char *const *)key; /* la clave es un char* */
    uint64_t h = 1469598103934665603ULL;       /* FNV offset basis */
    for (; *s; ++s) {
        h ^= (unsigned char)*s; /* XOR del carácter... */
        h *= 1099511628211ULL;  /* ...y multiplica por el primo */
    }
    return (size_t)h;
}

/**
 * @copydoc hashmap_str_eq
 *
 * Igualdad de claves tipo cadena: desreferencia ambos punteros a @c char* y los
 * compara con @c strcmp (por contenido). @c key_size se ignora.
 */
int hashmap_str_eq(const void *a, const void *b, size_t key_size) {
    (void)key_size;
    return strcmp(*(const char *const *)a, *(const char *const *)b) == 0;
}
