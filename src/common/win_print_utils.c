/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/win_print_utils.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wingdi.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#define DT_WIN_PRINT_MAX_BAND_BYTES (32u * 1024u * 1024u)

static GPrivate _abort_state = G_PRIVATE_INIT(NULL);

gboolean dt_win_print_store_printer_name(char *destination,
                                         const gsize destination_size,
                                         const char *printer_name)
{
  if(destination && destination_size > 0) destination[0] = '\0';
  if(!destination || destination_size == 0 || !printer_name) return FALSE;

  glong utf16_code_units = 0;
  gunichar2 *wide_name = g_utf8_to_utf16(printer_name, -1, NULL,
                                         &utf16_code_units, NULL);
  if(!wide_name) return FALSE;
  g_free(wide_name);

  const gsize utf8_bytes = strlen(printer_name);
  if(utf16_code_units > DT_PRINT_MAX_PRINTER_NAME_WCHARS - 1
     || utf8_bytes >= destination_size)
    return FALSE;

  memcpy(destination, printer_name, utf8_bytes + 1);
  return TRUE;
}

gboolean dt_win_print_store_wide_slot_name(char *destination,
                                           const gsize destination_size,
                                           const wchar_t *slot,
                                           const gsize slot_characters)
{
  if(destination && destination_size > 0) destination[0] = '\0';
  if(!destination || destination_size == 0 || !slot || slot_characters == 0)
    return FALSE;

  gsize length = 0;
  while(length < slot_characters && slot[length])
    length++;

  glong bytes_written = 0;
  char *utf8 = g_utf16_to_utf8((const gunichar2 *)slot, (glong)length,
                               NULL, &bytes_written, NULL);
  if(!utf8 || bytes_written < 0 || (gsize)bytes_written >= destination_size)
  {
    g_free(utf8);
    return FALSE;
  }

  memcpy(destination, utf8, (gsize)bytes_written + 1);
  g_free(utf8);
  return TRUE;
}

gboolean dt_win_print_page_size_mm(const int physical_width,
                                   const int physical_height,
                                   const int dpi_x,
                                   const int dpi_y,
                                   double *width_mm,
                                   double *height_mm)
{
  if(physical_width <= 0 || physical_height <= 0 || dpi_x <= 0 || dpi_y <= 0
     || !width_mm || !height_mm)
    return FALSE;

  const double width = (double)physical_width / (double)dpi_x * 25.4;
  const double height = (double)physical_height / (double)dpi_y * 25.4;
  if(!isfinite(width) || !isfinite(height) || width <= 0.0 || height <= 0.0)
    return FALSE;

  *width_mm = width;
  *height_mm = height;
  return TRUE;
}

gboolean dt_win_print_map_intent(const int intent,
                                 guint32 *bitmap_intent,
                                 guint32 *devmode_intent)
{
  if(!bitmap_intent || !devmode_intent) return FALSE;

  switch(intent)
  {
    case INTENT_PERCEPTUAL:
      *bitmap_intent = LCS_GM_IMAGES;
      *devmode_intent = DMICM_CONTRAST;
      return TRUE;
    case INTENT_RELATIVE_COLORIMETRIC:
      *bitmap_intent = LCS_GM_GRAPHICS;
      *devmode_intent = DMICM_COLORIMETRIC;
      return TRUE;
    case INTENT_SATURATION:
      *bitmap_intent = LCS_GM_BUSINESS;
      *devmode_intent = DMICM_SATURATE;
      return TRUE;
    case INTENT_ABSOLUTE_COLORIMETRIC:
      *bitmap_intent = LCS_GM_ABS_COLORIMETRIC;
      *devmode_intent = DMICM_ABS_COLORIMETRIC;
      return TRUE;
    default:
      return FALSE;
  }
}

