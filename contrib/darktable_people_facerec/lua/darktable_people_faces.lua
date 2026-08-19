--[[
  darktable People Face Recognition (pilot)

  Copyright (c) 2026 Cristóbal (open-source project scaffold)

  This script is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This script is provided as the first usable skeleton of a professional,
  open-source plugin for people-based retrieval in darktable.

  Acknowledged base projects:
  - darktable contrib/face_recognition.lua (Lua action model and XMP flow)
  - darktable contrib/selection2collection.lua (collection filtering by tags)
  - digiKam people workflow (inspiration for UX and review loop)
  - ONNX Runtime + OpenCV ecosystem in the darktable codebase for future native phases
]]

local dt = require "darktable"
local du = require "lib/dtutils"
local dtsys = require "lib/dtutils.system"

local gettext = dt.gettext.gettext
local function _(msgid)
  return gettext(msgid)
end

local MODULE = "darktable_people_faces"
local PS = dt.configuration.running_os == "windows" and "\\" or "/"

-- API guard keeps the script only visible on supported darktable versions.
du.check_min_api_version("7.0.0", MODULE)

local info = debug.getinfo(1, "S")
local script_path = info.source:sub(2)
local script_dir = script_path:match("(.+)[/\\]") or ""

local function q(path)
  if not path then return "\"\"" end
  local escaped = path:gsub('\\', '\\\\'):gsub('"', '\\\"')
  return string.format('"%s"', escaped)
end

-- Preferences (script-scoped)
dt.preferences.register(
  MODULE,
  "backend_command",
  "string",
  MODULE .. " - " .. _("Backend command (python executable)"),
  "python3",
  "python3"
)

dt.preferences.register(
  MODULE,
  "backend_script",
  "string",
  MODULE .. " - " .. _("Backend worker script path"),
  "",
  script_dir .. PS .. ".." .. PS .. "backend" .. PS .. "faces_stub.py"
)

dt.preferences.register(
  MODULE,
  "max_side",
  "integer",
  MODULE .. " - " .. _("Max edge size for exported image"),
  "Maximum edge in pixels used before detection (preserves privacy and speed).",
  1600
)

dt.preferences.register(
  MODULE,
  "min_confidence",
  "float",
  MODULE .. " - " .. _("Minimum confidence"),
  "Minimum score used to accept a face match.",
  0.55
)

dt.preferences.register(
  MODULE,
  "tag_prefix",
  "string",
  MODULE .. " - " .. _("Tag prefix for person identities"),
  "person/",
  "person/"
)

dt.preferences.register(
  MODULE,
  "unknown_tag",
  "string",
  MODULE .. " - " .. _("Fallback tag when no person is detected"),
  "face/unknown",
  "face/unknown"
)

dt.preferences.register(
  MODULE,
  "dry_run",
  "bool",
  MODULE .. " - " .. _("Dry-run mode"),
  "Run without backend, only export/housekeeping and show payload summary.",
  false
)

local function pref_read(name, type, fallback)
  local v = dt.preferences.read(MODULE, name, type)
  if v == nil then return fallback end
  return v
end

local function now_ms()
  return os.time()
end

local function sanitize_tag(raw)
  if not raw then return nil end
  local safe = tostring(raw):gsub("%s+", "_")
  safe = safe:gsub("[\"'`!@#$%%^&*()+=%[%]{};:,<>?/\\|]", "_")
  safe = safe:gsub("_+", "_")
  safe = safe:gsub("^_", "")
  safe = safe:gsub("_$", "")
  if safe == "" then return nil end
  return safe
end

local function build_export_path(image_id)
  local timestamp = now_ms()
  local seq = math.random(1000, 9999)
  return dt.configuration.tmp_dir .. PS .. "dt_people_face_" .. image_id .. "_" .. timestamp .. "_" .. seq .. ".jpg"
end

