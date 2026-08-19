# Decisiones técnicas iniciales

- Fase 1 se apoya en Lua + backend local para poder tener valor útil desde hoy.
- Se usa un contrato estable de entrada/salida para el motor:
  - Entrada: `image_id<TAB>path`
  - Salida: `image_id<TAB>person_or_none<TAB>confidence`
- El backend real se desacopla de la UI de darktable: por ahora la capa Lua solo
  transforma selección -> lotes y aplica etiquetas cuando la detección devuelve un resultado.
- Se prioriza `face/unknown` como fallback y prefijo configurable de tags (`person/`).
- La capa piloto **no** intenta dibujar overlays en darkroom (eso requiere fase nativa en C).
- Trazabilidad explícita en código y docs: referencias a `contrib/face_recognition.lua`,
  `selection2collection.lua` y al ecosistema ONNX/face tooling OSS.