gboolean dt_win_print_get_band_layout(const int width,
                                      const int height,
                                      gsize *source_row_bytes,
                                      int *band_rows,
                                      gsize *band_bytes)
{
  if(width <= 0 || height <= 0 || !source_row_bytes || !band_rows || !band_bytes)
    return FALSE;

  if((gsize)width > G_MAXSIZE / 4 || (gsize)width > G_MAXSIZE / 3)
    return FALSE;

  const gsize source_bytes = (gsize)width * 3;
  const gsize dib_row_bytes = (gsize)width * 4;
  if(dib_row_bytes == 0 || dib_row_bytes > DT_WIN_PRINT_MAX_BAND_BYTES)
    return FALSE;
  if((gsize)height > G_MAXSIZE / source_bytes)
    return FALSE;

  const gsize rows = MIN((gsize)height,
                         (gsize)DT_WIN_PRINT_MAX_BAND_BYTES / dib_row_bytes);
  if(rows == 0 || rows > G_MAXSIZE / dib_row_bytes || rows > G_MAXINT)
    return FALSE;

  *source_row_bytes = source_bytes;
  *band_rows = (int)rows;
  *band_bytes = rows * dib_row_bytes;
  return TRUE;
}

gboolean dt_win_print_get_bitmap_header_size(const gsize profile_size,
                                             gsize *header_size)
{
  if(!header_size || profile_size == 0 || profile_size > UINT32_MAX
     || profile_size > G_MAXSIZE - sizeof(BITMAPV5HEADER))
    return FALSE;

  *header_size = sizeof(BITMAPV5HEADER) + profile_size;
  return TRUE;
}

gboolean dt_win_print_devmode_layout_valid(const DEVMODEW *devmode,
                                           const gsize allocation_size)
{
  const gsize minimum_size = G_STRUCT_OFFSET(DEVMODEW, dmFields)
                             + sizeof(devmode->dmFields);
  if(!devmode || allocation_size < minimum_size
     || devmode->dmSize < minimum_size
     || devmode->dmSize > allocation_size)
    return FALSE;

  return devmode->dmDriverExtra <= allocation_size - devmode->dmSize;
}

gboolean dt_win_print_devmode_field_available(const DEVMODEW *devmode,
                                              const gsize allocation_size,
                                              const gsize field_offset,
                                              const gsize field_size)
{
  return dt_win_print_devmode_layout_valid(devmode, allocation_size)
         && field_offset <= devmode->dmSize
         && field_size <= devmode->dmSize - field_offset;
}

guint32 dt_win_print_select_icm_method(const gboolean darktable_managed,
                                        const guint32 driver_method)
{
  return darktable_managed ? DMICMMETHOD_NONE : driver_method;
}

gboolean dt_win_print_set_paper_fields(DEVMODEW *devmode,
                                       const gsize allocation_size,
                                       const dt_win_print_paper_mode_t mode,
                                       const short paper_size,
                                       const short paper_width,
                                       const short paper_length)
{
  if(!dt_win_print_devmode_layout_valid(devmode, allocation_size)) return FALSE;
  if(mode == DT_WIN_PRINT_PAPER_DEFAULT) return TRUE;

  if(mode == DT_WIN_PRINT_PAPER_STOCK)
  {
    if(!dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperSize),
         sizeof(devmode->dmPaperSize)))
      return FALSE;

    devmode->dmFields &= ~(DM_PAPERWIDTH | DM_PAPERLENGTH | DM_FORMNAME);
    devmode->dmFields |= DM_PAPERSIZE;
    devmode->dmPaperSize = paper_size;
    if(dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperWidth),
         sizeof(devmode->dmPaperWidth)))
      devmode->dmPaperWidth = 0;
    if(dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperLength),
         sizeof(devmode->dmPaperLength)))
      devmode->dmPaperLength = 0;
    if(dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmFormName),
         sizeof(devmode->dmFormName)))
      memset(devmode->dmFormName, 0, sizeof(devmode->dmFormName));
  }
  else if(mode == DT_WIN_PRINT_PAPER_CUSTOM)
  {
    if(!dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperWidth),
         sizeof(devmode->dmPaperWidth))
       || !dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperLength),
         sizeof(devmode->dmPaperLength)))
      return FALSE;

    devmode->dmFields &= ~(DM_PAPERSIZE | DM_FORMNAME);
    devmode->dmFields |= DM_PAPERWIDTH | DM_PAPERLENGTH;
    if(dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperSize),
         sizeof(devmode->dmPaperSize)))
      devmode->dmPaperSize = 0;
    devmode->dmPaperWidth = paper_width;
    devmode->dmPaperLength = paper_length;
    if(dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmFormName),
         sizeof(devmode->dmFormName)))
      memset(devmode->dmFormName, 0, sizeof(devmode->dmFormName));
  }
  else
    return FALSE;

  return TRUE;
}

