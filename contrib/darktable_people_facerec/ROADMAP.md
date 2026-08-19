# ROADMAP

## Fase 0 (bloqueante)

- [ ] Validar `apply_sidecar` + round-trip de `mwg-rs` en flujo de export/import.
- [ ] Confirmar el comportamiento con regiones por cara antes de pasar a producción.

## Fase 1 (MVP Lua)

- [x] Subcarpeta del proyecto creada (`darktable-people-facerec`).
- [x] Script Lua de detección/etiquetado `lua/darktable_people_faces.lua`.
- [x] Backend de contrato estable para integración local (`backend/faces_stub.py`).
- [ ] Sustituir stub por inferencia real (YuNet + SFace/ArcFace).
- [ ] Persistencia de sugerencias manuales (aceptar/rechazar).

## Fase 2 (Integración nativa)

- [ ] Módulo C++ con ONNX Runtime para detección en darkroom y overlays.
- [ ] Escritura de `mwg-rs` por cara con coordenadas del detector.

## Fase 3 (People View)

- [ ] Vista/colección per-person escalable y filtros reutilizables.
- [ ] Publicación OSS y documentación de instalación completa.