local function export_selected_images(images)
  local exporter = dt.new_format("jpeg")
  exporter.quality = 85

  local max_side = pref_read("max_side", "integer", 1600)
  exporter.max_width = max_side
  exporter.max_height = max_side

  local upsize = false
  local entries = {}
  local files = {}

  if #images == 0 then
    return entries, files
  end

  for _, img in ipairs(images) do
    if img and img.id and img.filename then
      local path = build_export_path(tostring(img.id))
      local ok = pcall(function()
        exporter:write_image(img, path, upsize)
      end)
      if ok then
        local id = tostring(img.id)
        table.insert(entries, { id = id, image = img, exported = path })
        table.insert(files, path)
      else
        dt.print_log(MODULE .. ": failed export for " .. tostring(img.id))
      end
    end
  end

  return entries, files
end

local function write_batch_input(entries, batch_file)
  local f, err = io.open(batch_file, "w")
  if not f then
    dt.print_error(MODULE .. ": unable to write batch file " .. batch_file .. " (" .. tostring(err) .. ")")
    return false
  end

  for _, item in ipairs(entries) do
    f:write(item.id)
    f:write("\t")
    f:write(item.exported)
    f:write("\n")
  end
  f:close()

  return true
end

local function parse_predictions(path)
  local results = {}
  local f, err = io.open(path, "r")
  if not f then
    dt.print_log(MODULE .. ": no predictions file " .. path .. " (" .. tostring(err) .. ")")
    return results
  end

  for line in f:lines() do
    if line and line:match("%S") then
      local fields = {}
      for token in line:gmatch("([^\t]+)") do
        fields[#fields + 1] = token
      end

      if #fields >= 3 then
        local image_id = fields[1]
        local person = sanitize_tag(fields[2])
        local conf = tonumber(fields[3]) or 0
        if image_id then
          results[image_id] = results[image_id] or {}
          table.insert(results[image_id], { person = person, confidence = conf })
        end
      end
    end
  end
  f:close()

  return results
end

local function run_backend(entries)
  local batch_id = "faces_" .. tostring(now_ms()) .. "_" .. tostring(math.random(1000, 9999))
  local input_path = dt.configuration.tmp_dir .. PS .. batch_id .. "_input.tsv"
  local output_path = dt.configuration.tmp_dir .. PS .. batch_id .. "_output.tsv"

  if not write_batch_input(entries, input_path) then
    return nil
  end

  local cmd_bin = pref_read("backend_command", "string", "python3")
  local script = pref_read("backend_script", "string", script_dir .. PS .. ".." .. PS .. "backend" .. PS .. "faces_stub.py")

  if cmd_bin == nil or cmd_bin == "" or script == nil or script == "" then
    dt.print_error(MODULE .. ": backend command or script is not configured")
    os.remove(input_path)
    return nil
  end

  local threshold = pref_read("min_confidence", "float", 0.55)
  local cmd = string.format("%s %s analyze --input %s --output %s --min-confidence %s", q(cmd_bin), q(script), q(input_path), q(output_path), tostring(threshold))
  dt.print_log(MODULE .. ": invoking " .. cmd)
  local rc = dtsys.external_command(cmd)

  os.remove(input_path)

  if rc ~= 0 then
    os.remove(output_path)
    dt.print_error(MODULE .. ": backend returned code " .. tostring(rc))
    return nil
  end

  return output_path
end

local function cleanup_temp_files(files)
  if not files then return end
  for _, f in ipairs(files) do
    if f and f ~= "" then
      os.remove(f)
    end
  end
end

local function apply_results(entries, predictions)
  local prefix = pref_read("tag_prefix", "string", "person/")
  local unknown = pref_read("unknown_tag", "string", "face/unknown") or "face/unknown"
  local threshold = pref_read("min_confidence", "string", 0.55)
  threshold = tonumber(threshold) or 0.55

  local touched = 0
  local summary = {}

  for _, item in ipairs(entries) do
    local found_tags = {}
    local per_image = predictions[item.id] or {}

    for _, hit in ipairs(per_image) do
      if hit.confidence >= threshold and hit.person and hit.person ~= "" and hit.person ~= "_none_" then
        local full_tag = prefix .. hit.person
        if not found_tags[full_tag] then
          found_tags[full_tag] = true
          local tag = dt.tags.create(full_tag)
          item.image:attach_tag(tag)
        end
      end
    end

    if next(found_tags) == nil then
      local tag = dt.tags.create(unknown)
      item.image:attach_tag(tag)
      summary.unknown = (summary.unknown or 0) + 1
    else
      local count = 0
      for _ in pairs(found_tags) do
        count = count + 1
      end
      summary.matches = (summary.matches or 0) + count
    end

    touched = touched + 1
  end

  return touched, summary
end

local function action_detect(event, images)
  local selected = images or dt.gui.selection()
  if not selected or #selected == 0 then
    dt.print(_("No hay imágenes seleccionadas."))
    return
  end

  if pref_read("dry_run", "bool", false) then
    dt.print(_("Modo simulación activado: se exportan imágenes y no se aplicará reconocimiento."))
  end

  local entries, temp_files = export_selected_images(selected)
  if #entries == 0 then
    dt.print_error(_("No se pudo exportar ninguna imagen."))
    cleanup_temp_files(temp_files)
    return
  end

  local predictions = {}
  if pref_read("dry_run", "bool", false) then
    for _, item in ipairs(entries) do
      predictions[item.id] = {
        { person = nil, confidence = 0 }
      }
    end
  else
    local output_path = run_backend(entries)
    if output_path then
      predictions = parse_predictions(output_path)
      table.insert(temp_files, output_path)
    else
      cleanup_temp_files(temp_files)
      return
    end
  end

  local touched, summary = apply_results(entries, predictions)
  cleanup_temp_files(temp_files)

  dt.print_log(
    MODULE ..
    ": processed=" .. tostring(touched) ..", unknown=" .. tostring(summary.unknown or 0) ..", matched=" .. tostring(summary.matches or 0)
  )
  dt.print(_("Reconocimiento de personas completado"))
end

local function action_filter_by_first_person(event, images)
  local selected = images or dt.gui.selection()
  if not selected or #selected == 0 then
    dt.print(_("Selecciona una imagen con etiquetas de persona."))
    return
  end

  local image = selected[1]
  local prefix = pref_read("tag_prefix", "string", "person/")
  local person_tags = {}

  for _, tag in ipairs(image:get_tags() or {}) do
    if prefix == "" or tag.name:sub(1, #prefix) == prefix then
      table.insert(person_tags, tag)
    end
  end

  if #person_tags == 0 then
    dt.print(_("La imagen no tiene etiqueta de persona."))
    return
  end

  local rules = dt.gui.libs.collect.filter()
  local rule = dt.gui.libs.collect.new_rule()
  rule.mode = "DT_LIB_COLLECT_MODE_AND"
  rule.data = person_tags[1].name
  rule.item = "DT_COLLECTION_PROP_TAG"
  table.insert(rules, rule)

  dt.gui.libs.collect.filter(rules)
  dt.print(_("Colección filtrada por etiqueta: ") .. person_tags[1].name)
end

-- Register UI actions in lighttable image context.
local ACTION_ID_RECOGNIZE = MODULE .. ":recognize"
dt.gui.libs.image.register_action(
  ACTION_ID_RECOGNIZE,
  _("Reconocer personas"),
  action_detect,
  _("Exporta la selección, llama al backend local y etiqueta personas por cara.")
)

local ACTION_ID_FILTER = MODULE .. ":filter"
dt.gui.libs.image.register_action(
  ACTION_ID_FILTER,
  _("Filtrar por primera persona"),
  action_filter_by_first_person,
  _("Filtra colección actual por la primera etiqueta de persona de la imagen.")
)

local script_data = {}

script_data.metadata = {
  name = _("People Faces (pilot)"),
  purpose = _("Tag and retrieve people in darktable via local facial recognition workflow."),
  author = "Cristóbal, with OSS foundations",
  help = "local folder at workspace/darktable-people-facerec/lua/darktable_people_faces.lua"
}

local function destroy()
  dt.gui.libs.image.destroy_action(ACTION_ID_RECOGNIZE)
  dt.gui.libs.image.destroy_action(ACTION_ID_FILTER)
end

script_data.destroy = destroy

math.randomseed(os.time())

return script_data