gboolean dt_win_print_validate_paper_fields(const DEVMODEW *devmode,
                                             const gsize allocation_size,
                                             const dt_win_print_paper_mode_t mode,
                                             const short requested_paper_size,
                                             const short requested_paper_width,
                                             const short requested_paper_length,
                                             const char *requested_form_name)
{
  if(!dt_win_print_devmode_layout_valid(devmode, allocation_size)) return FALSE;
  if(mode == DT_WIN_PRINT_PAPER_DEFAULT) return TRUE;

  if(mode == DT_WIN_PRINT_PAPER_STOCK)
  {
    if(!dt_win_print_devmode_field_available(
       devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperSize),
          sizeof(devmode->dmPaperSize))
       || !(devmode->dmFields & DM_PAPERSIZE)
       || devmode->dmPaperSize != requested_paper_size
       || (devmode->dmFields & (DM_PAPERWIDTH | DM_PAPERLENGTH)))
      return FALSE;

    if(devmode->dmFields & DM_FORMNAME)
    {
      if(requested_paper_width <= 0 || requested_paper_length <= 0
         || !requested_form_name || !*requested_form_name
         || !dt_win_print_devmode_field_available(
              devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperWidth),
              sizeof(devmode->dmPaperWidth))
         || !dt_win_print_devmode_field_available(
              devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperLength),
              sizeof(devmode->dmPaperLength))
         || !dt_win_print_devmode_field_available(
              devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmFormName),
              sizeof(devmode->dmFormName))
         || devmode->dmPaperWidth != requested_paper_width
         || devmode->dmPaperLength != requested_paper_length
         || !wmemchr(devmode->dmFormName, L'\0', G_N_ELEMENTS(devmode->dmFormName)))
        return FALSE;

      glong form_units = 0;
      gunichar2 *requested_form = g_utf8_to_utf16(requested_form_name, -1,
                                                  NULL, &form_units, NULL);
      const gboolean form_matches = requested_form && form_units > 0
                                    && form_units
                                         < (glong)G_N_ELEMENTS(devmode->dmFormName)
                                    && !wcscmp(devmode->dmFormName,
                                               (const wchar_t *)requested_form);
      g_free(requested_form);
      if(!form_matches) return FALSE;
    }
  }
  else if(mode == DT_WIN_PRINT_PAPER_CUSTOM)
  {
    if(!dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperWidth),
         sizeof(devmode->dmPaperWidth))
       || !dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmPaperLength),
         sizeof(devmode->dmPaperLength))
       || !(devmode->dmFields & DM_PAPERWIDTH)
       || !(devmode->dmFields & DM_PAPERLENGTH)
       || devmode->dmPaperWidth != requested_paper_width
       || devmode->dmPaperLength != requested_paper_length
       || (devmode->dmFields & (DM_PAPERSIZE | DM_FORMNAME)))
      return FALSE;
  }
  else
    return FALSE;

  return TRUE;
}

gboolean dt_win_print_validate_copies(const DEVMODEW *devmode,
                                      const gsize allocation_size)
{
  return dt_win_print_devmode_field_available(
           devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmCopies),
           sizeof(devmode->dmCopies))
         && (devmode->dmFields & DM_COPIES)
         && devmode->dmCopies == 1;
}

gboolean dt_win_print_validate_color_fields(const DEVMODEW *devmode,
                                             const gsize allocation_size,
                                             const guint32 requested_fields,
                                             const guint32 requested_method,
                                             const guint32 requested_intent)
{
  if(!dt_win_print_devmode_layout_valid(devmode, allocation_size)) return FALSE;

#ifdef DM_ICMMETHOD
  if(devmode->dmFields & DM_ICMMETHOD)
  {
    if(!(requested_fields & DM_ICMMETHOD)
       || !dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmICMMethod),
         sizeof(devmode->dmICMMethod))
       || devmode->dmICMMethod != requested_method)
      return FALSE;
  }
