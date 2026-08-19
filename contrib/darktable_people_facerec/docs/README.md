# Documentación del subproyecto

## Contrato de integración con backend

- **Entrada**: `ID`	`PATH` por línea (TSV)
- **Salida**: `ID`	`PERSON`	`CONFIDENCE` por detección
- `_none_` significa sin identidad asignada para esa imagen.

## Referencias y créditos

Este subproyecto cita explícitamente:

- `src/external/lua-scripts/contrib/face_recognition.lua` (orquestación/exportación)
- `src/external/lua-scripts/contrib/selection2collection.lua` (filtrado por reglas de colección)
- `src/common/exif.cc` (`mwg-rs` en darktable) para persistencia de metadatos de regiones
- `src/ai/backend_onnx.c` y `src/ai/backend_common.c` (infraestructura ONNX embebida en darktable)
- DigiKam como referencia de UX de personas y revisión humana.

## Licencias y separación de pesos/código

En cada fase de integración real, separar explícitamente:

- Licencia del código del componente.
- Licencia/condiciones de uso de pesos (`.onnx`, modelos preentrenados).
