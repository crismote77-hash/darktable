# Backend local (pilot)

Este backend es un stub de desarrollo para validar el contrato entre Lua y
el motor de inferencia.

## Contrato mínimo usado por `lua/darktable_people_faces.lua`

Entrada (`--input`): fichero TSV con una línea por imagen:

- `<image_id>\t<exported_image_path>`

Salida (`--output`): una o varias líneas por imagen con el formato:

- `<image_id>\t<person_or__none_>\t<confidence>`

La capa Lua acepta múltiples detecciones y agrega solo las que superen
`min_confidence`.

`_none_` indica que no hay persona reconocida para esa imagen y la capa
Lua aplica la etiqueta de "desconocido".

## Sustitución por motor real (fase 1+) 

El stub se puede sustituir por un procesador real sin tocar el contrato:

- Mantener flags CLI: `analyze --input <tsv> --output <tsv> --min-confidence X`
- Escribir una fila por detección detectada para cada `image_id`

Cuando se integre detección + embeddings reales, mantener este protocolo para
que la integración en Lua no cambie.