#else
  (void)requested_method;
#endif

#ifdef DM_ICMINTENT
  if(devmode->dmFields & DM_ICMINTENT)
  {
    if(!(requested_fields & DM_ICMINTENT)
       || !dt_win_print_devmode_field_available(
         devmode, allocation_size, G_STRUCT_OFFSET(DEVMODEW, dmICMIntent),
         sizeof(devmode->dmICMIntent))
       || devmode->dmICMIntent != requested_intent)
      return FALSE;
  }
#else
  (void)requested_intent;
#endif

  return TRUE;
}

gboolean dt_win_print_normalize_page_size(double *width, double *height)
{
  if(!width || !height || !isfinite(*width) || !isfinite(*height)
     || *width <= 0.0 || *height <= 0.0)
    return FALSE;

  if(*width > *height)
  {
    const double swap = *width;
    *width = *height;
    *height = swap;
  }
  return TRUE;
}

gboolean dt_win_print_margins_to_landscape(double *left,
                                            double *top,
                                            double *right,
                                            double *bottom)
{
  if(!left || !top || !right || !bottom) return FALSE;

  const double portrait_left = *left;
  const double portrait_top = *top;
  const double portrait_right = *right;
  const double portrait_bottom = *bottom;
  *left = portrait_top;
  *top = portrait_right;
  *right = portrait_bottom;
  *bottom = portrait_left;
  return TRUE;
}

gboolean dt_win_print_margins_to_portrait(double *left,
                                           double *top,
                                           double *right,
                                           double *bottom)
{
  if(!left || !top || !right || !bottom) return FALSE;

  const double landscape_left = *left;
  const double landscape_top = *top;
  const double landscape_right = *right;
  const double landscape_bottom = *bottom;
  *left = landscape_bottom;
  *top = landscape_left;
  *right = landscape_top;
  *bottom = landscape_right;
  return TRUE;
}

gboolean dt_win_print_output_path_available(const wchar_t *path,
                                             gulong *error_code)
{
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!path || !*path)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return FALSE;
  }

  if(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
  {
    if(error_code) *error_code = ERROR_FILE_EXISTS;
    return FALSE;
  }

  const DWORD code = GetLastError();
  if(code == ERROR_FILE_NOT_FOUND)
    return TRUE;

  if(error_code) *error_code = code ? code : ERROR_INVALID_DATA;
  return FALSE;
}

gboolean dt_win_print_prepare_output_path(const wchar_t *destination,
                                          wchar_t **temporary_path,
                                          gulong *error_code)
{
  if(temporary_path) *temporary_path = NULL;
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!destination || !*destination || !temporary_path)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return FALSE;
  }

  if(!dt_win_print_output_path_available(destination, error_code)) return FALSE;

  const wchar_t *separator = wcsrchr(destination, L'\\');
  const wchar_t *slash = wcsrchr(destination, L'/');
  if(slash && (!separator || slash > separator)) separator = slash;
  if(!separator)
  {
    if(error_code) *error_code = ERROR_PATH_NOT_FOUND;
    return FALSE;
  }

  const gsize directory_length = (gsize)(separator - destination) + 1;
  const gsize capacity = directory_length + 80;
  wchar_t *candidate = g_new0(wchar_t, capacity);
  wmemcpy(candidate, destination, directory_length);

  for(int attempt = 0; attempt < 64; attempt++)
  {
    char *uuid = g_uuid_string_random();
    gunichar2 *uuid_wide = g_utf8_to_utf16(uuid, -1, NULL, NULL, NULL);
    g_free(uuid);
    if(!uuid_wide)
    {
      g_free(candidate);
      if(error_code) *error_code = ERROR_INVALID_DATA;
      return FALSE;
    }
    swprintf(candidate + directory_length, capacity - directory_length,
              L".darktable-print-%ls.pdf", (const wchar_t *)uuid_wide);
    g_free(uuid_wide);

    HANDLE reservation = CreateFileW(candidate, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                     FILE_ATTRIBUTE_TEMPORARY, NULL);
    if(reservation != INVALID_HANDLE_VALUE)
    {
      if(!CloseHandle(reservation))
      {
        const DWORD code = GetLastError();
        if(DeleteFileW(candidate))
          g_free(candidate);
        else
          *temporary_path = candidate;
        if(error_code) *error_code = code ? code : ERROR_WRITE_FAULT;
        return FALSE;
      }
      if(!DeleteFileW(candidate))
      {
        const DWORD code = GetLastError();
        *temporary_path = candidate;
        if(error_code) *error_code = code ? code : ERROR_ACCESS_DENIED;
        return FALSE;
      }

      *temporary_path = candidate;
      return TRUE;
    }

    const DWORD code = GetLastError();
    if(code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
    {
      g_free(candidate);
      if(error_code) *error_code = code ? code : ERROR_CANNOT_MAKE;
      return FALSE;
    }
  }

  g_free(candidate);
  if(error_code) *error_code = ERROR_FILE_EXISTS;
  return FALSE;
}

