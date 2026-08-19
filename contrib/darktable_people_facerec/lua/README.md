# lua/

Implementación de la capa de integración con darktable.

Archivo principal:

- `darktable_people_faces.lua`
  - Registro de acciones en lighttable.
  - Exportación temporal de selección.
  - Contrato con backend local.
  - Aplicación de tags por persona y fallback `face/unknown`.

Notas de mantenimiento:

- Mantiene trazabilidad del origen de la base usada (`face_recognition.lua`,
  `selection2collection.lua`).
- Las preferencias del script son autoconfigurables desde preferencias de darktable
  y se pueden ajustar si se sustituye el backend.
