# CONTRIBUTING

Gracias por tu interes en aportar trabajo/ideas a este proyecto.

Este documentos recojera las normas e instrucciones para contribuir al desarrollo del proyecto.

## Flujo de trabajo:

El repositorio normalmente contara con estas 2 ramas iniciales:

```bash
production  -> Versiones estables (Verificadas y comprobadas)
development -> Rama principal de desarrollo que se subiran los cambios realizados
```

***Reglas importantes:***

- Si hay un bug o algo que se deba cambiar, se crea una `issue` y se hace una rama que en el caso de poder, debera de ser verificada por el creador del proyecto, o en su defecto por otro usuario contribuyente al proyecto

- No hacer commits directamente a `production`

## Estilo de commits:

Los commits deberan de ser cortos y representativos (Es mejor hacer muchos commits pequeños que no uno enorme)

Formato a seguir:

```bash
Feat:     <texto> -> Nueva funcionalidad
Fix:      <texto> -> Solucion a error/bug
Refactor: <texto> -> Refactorizacion de interfaz/codigo
Docs:     <texto> -> Documentacion
Build:    <texto> -> Cambios en compilacion o nueva release
Test:     <texto> -> Nuevos tests o cambios en los mismos 
```

## Pull requests:

Requisitos para abrir una PR:

- El proyecto debera de funcionar correctamente antes de ser aceptada

- No hay errores/warnings nuevos

- La funcionalidad/cambio esta aprobada

## Versionado:

Hasta tener una version minima estable seguiremos lanzando versiones ***Alpha*** con el formato:

```bash
Release -> 0.0.1 (Alpha)
Release -> 0.0.2 (Alpha)
Release -> 0.0.3 (Alpha)
Release -> 0.0.4 (Alpha)
```

--- 

Gracias por contribuir.