gboolean dt_win_print_publish_output(const wchar_t *temporary_path,
                                     const wchar_t *destination,
                                     gulong *error_code)
{
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!temporary_path || !*temporary_path || !destination || !*destination)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return FALSE;
  }

  if(MoveFileExW(temporary_path, destination, MOVEFILE_WRITE_THROUGH)) return TRUE;

  const DWORD code = GetLastError();
  if(error_code) *error_code = code ? code : ERROR_WRITE_FAULT;
  return FALSE;
}

static gboolean _dt_win_print_pdf_tail_complete(const char *tail,
                                                 const gsize tail_size)
{
  gsize end = tail_size;
  while(end > 0 && (tail[end - 1] == ' ' || tail[end - 1] == '\t'
                    || tail[end - 1] == '\r' || tail[end - 1] == '\n'
                    || tail[end - 1] == '\f' || tail[end - 1] == '\v'))
    end--;

  return end >= 5 && memcmp(tail + end - 5, "%%EOF", 5) == 0;
}

dt_win_print_output_probe_result_t dt_win_print_probe_output(
  const wchar_t *path,
  guint64 *size,
  gulong *error_code,
  gpointer user_data)
{
  (void)user_data;
  if(size) *size = 0;
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!path || !*path)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
  }

  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if(file == INVALID_HANDLE_VALUE)
  {
    const DWORD code = GetLastError();
    if(error_code) *error_code = code ? code : ERROR_READ_FAULT;
    if(code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
      return DT_WIN_PRINT_OUTPUT_PROBE_ABSENT;
    if(code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION
       || code == ERROR_ACCESS_DENIED)
      return DT_WIN_PRINT_OUTPUT_PROBE_INCOMPLETE;
    return DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
  }

  dt_win_print_output_probe_result_t result = DT_WIN_PRINT_OUTPUT_PROBE_INCOMPLETE;
  DWORD result_error = ERROR_HANDLE_EOF;
  LARGE_INTEGER size_before = { 0 };
  LARGE_INTEGER size_after = { 0 };
  if(!GetFileSizeEx(file, &size_before))
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = GetLastError();
    goto finish;
  }
  if(size_before.QuadPart <= 0) goto finish;
  if(size) *size = (guint64)size_before.QuadPart;

  LARGE_INTEGER offset = { 0 };
  if(!SetFilePointerEx(file, offset, NULL, FILE_BEGIN))
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = GetLastError();
    goto finish;
  }

  char header[5] = { 0 };
  DWORD bytes_read = 0;
  if(!ReadFile(file, header, sizeof(header), &bytes_read, NULL))
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = GetLastError();
    goto finish;
  }
  if(bytes_read < sizeof(header)) goto finish;
  if(memcmp(header, "%PDF-", sizeof(header)) != 0)
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_INVALID;
    result_error = ERROR_INVALID_DATA;
    goto finish;
  }

  const DWORD tail_size = (DWORD)MIN(size_before.QuadPart, (LONGLONG)4096);
  char *tail = g_try_malloc(tail_size);
  if(!tail)
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = ERROR_NOT_ENOUGH_MEMORY;
    goto finish;
  }
  offset.QuadPart = size_before.QuadPart - tail_size;
  if(!SetFilePointerEx(file, offset, NULL, FILE_BEGIN)
     || !ReadFile(file, tail, tail_size, &bytes_read, NULL))
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = GetLastError();
    g_free(tail);
    goto finish;
  }
  const gboolean has_terminal_eof = bytes_read == tail_size
                                    && _dt_win_print_pdf_tail_complete(tail, tail_size);
  g_free(tail);

  if(!GetFileSizeEx(file, &size_after))
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = GetLastError();
    goto finish;
  }
  if(size_before.QuadPart != size_after.QuadPart || !has_terminal_eof)
    goto finish;

  result = DT_WIN_PRINT_OUTPUT_PROBE_READY;
  result_error = ERROR_SUCCESS;

