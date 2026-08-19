# darktable People Faces (subproyecto OSS)

Prototipo OSS para recuperar personas por reconocimiento facial en darktable
con capa Lua (MVP) + backend local desacoplado.

### Qué ya existe

- Carpeta del plugin: `lua/darktable_people_faces.lua`
- Acción principal de reconocimiento para selección en Lighttable.
- Backend de desarrollo `backend/faces_stub.py` con interfaz estable (TSV de entrada/salida).
- Plantilla de requisitos `backend/requirements.txt` para el motor real en fases posteriores.
- Contrato de integración detallado y decisiones técnicas en `docs/DECISIONS.md`.

### Instalación de prueba

1. Copia este directorio en una ruta accesible por tu `luarc`.
2. En `luarc`, añade:

```lua
require "darktable_people_faces"
```

3. Reinicia darktable.
4. En Lighttable, selecciona imágenes y lanza la acción `Reconocer personas`.

### Uso inicial

- Selecciona imágenes en Lighttable.
- Ejecuta la acción `Reconocer personas`.
- Revisa/ajusta etiquetas `person/*` creadas.

Notas rápidas:

- El stub (`faces_stub.py`) actualmente marca ausencia con `_none_` cuando no puede sugerir persona.
- El filtro por persona usa la etiqueta `person/*` más confiable de la imagen y la aplica como filtro de colección.

### Trazabilidad de base

El código incluye créditos explícitos a proyectos reutilizados o tomados como patrón:

- `src/external/lua-scripts/contrib/face_recognition.lua` (estructura y flujo base Lua).
- `src/external/lua-scripts/contrib/selection2collection.lua` (filtrado por etiqueta).
- `src/external/lua-scripts/contrib/README` (convención y organización del directorio oficial de scripts).
- ecosistema interno de darktable: `mwg-rs` sidecar y APIs Lua.
- Proyectos OSS de referencia en visión facial (p. ej. DigiKam, OpenCV/ONNX Runtime).