finish:
  if(!CloseHandle(file))
  {
    result = DT_WIN_PRINT_OUTPUT_PROBE_ERROR;
    result_error = GetLastError();
  }
  if(error_code) *error_code = result_error ? result_error : ERROR_SUCCESS;
  return result;
}

dt_win_print_output_wait_result_t dt_win_print_wait_for_output_with_ops(
  const wchar_t *path,
  const guint max_probes,
  const guint delay_milliseconds,
  dt_win_print_output_probe_t probe_operation,
  dt_win_print_sleep_t sleep_operation,
  gpointer user_data,
  guint64 *stable_size,
  gulong *error_code)
{
  if(stable_size) *stable_size = 0;
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!path || !*path || max_probes == 0 || !probe_operation
     || (max_probes > 1 && !sleep_operation))
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return DT_WIN_PRINT_OUTPUT_WAIT_ERROR;
  }

  gboolean previous_ready = FALSE;
  guint64 previous_ready_size = 0;
  for(guint probe_index = 0; probe_index < max_probes; probe_index++)
  {
    guint64 current_size = 0;
    gulong current_error = ERROR_SUCCESS;
    const dt_win_print_output_probe_result_t probe_result =
      probe_operation(path, &current_size, &current_error, user_data);
    if(stable_size) *stable_size = current_size;
    if(error_code) *error_code = current_error;

    if(probe_result == DT_WIN_PRINT_OUTPUT_PROBE_READY)
    {
      if(previous_ready && current_size > 0 && current_size == previous_ready_size)
      {
        if(error_code) *error_code = ERROR_SUCCESS;
        return DT_WIN_PRINT_OUTPUT_WAIT_READY;
      }
      previous_ready = TRUE;
      previous_ready_size = current_size;
    }
    else
      previous_ready = FALSE;

    if(probe_result == DT_WIN_PRINT_OUTPUT_PROBE_INVALID)
      return DT_WIN_PRINT_OUTPUT_WAIT_INVALID;
    if(probe_result == DT_WIN_PRINT_OUTPUT_PROBE_ERROR)
      return DT_WIN_PRINT_OUTPUT_WAIT_ERROR;

    if(probe_index + 1 < max_probes)
      sleep_operation(delay_milliseconds, user_data);
  }

  if(error_code && *error_code == ERROR_SUCCESS) *error_code = ERROR_TIMEOUT;
  return DT_WIN_PRINT_OUTPUT_WAIT_TIMEOUT;
}

static void _dt_win_print_sleep(const guint milliseconds, gpointer user_data)
{
  (void)user_data;
  g_usleep((gulong)milliseconds * 1000u);
}

dt_win_print_output_wait_result_t dt_win_print_wait_for_output(
  const wchar_t *path,
  guint64 *stable_size,
  gulong *error_code)
{
  return dt_win_print_wait_for_output_with_ops(
    path, 41, 250, dt_win_print_probe_output, _dt_win_print_sleep, NULL,
    stable_size, error_code);
}

gboolean dt_win_print_output_requires_committed_cleanup(
  const gboolean redirected_output,
  const gboolean document_started,
  const gboolean end_doc_succeeded,
  const gboolean abort_doc_succeeded)
{
  return redirected_output
         && (end_doc_succeeded
             || (document_started && !abort_doc_succeeded));
}

static gboolean _dt_win_print_delete_file(const wchar_t *path,
                                          gulong *error_code)
{
  if(error_code) *error_code = ERROR_SUCCESS;
  if(DeleteFileW(path)) return TRUE;

  const DWORD code = GetLastError();
  if(code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return TRUE;
  if(error_code) *error_code = code ? code : ERROR_ACCESS_DENIED;
  return FALSE;
}

static gboolean _dt_win_print_schedule_delete(const wchar_t *path,
                                              gulong *error_code)
{
  if(error_code) *error_code = ERROR_SUCCESS;
  if(MoveFileExW(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) return TRUE;

  const DWORD code = GetLastError();
  if(error_code) *error_code = code ? code : ERROR_ACCESS_DENIED;
  return FALSE;
}

static gboolean _dt_win_print_file_presence(const wchar_t *path,
                                             gboolean *present,
                                             gulong *error_code)
{
  if(present) *present = FALSE;
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!path || !*path || !present)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return FALSE;
  }

  if(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
  {
    *present = TRUE;
    return TRUE;
  }

  const DWORD code = GetLastError();
  if(code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
  {
    if(error_code) *error_code = code;
    return TRUE;
  }

  if(error_code) *error_code = code ? code : ERROR_INVALID_DATA;
  return FALSE;
}

dt_win_print_temp_cleanup_result_t dt_win_print_cleanup_output_temp_with_ops(
  const wchar_t *temporary_path,
  dt_win_print_file_operation_t delete_operation,
  dt_win_print_file_operation_t schedule_operation,
  gulong *error_code)
{
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!temporary_path || !*temporary_path) return DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE;
  if(!delete_operation || !schedule_operation)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED;
  }

  gulong delete_error = ERROR_SUCCESS;
  if(delete_operation(temporary_path, &delete_error))
    return DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE;

  gulong schedule_error = ERROR_SUCCESS;
  const gboolean scheduled = schedule_operation(temporary_path, &schedule_error);
  if(error_code)
    *error_code = delete_error ? delete_error
                               : (schedule_error ? schedule_error : ERROR_ACCESS_DENIED);
  return scheduled ? DT_WIN_PRINT_TEMP_CLEANUP_SCHEDULED
                   : DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED;
}

dt_win_print_temp_cleanup_result_t dt_win_print_cleanup_output_temp(
  const wchar_t *temporary_path,
  gulong *error_code)
{
  return dt_win_print_cleanup_output_temp_with_ops(
    temporary_path, _dt_win_print_delete_file, _dt_win_print_schedule_delete,
    error_code);
}

dt_win_print_temp_cleanup_result_t
dt_win_print_cleanup_committed_output_temp_with_ops(
  const wchar_t *temporary_path,
  dt_win_print_file_presence_operation_t presence_operation,
  dt_win_print_file_operation_t delete_operation,
  dt_win_print_file_operation_t schedule_operation,
  gulong *error_code)
{
  if(error_code) *error_code = ERROR_SUCCESS;
  if(!temporary_path || !*temporary_path || !presence_operation
     || !delete_operation || !schedule_operation)
  {
    if(error_code) *error_code = ERROR_INVALID_PARAMETER;
    return DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED;
  }

  gboolean present = FALSE;
  gulong presence_error = ERROR_SUCCESS;
  if(!presence_operation(temporary_path, &present, &presence_error) || !present)
  {
    if(error_code)
      *error_code = presence_error ? presence_error : ERROR_FILE_NOT_FOUND;
    return DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED;
  }

  return dt_win_print_cleanup_output_temp_with_ops(
    temporary_path, delete_operation, schedule_operation, error_code);
}

dt_win_print_temp_cleanup_result_t dt_win_print_cleanup_committed_output_temp(
  const wchar_t *temporary_path,
  gulong *error_code)
{
  return dt_win_print_cleanup_committed_output_temp_with_ops(
    temporary_path, _dt_win_print_file_presence, _dt_win_print_delete_file,
    _dt_win_print_schedule_delete, error_code);
}

void dt_win_print_set_abort_state(gpointer state)
{
  g_private_set(&_abort_state, state);
}

gpointer dt_win_print_get_abort_state(void)
{
  return g_private_get(&_abort_state);
}

#endif // _WIN32
