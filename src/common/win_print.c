/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/win_print.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wingdi.h>
#include <winspool.h>

#include "common/darktable.h"
#include "common/debug.h"
#include "common/print_backend_utils.h"
#include "common/printing.h"
#include "common/win_print_utils.h"
#include "control/control.h"
#include "control/jobs.h"

#include <errno.h>
#include <limits.h>
#include <math.h>

typedef enum dt_win_print_error_t
{
  DT_WIN_PRINT_ERROR_FAILED
} dt_win_print_error_t;

typedef struct dt_win_prtctl_t
{
  void (*cb)(dt_printer_info_t *, void *);
  void *user_data;
} dt_win_prtctl_t;

typedef struct dt_win_print_job_state_t
{
  dt_job_t *job;
  HDC hdc;
  volatile LONG cancel_requested;
  volatile LONG started_doc;
} dt_win_print_job_state_t;

static volatile LONG _discovery_cancel = 0;

// wingdi.h defines PROFILE_EMBEDDED as 'MBED', which triggers -Wmultichar
// under GCC when darktable builds with -Werror.
#define DT_WIN_PROFILE_EMBEDDED ((DWORD)0x4d424544u)

static GQuark _dt_win_print_error_quark(void)
{
  return g_quark_from_static_string("dt-win-print-error-quark");
}

static void _dt_win_set_error(GError **error,
                              const char *operation,
                              const char *printer_name,
                              const DWORD code)
{
  g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
              _("%s failed for printer `%s' (Windows error %lu)"),
              operation, printer_name ? printer_name : "", (unsigned long)code);
}

static wchar_t *_dt_win_utf8_to_wide(const char *s)
{
  if(!s) return NULL;
  glong items_written = 0;
  return (wchar_t *)g_utf8_to_utf16(s, -1, NULL, &items_written, NULL);
}

static char *_dt_win_wide_to_utf8(const wchar_t *s)
{
  if(!s) return NULL;
  glong items_written = 0;
  return g_utf16_to_utf8((const gunichar2 *)s, -1, NULL, &items_written, NULL);
}

static gboolean _dt_win_job_cancelled(dt_job_t *job)
{
  return job && dt_control_job_get_state(job) == DT_JOB_STATE_CANCELLED;
}

static gboolean _dt_win_cancel_requested(dt_job_t *job,
                                         dt_win_print_job_state_t *state)
{
  return _dt_win_job_cancelled(job)
         || (state && InterlockedCompareExchange(&state->cancel_requested, 0, 0));
}

static void _dt_win_set_cancelled_error(GError **error,
                                        const char *printer_name)
{
  g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
              _("printing on `%s' cancelled"), printer_name ? printer_name : "");
}

static void _dt_win_append_error(GError **error, const char *secondary)
{
  if(!error || !secondary) return;

  char *combined = *error
    ? g_strdup_printf("%s; %s", (*error)->message, secondary)
    : g_strdup(secondary);
  g_clear_error(error);
  g_set_error_literal(error, _dt_win_print_error_quark(),
                      DT_WIN_PRINT_ERROR_FAILED, combined);
  g_free(combined);
}

static dt_print_result_t _dt_win_cleanup_temporary_output(
  const wchar_t *temporary_path,
  const char *printer_name,
  const gboolean committed_output,
  dt_print_result_t result,
  GError **error)
{
  gulong cleanup_error = ERROR_SUCCESS;
  const dt_win_print_temp_cleanup_result_t cleanup_result =
    committed_output
      ? dt_win_print_cleanup_committed_output_temp(temporary_path, &cleanup_error)
      : dt_win_print_cleanup_output_temp(temporary_path, &cleanup_error);
  if(cleanup_result == DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE) return result;

  char *path = _dt_win_wide_to_utf8(temporary_path);
  char *message = cleanup_result == DT_WIN_PRINT_TEMP_CLEANUP_SCHEDULED
    ? g_strdup_printf(
        _("temporary print output `%s' for printer `%s' could not be removed immediately "
          "(Windows error %lu); deletion was scheduled for the next system restart"),
        path ? path : "", printer_name ? printer_name : "",
        (unsigned long)cleanup_error)
    : g_strdup_printf(
        _("temporary print output `%s' for printer `%s' has uncertain cleanup "
          "(Windows error %lu); a late producer may still create it, so verify and remove it manually"),
        path ? path : "", printer_name ? printer_name : "",
        (unsigned long)cleanup_error);
  _dt_win_append_error(error, message);
  g_free(message);
  g_free(path);
  return cleanup_result == DT_WIN_PRINT_TEMP_CLEANUP_SCHEDULED
           ? result : dt_print_result_after_cleanup(result, FALSE);
}

static void _dt_win_set_submission_error(GError **error,
                                         dt_job_t *job,
                                         dt_win_print_job_state_t *state,
                                         const char *operation,
                                         const char *printer_name,
                                         const DWORD error_code)
{
  if(_dt_win_cancel_requested(job, state))
    _dt_win_set_cancelled_error(error, printer_name);
  else
    _dt_win_set_error(error, operation, printer_name, error_code);
}

static BOOL CALLBACK _dt_win_abort_proc(HDC hdc, int code)
{
  (void)hdc;
  (void)code;

  dt_win_print_job_state_t *state = dt_win_print_get_abort_state();
  if(!state) return TRUE;

  if(InterlockedCompareExchange(&state->cancel_requested, 0, 0))
    return FALSE;

  if(_dt_win_job_cancelled(state->job))
  {
    InterlockedExchange(&state->cancel_requested, 1);
    return FALSE;
  }

  return TRUE;
}

static void _dt_win_close_printer(HANDLE printer)
{
  if(printer) ClosePrinter(printer);
}

static DEVMODEW *_dt_win_default_devmode(const wchar_t *printer_name,
                                          gsize *allocation_size,
                                          HANDLE *printer_handle,
                                          GError **error)
{
  if(allocation_size) *allocation_size = 0;
  HANDLE printer = NULL;
  if(!OpenPrinterW((LPWSTR)printer_name, &printer, NULL))
  {
    char *name = _dt_win_wide_to_utf8(printer_name);
    _dt_win_set_error(error, "OpenPrinterW", name, GetLastError());
    g_free(name);
    return NULL;
  }

  const LONG size = DocumentPropertiesW(NULL, printer, (LPWSTR)printer_name,
                                        NULL, NULL, 0);
  if(size <= 0)
  {
    char *name = _dt_win_wide_to_utf8(printer_name);
    _dt_win_set_error(error, "DocumentPropertiesW(size)", name, GetLastError());
    g_free(name);
    _dt_win_close_printer(printer);
    return NULL;
  }

  DEVMODEW *devmode = g_malloc0(size);
  const LONG rc = DocumentPropertiesW(NULL, printer, (LPWSTR)printer_name,
                                      devmode, NULL, DM_OUT_BUFFER);
  if(rc != IDOK)
  {
    char *name = _dt_win_wide_to_utf8(printer_name);
    _dt_win_set_error(error, "DocumentPropertiesW(default)", name, GetLastError());
    g_free(name);
    g_free(devmode);
    _dt_win_close_printer(printer);
    return NULL;
  }

  if(!dt_win_print_devmode_layout_valid(devmode, (gsize)size))
  {
    char *name = _dt_win_wide_to_utf8(printer_name);
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' returned an invalid DEVMODE layout"),
                name ? name : "");
    g_free(name);
    g_free(devmode);
    _dt_win_close_printer(printer);
    return NULL;
  }

  if(printer_handle)
    *printer_handle = printer;
  else
    _dt_win_close_printer(printer);

  if(allocation_size) *allocation_size = (gsize)size;

  return devmode;
}

static gboolean _dt_win_parse_prefixed_int(const char *name,
                                           const char *prefix,
                                           int *value)
{
  if(!name || !prefix || !value || !g_str_has_prefix(name, prefix)) return FALSE;
  char *end = NULL;
  errno = 0;
  const long parsed = strtol(name + strlen(prefix), &end, 10);
  if(errno == ERANGE || !end || end == name + strlen(prefix) || *end
     || parsed < INT_MIN || parsed > INT_MAX)
    return FALSE;
  *value = (int)parsed;
  return TRUE;
}

static gboolean _dt_win_parse_prefixed_dword(const char *name,
                                             const char *prefix,
                                             DWORD *value)
{
  if(!name || !prefix || !value || !g_str_has_prefix(name, prefix)) return FALSE;
  char *end = NULL;
  errno = 0;
  const char *number = name + strlen(prefix);
  const unsigned long parsed = strtoul(number, &end, 10);
  if(errno == ERANGE || !end || end == number || *end || parsed > G_MAXUINT32)
    return FALSE;
  *value = (DWORD)parsed;
  return TRUE;
}

static DEVMODEW *_dt_win_create_devmode(const dt_print_info_t *pinfo,
                                         const dt_print_color_context_t *color,
                                         GError **error)
{
  guint32 bitmap_intent = 0;
  guint32 devmode_intent = 0;
  if(color && color->mode == DT_PRINT_COLOR_DRIVER_MANAGED
     && !dt_win_print_map_intent(pinfo->printer.intent,
                                 &bitmap_intent, &devmode_intent))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid rendering intent for printer `%s'"),
                pinfo->printer.name);
    return NULL;
  }
  (void)bitmap_intent;

  wchar_t *printer_name = _dt_win_utf8_to_wide(pinfo->printer.name);
  if(!printer_name)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("failed to convert printer name to UTF-16"));
    return NULL;
  }

  HANDLE printer = NULL;
  gsize devmode_size = 0;
  DEVMODEW *devmode = _dt_win_default_devmode(printer_name, &devmode_size,
                                               &printer, error);
  if(!devmode)
  {
    g_free(printer_name);
    return NULL;
  }
  if(!dt_win_print_devmode_field_available(
       devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmOrientation),
       sizeof(devmode->dmOrientation))
     || !dt_win_print_devmode_field_available(
       devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmCopies),
       sizeof(devmode->dmCopies)))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' returned a truncated DEVMODE"),
                pinfo->printer.name);
    goto invalid_devmode;
  }

  const DWORD advertised_fields = devmode->dmFields;

  devmode->dmFields |= DM_ORIENTATION | DM_COPIES;
  devmode->dmOrientation = pinfo->page.landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
  devmode->dmCopies = 1;

  int paper_id = 0;
  dt_win_print_paper_mode_t paper_mode = DT_WIN_PRINT_PAPER_DEFAULT;
  SHORT paper_width = 0;
  SHORT paper_length = 0;
  if(_dt_win_parse_prefixed_int(pinfo->paper.name, "win_dmpaper_", &paper_id))
  {
    if(paper_id <= 0 || paper_id > SHRT_MAX)
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("invalid paper identifier for printer `%s'"),
                  pinfo->printer.name);
      goto invalid_devmode;
    }
    const double width_tenth_mm = pinfo->paper.width * 10.0;
    const double height_tenth_mm = pinfo->paper.height * 10.0;
    if(!isfinite(width_tenth_mm) || !isfinite(height_tenth_mm)
       || width_tenth_mm < 1.0 || width_tenth_mm > SHRT_MAX
       || height_tenth_mm < 1.0 || height_tenth_mm > SHRT_MAX)
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("invalid paper dimensions for printer `%s'"),
                  pinfo->printer.name);
      goto invalid_devmode;
    }
    paper_width = (SHORT)lrint(width_tenth_mm);
    paper_length = (SHORT)lrint(height_tenth_mm);
    paper_mode = DT_WIN_PRINT_PAPER_STOCK;
    if(!dt_win_print_set_paper_fields(devmode, devmode_size, paper_mode,
                                      (SHORT)paper_id, 0, 0))
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("printer `%s' DEVMODE is too short for paper selection"),
                  pinfo->printer.name);
      goto invalid_devmode;
    }
  }
  else if(g_str_has_prefix(pinfo->paper.name, "win_dmpaper_"))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid paper identifier for printer `%s'"),
                pinfo->printer.name);
    goto invalid_devmode;
  }
  else if(!strcmp(pinfo->paper.name, "win_default"))
  {
    paper_mode = DT_WIN_PRINT_PAPER_DEFAULT;
  }
  else if(pinfo->paper.width > 0.0 && pinfo->paper.height > 0.0)
  {
    const double width_tenth_mm = pinfo->paper.width * 10.0;
    const double height_tenth_mm = pinfo->paper.height * 10.0;
    if(!isfinite(width_tenth_mm) || !isfinite(height_tenth_mm)
       || width_tenth_mm < 1.0 || width_tenth_mm > SHRT_MAX
       || height_tenth_mm < 1.0 || height_tenth_mm > SHRT_MAX)
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("invalid custom paper dimensions for printer `%s'"),
                  pinfo->printer.name);
      goto invalid_devmode;
    }
    paper_mode = DT_WIN_PRINT_PAPER_CUSTOM;
    paper_width = (SHORT)lrint(width_tenth_mm);
    paper_length = (SHORT)lrint(height_tenth_mm);
    if(!dt_win_print_set_paper_fields(devmode, devmode_size, paper_mode, 0,
                                      paper_width, paper_length))
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("printer `%s' DEVMODE is too short for custom paper dimensions"),
                  pinfo->printer.name);
      goto invalid_devmode;
    }
  }
  else
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid paper dimensions for printer `%s'"),
                pinfo->printer.name);
    goto invalid_devmode;
  }

#ifdef DM_MEDIATYPE
  DWORD media_id = 0;
  gboolean media_requested = FALSE;
  if(_dt_win_parse_prefixed_dword(pinfo->medium.name, "win_dmmedia_", &media_id))
  {
    if(!dt_win_print_devmode_field_available(
         devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmMediaType),
         sizeof(devmode->dmMediaType)))
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("printer `%s' DEVMODE is too short for media selection"),
                  pinfo->printer.name);
      goto invalid_devmode;
    }
    devmode->dmFields |= DM_MEDIATYPE;
    devmode->dmMediaType = media_id;
    media_requested = TRUE;
  }
  else if(g_str_has_prefix(pinfo->medium.name, "win_dmmedia_"))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid media identifier for printer `%s'"),
                pinfo->printer.name);
    goto invalid_devmode;
  }
#endif

#ifdef DM_ICMMETHOD
  const gboolean icm_method_available = dt_win_print_devmode_field_available(
    devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmICMMethod),
    sizeof(devmode->dmICMMethod));
  if(icm_method_available && (advertised_fields & DM_ICMMETHOD) && color
     && (color->mode == DT_PRINT_COLOR_DARKTABLE_MANAGED
         || color->mode == DT_PRINT_COLOR_DRIVER_MANAGED))
  {
    devmode->dmICMMethod = dt_win_print_select_icm_method(
      color->mode == DT_PRINT_COLOR_DARKTABLE_MANAGED,
      devmode->dmICMMethod);
  }
#endif

#ifdef DM_ICMINTENT
  if(color && color->mode == DT_PRINT_COLOR_DARKTABLE_MANAGED)
    devmode->dmFields &= ~DM_ICMINTENT;
  else if(dt_win_print_devmode_field_available(
            devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmICMIntent),
            sizeof(devmode->dmICMIntent))
          && (advertised_fields & DM_ICMINTENT)
          && color && color->mode == DT_PRINT_COLOR_DRIVER_MANAGED)
    devmode->dmICMIntent = (DWORD)devmode_intent;
#endif

  const DWORD requested_fields = devmode->dmFields;
  const SHORT requested_orientation = devmode->dmOrientation;
  const SHORT requested_paper = paper_mode == DT_WIN_PRINT_PAPER_STOCK
                                  ? devmode->dmPaperSize : 0;
  const SHORT requested_paper_width = paper_mode == DT_WIN_PRINT_PAPER_DEFAULT
                                        ? 0 : paper_width;
  const SHORT requested_paper_length = paper_mode == DT_WIN_PRINT_PAPER_DEFAULT
                                         ? 0 : paper_length;
#ifdef DM_MEDIATYPE
  const DWORD requested_media = media_requested ? devmode->dmMediaType : 0;
#endif
#ifdef DM_ICMMETHOD
  const DWORD requested_icm_method =
    (requested_fields & DM_ICMMETHOD) && icm_method_available
      ? devmode->dmICMMethod : 0;
#else
  const DWORD requested_icm_method = 0;
#endif
#ifdef DM_ICMINTENT
  const DWORD requested_icm_intent =
    (requested_fields & DM_ICMINTENT)
    && dt_win_print_devmode_field_available(
      devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmICMIntent),
      sizeof(devmode->dmICMIntent))
      ? devmode->dmICMIntent : 0;
#else
  const DWORD requested_icm_intent = 0;
#endif
  const LONG rc = DocumentPropertiesW(NULL, printer, printer_name,
                                      devmode, devmode,
                                      DM_IN_BUFFER | DM_OUT_BUFFER);
  _dt_win_close_printer(printer);
  g_free(printer_name);

  if(rc != IDOK)
  {
    _dt_win_set_error(error, "DocumentPropertiesW(merge)", pinfo->printer.name, GetLastError());
    g_free(devmode);
    return NULL;
  }

  if(!dt_win_print_devmode_layout_valid(devmode, devmode_size))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' returned an invalid DEVMODE after normalization"),
                pinfo->printer.name);
    g_free(devmode);
    return NULL;
  }

  if(!dt_win_print_devmode_field_available(
       devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmOrientation),
       sizeof(devmode->dmOrientation))
     || !(devmode->dmFields & DM_ORIENTATION)
     || devmode->dmOrientation != requested_orientation)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' rejected the requested orientation"),
                pinfo->printer.name);
    g_free(devmode);
    return NULL;
  }

  if(!dt_win_print_validate_copies(devmode, devmode_size))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' rejected the forced single-copy setting"),
                pinfo->printer.name);
    g_free(devmode);
    return NULL;
  }

  if(!dt_win_print_validate_paper_fields(devmode, devmode_size, paper_mode,
                                         requested_paper, requested_paper_width,
                                         requested_paper_length,
                                         paper_mode == DT_WIN_PRINT_PAPER_STOCK
                                           ? pinfo->paper.common_name : NULL))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' rejected the requested paper settings"),
                pinfo->printer.name);
    g_free(devmode);
    return NULL;
  }

#ifdef DM_MEDIATYPE
  if(media_requested
     && (!dt_win_print_devmode_field_available(
           devmode, devmode_size, G_STRUCT_OFFSET(DEVMODEW, dmMediaType),
           sizeof(devmode->dmMediaType))
         || !(devmode->dmFields & DM_MEDIATYPE)
         || devmode->dmMediaType != requested_media))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' rejected the requested media type"),
                pinfo->printer.name);
    g_free(devmode);
    return NULL;
  }
#endif

  if(!dt_win_print_validate_color_fields(devmode, devmode_size, requested_fields,
                                          requested_icm_method,
                                          requested_icm_intent))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("printer `%s' rejected the requested color management settings"),
                pinfo->printer.name);
    g_free(devmode);
    return NULL;
  }

  return devmode;

invalid_devmode:
  _dt_win_close_printer(printer);
  g_free(printer_name);
  g_free(devmode);
  return NULL;
}

static int _dt_win_capped_resolution(const int resolution)
{
  int capped = resolution > 0 ? resolution : 300;
  while(capped > 360)
    capped /= 2;
  return capped;
}

void dt_win_get_printer_info(const char *printer_name,
                             dt_printer_info_t *pinfo)
{
  if(!printer_name || !pinfo) return;

  *pinfo->name = '\0';
  char validated_name[DT_PRINT_MAX_PRINTER_NAME] = { 0 };
  if(!dt_win_print_store_printer_name(validated_name, sizeof(validated_name),
                                      printer_name))
  {
    dt_print(DT_DEBUG_PRINT, "[print] rejected invalid or oversized Windows printer identity");
    return;
  }

  wchar_t *printer_name_w = _dt_win_utf8_to_wide(validated_name);
  if(!printer_name_w) return;

  GError *error = NULL;
  DEVMODEW *devmode = _dt_win_default_devmode(printer_name_w, NULL, NULL, &error);
  if(!devmode)
  {
    dt_print(DT_DEBUG_PRINT, "[print] %s", error ? error->message : "failed to get Windows printer info");
    g_clear_error(&error);
    g_free(printer_name_w);
    return;
  }

  HDC hdc = CreateDCW(L"WINSPOOL", printer_name_w, NULL, devmode);
  if(!hdc)
  {
    dt_print(DT_DEBUG_PRINT, "[print] CreateDCW failed for %s (%lu)",
             validated_name, (unsigned long)GetLastError());
    g_free(devmode);
    g_free(printer_name_w);
    return;
  }

  memcpy(pinfo->name, validated_name, strlen(validated_name) + 1);

  const int dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
  const int dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
  const int physical_width = GetDeviceCaps(hdc, PHYSICALWIDTH);
  const int physical_height = GetDeviceCaps(hdc, PHYSICALHEIGHT);
  const int offset_x = GetDeviceCaps(hdc, PHYSICALOFFSETX);
  const int offset_y = GetDeviceCaps(hdc, PHYSICALOFFSETY);
  const int horzres = GetDeviceCaps(hdc, HORZRES);
  const int vertres = GetDeviceCaps(hdc, VERTRES);

  if(dpi_x > 0 && dpi_y > 0)
  {
    pinfo->hw_margin_left = (double)offset_x / (double)dpi_x * 25.4;
    pinfo->hw_margin_top = (double)offset_y / (double)dpi_y * 25.4;
    pinfo->hw_margin_right =
      (double)(physical_width - horzres - offset_x) / (double)dpi_x * 25.4;
    pinfo->hw_margin_bottom =
      (double)(physical_height - vertres - offset_y) / (double)dpi_y * 25.4;
    if(physical_width > physical_height)
      dt_win_print_margins_to_portrait(&pinfo->hw_margin_left,
                                        &pinfo->hw_margin_top,
                                        &pinfo->hw_margin_right,
                                        &pinfo->hw_margin_bottom);
  }

  pinfo->resolution = _dt_win_capped_resolution(MAX(dpi_x, dpi_y));
  pinfo->intent = DT_INTENT_PERCEPTUAL;
  pinfo->is_turboprint = FALSE;

  dt_print(DT_DEBUG_PRINT, "[print] Windows printer `%s' dpi %d/%d, render dpi %d",
           validated_name, dpi_x, dpi_y, pinfo->resolution);

  DeleteDC(hdc);
  g_free(devmode);
  g_free(printer_name_w);
}

static gboolean _dt_win_get_default_paper_size(const wchar_t *printer_name,
                                                double *width,
                                                double *height)
{
  GError *error = NULL;
  DEVMODEW *devmode = _dt_win_default_devmode(printer_name, NULL, NULL, &error);
  if(!devmode)
  {
    dt_print(DT_DEBUG_PRINT, "[print] %s",
             error ? error->message : "failed to get default Windows paper size");
    g_clear_error(&error);
    return FALSE;
  }

  gboolean found = FALSE;
  HDC hdc = CreateDCW(L"WINSPOOL", printer_name, NULL, devmode);
  if(hdc)
  {
    found = dt_win_print_page_size_mm(GetDeviceCaps(hdc, PHYSICALWIDTH),
                                      GetDeviceCaps(hdc, PHYSICALHEIGHT),
                                      GetDeviceCaps(hdc, LOGPIXELSX),
                                      GetDeviceCaps(hdc, LOGPIXELSY),
                                      width, height);
    if(found) found = dt_win_print_normalize_page_size(width, height);
    DeleteDC(hdc);
  }

  g_free(devmode);
  return found;
}

GList *dt_win_get_papers(const dt_printer_info_t *printer)
{
  if(!printer || !*printer->name) return NULL;

  wchar_t *printer_name = _dt_win_utf8_to_wide(printer->name);
  if(!printer_name) return NULL;

  const int count = DeviceCapabilitiesW(printer_name, NULL, DC_PAPERS, NULL, NULL);
  GList *result = NULL;

  if(count > 0)
  {
    WORD *papers = g_malloc0_n(count, sizeof(WORD));
    WCHAR *names = g_malloc0_n(count, 64 * sizeof(WCHAR));
    POINT *sizes = g_malloc0_n(count, sizeof(POINT));

    const int got_papers = DeviceCapabilitiesW(printer_name, NULL, DC_PAPERS, (LPWSTR)papers, NULL);
    const int got_names = DeviceCapabilitiesW(printer_name, NULL, DC_PAPERNAMES, names, NULL);
    const int got_sizes = DeviceCapabilitiesW(printer_name, NULL, DC_PAPERSIZE, (LPWSTR)sizes, NULL);

    const int usable = MIN(count, MIN(got_papers, MIN(got_names, got_sizes)));
    for(int k = 0; k < usable; k++)
    {
      if(sizes[k].x <= 0 || sizes[k].y <= 0) continue;

      dt_paper_info_t *paper = malloc(sizeof(dt_paper_info_t));
      snprintf(paper->name, sizeof(paper->name), "win_dmpaper_%u", (unsigned)papers[k]);
      if(!dt_win_print_store_wide_slot_name(paper->common_name,
                                             sizeof(paper->common_name),
                                             &names[k * 64], 64))
      {
        dt_print(DT_DEBUG_PRINT,
                 "[print] rejected Windows paper name outside the UTF-8 media bound");
        free(paper);
        continue;
      }
      paper->width = (double)sizes[k].x / 10.0;
      paper->height = (double)sizes[k].y / 10.0;
      result = g_list_append(result, paper);
    }

    g_free(papers);
    g_free(names);
    g_free(sizes);
  }

  double default_width = 0.0;
  double default_height = 0.0;
  if(_dt_win_get_default_paper_size(printer_name, &default_width, &default_height))
  {
    dt_paper_info_t *paper = malloc(sizeof(dt_paper_info_t));
    dt_print_store_name(paper->name, sizeof(paper->name), "win_default");
    if(!dt_print_store_name(paper->common_name, sizeof(paper->common_name),
                            _("printer default")))
      dt_print_store_name(paper->common_name, sizeof(paper->common_name),
                          "printer default");
    paper->width = default_width;
    paper->height = default_height;
    result = g_list_append(result, paper);
  }
  else if(!result)
    dt_print(DT_DEBUG_PRINT,
             "[print] unable to determine default paper geometry; no fallback paper added");

  g_free(printer_name);
  return dt_print_sort_papers_default_first(result, "win_default");
}

GList *dt_win_get_media_type(const dt_printer_info_t *printer)
{
  if(!printer || !*printer->name) return NULL;

  wchar_t *printer_name = _dt_win_utf8_to_wide(printer->name);
  if(!printer_name) return NULL;

  GList *result = NULL;
  const int count = DeviceCapabilitiesW(printer_name, NULL, DC_MEDIATYPES, NULL, NULL);

  if(count > 0)
  {
    DWORD *types = g_malloc0_n(count, sizeof(DWORD));
    WCHAR *names = g_malloc0_n(count, 64 * sizeof(WCHAR));

    const int got_types = DeviceCapabilitiesW(printer_name, NULL, DC_MEDIATYPES, (LPWSTR)types, NULL);
    const int got_names = DeviceCapabilitiesW(printer_name, NULL, DC_MEDIATYPENAMES, names, NULL);
    const int usable = MIN(count, MIN(got_types, got_names));

    for(int k = 0; k < usable; k++)
    {
      dt_medium_info_t *medium = malloc(sizeof(dt_medium_info_t));
      snprintf(medium->name, sizeof(medium->name), "win_dmmedia_%lu", (unsigned long)types[k]);
      if(!dt_win_print_store_wide_slot_name(medium->common_name,
                                             sizeof(medium->common_name),
                                             &names[k * 64], 64))
      {
        dt_print(DT_DEBUG_PRINT,
                 "[print] rejected Windows media name outside the UTF-8 media bound");
        free(medium);
        continue;
      }
      result = g_list_append(result, medium);
    }

    g_free(types);
    g_free(names);
  }

  if(!result)
  {
    dt_medium_info_t *medium = malloc(sizeof(dt_medium_info_t));
    dt_print_store_name(medium->name, sizeof(medium->name), "default");
    if(!dt_print_store_name(medium->common_name, sizeof(medium->common_name),
                            _("printer default")))
      dt_print_store_name(medium->common_name, sizeof(medium->common_name),
                          "printer default");
    result = g_list_append(result, medium);
  }

  g_free(printer_name);
  return result;
}

static gboolean _dt_win_printer_is_unavailable(const PRINTER_INFO_2W *printer)
{
  if(!printer || !printer->pPrinterName || !*printer->pPrinterName)
    return TRUE;
  if(printer->Attributes & PRINTER_ATTRIBUTE_WORK_OFFLINE)
    return TRUE;
  if(printer->Status & (PRINTER_STATUS_NOT_AVAILABLE | PRINTER_STATUS_SERVER_UNKNOWN))
    return TRUE;
  return FALSE;
}

static int _dt_win_detect_printers_callback(dt_job_t *job)
{
  dt_win_prtctl_t *pctl = dt_control_job_get_params(job);
  DWORD needed = 0;
  DWORD returned = 0;

  EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2,
                NULL, 0, &needed, &returned);

  if(!needed || InterlockedCompareExchange(&_discovery_cancel, 0, 0))
  {
    dt_printers_discovery_settled();
    return 0;
  }

  BYTE *buffer = g_malloc0(needed);
  if(!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2,
                    buffer, needed, &needed, &returned))
  {
    dt_print(DT_DEBUG_PRINT, "[print] EnumPrintersW failed (%lu)",
             (unsigned long)GetLastError());
    g_free(buffer);
    dt_printers_discovery_settled();
    return 1;
  }

  PRINTER_INFO_2W *printers = (PRINTER_INFO_2W *)buffer;
  for(DWORD k = 0; k < returned; k++)
  {
    if(InterlockedCompareExchange(&_discovery_cancel, 0, 0)
       || _dt_win_job_cancelled(job))
      break;

    if(_dt_win_printer_is_unavailable(&printers[k]))
      continue;

    char *name = _dt_win_wide_to_utf8(printers[k].pPrinterName);
    if(!name) continue;

    dt_printer_info_t pr;
    memset(&pr, 0, sizeof(pr));
    dt_win_get_printer_info(name, &pr);
    if(!InterlockedCompareExchange(&_discovery_cancel, 0, 0)
       && *pr.name && pctl->cb)
    {
      pctl->cb(&pr, pctl->user_data);
      dt_print(DT_DEBUG_PRINT, "[print] new Windows printer %s found", name);
    }
    g_free(name);
  }

  g_free(buffer);
  dt_printers_discovery_settled();
  return 0;
}

void dt_win_printers_abort_discovery(void)
{
  InterlockedExchange(&_discovery_cancel, 1);
}

void dt_win_printers_discovery(void (*cb)(dt_printer_info_t *pr, void *user_data),
                               void *user_data)
{
  dt_job_t *job = dt_control_job_create(&_dt_win_detect_printers_callback,
                                        "detect connected printers");
  InterlockedExchange(&_discovery_cancel, 0);

  if(job)
  {
    dt_win_prtctl_t *prtctl = g_malloc0(sizeof(dt_win_prtctl_t));

    prtctl->cb = cb;
    prtctl->user_data = user_data;

    dt_control_job_set_params(job, prtctl, g_free);
    dt_control_add_job(DT_JOB_QUEUE_SYSTEM_BG, job);
  }
  else
    dt_printers_discovery_settled();
}

static gboolean _dt_win_apply_color_mode(HDC hdc,
                                         const char *printer_name,
                                         const dt_print_color_context_t *color,
                                         GError **error)
{
  if(color && color->mode == DT_PRINT_COLOR_DARKTABLE_MANAGED)
  {
    if(SetICMMode(hdc, ICM_OFF) == 0)
    {
      _dt_win_set_error(error, "SetICMMode(ICM_OFF)", printer_name, GetLastError());
      return FALSE;
    }
    return TRUE;
  }

  if(!color || color->mode != DT_PRINT_COLOR_DRIVER_MANAGED)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid color management mode for `%s'"), printer_name);
    return FALSE;
  }

  if(SetICMMode(hdc, ICM_ON) == 0)
  {
    _dt_win_set_error(error, "SetICMMode(ICM_ON)", printer_name, GetLastError());
    return FALSE;
  }

  return TRUE;
}

static gboolean _dt_win_validate_images(const dt_print_info_t *pinfo,
                                        const dt_print_color_context_t *color,
                                        const dt_images_box *imgs,
                                        GError **error)
{
  if(!pinfo || !color || !imgs || imgs->count <= 0
     || imgs->count > MAX_IMAGE_PER_PAGE)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid image list for Windows printing"));
    return FALSE;
  }

  int valid_images = 0;
  for(int k = 0; k < imgs->count; k++)
  {
    const dt_image_box *box = &imgs->box[k];
    if(!dt_is_valid_imgid(box->imgid)) continue;
    valid_images++;

    if(!box->buf || box->exp_width <= 0 || box->exp_height <= 0)
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("invalid exported image %d for printer `%s'"),
                  k + 1, pinfo->printer.name);
      return FALSE;
    }

    if(color->mode == DT_PRINT_COLOR_DRIVER_MANAGED)
    {
      gsize profile_size = 0;
      if(!box->source_icc_blob
         || !g_bytes_get_data(box->source_icc_blob, &profile_size)
         || profile_size == 0)
      {
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("driver color management for image %d on `%s' needs an export ICC profile"),
                    k + 1, pinfo->printer.name);
        return FALSE;
      }
    }
  }

  if(valid_images == 0)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("no exported images to print on `%s'"), pinfo->printer.name);
    return FALSE;
  }

  return TRUE;
}

static gboolean _dt_win_printer_uses_pdf_driver(const wchar_t *printer_name,
                                                const char *printer_name_utf8,
                                                GError **error)
{
  HANDLE printer = NULL;
  if(!OpenPrinterW((LPWSTR)printer_name, &printer, NULL))
  {
    _dt_win_set_error(error, "OpenPrinterW", printer_name_utf8, GetLastError());
    return FALSE;
  }

  DWORD needed = 0;
  SetLastError(ERROR_SUCCESS);
  GetPrinterW(printer, 2, NULL, 0, &needed);
  const DWORD size_error = GetLastError();
  if(needed == 0 || (size_error != ERROR_INSUFFICIENT_BUFFER
                     && size_error != ERROR_SUCCESS))
  {
    _dt_win_set_error(error, "GetPrinterW(size)", printer_name_utf8, size_error);
    _dt_win_close_printer(printer);
    return FALSE;
  }

  BYTE *buffer = g_try_malloc(needed);
  if(!buffer)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("out of memory reading printer `%s'"), printer_name_utf8);
    _dt_win_close_printer(printer);
    return FALSE;
  }

  if(!GetPrinterW(printer, 2, buffer, needed, &needed))
  {
    _dt_win_set_error(error, "GetPrinterW", printer_name_utf8, GetLastError());
    g_free(buffer);
    _dt_win_close_printer(printer);
    return FALSE;
  }

  const PRINTER_INFO_2W *info = (const PRINTER_INFO_2W *)buffer;
  const gboolean matches = info->pDriverName
                           && _wcsicmp(info->pDriverName,
                                      L"Microsoft Print To PDF") == 0;
  if(!matches)
  {
    char *driver = _dt_win_wide_to_utf8(info->pDriverName);
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("DARKTABLE_WIN_PRINT_OUTPUT_FILE is restricted to the Microsoft Print To PDF driver; "
                  "printer `%s' uses `%s'"),
                printer_name_utf8, driver ? driver : _("unknown driver"));
    g_free(driver);
  }

  g_free(buffer);
  _dt_win_close_printer(printer);
  return matches;
}

static gpointer _dt_win_create_bitmap_header(const int width,
                                             const int height,
                                             const dt_print_info_t *pinfo,
                                             const dt_print_color_context_t *color,
                                             const dt_image_box *box,
                                             GError **error)
{
  if(color && color->mode == DT_PRINT_COLOR_DRIVER_MANAGED)
  {
    gsize profile_size = 0;
    const guint8 *profile = box->source_icc_blob
      ? g_bytes_get_data(box->source_icc_blob, &profile_size) : NULL;
    gsize size = 0;
    guint32 bitmap_intent = 0;
    guint32 devmode_intent = 0;
    if(!profile || !dt_win_print_get_bitmap_header_size(profile_size, &size)
       || !dt_win_print_map_intent(pinfo->printer.intent,
                                   &bitmap_intent, &devmode_intent))
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("invalid ICC profile or rendering intent for printer `%s'"),
                  pinfo->printer.name);
      return NULL;
    }
    (void)devmode_intent;

    BITMAPV5HEADER *header = g_try_malloc0(size);
    if(!header)
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("out of memory preparing image for printer `%s'"),
                  pinfo->printer.name);
      return NULL;
    }
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = width;
    header->bV5Height = -height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_RGB;
    header->bV5CSType = DT_WIN_PROFILE_EMBEDDED;
    header->bV5Intent = bitmap_intent;
    header->bV5ProfileData = sizeof(BITMAPV5HEADER);
    header->bV5ProfileSize = (DWORD)profile_size;
    memcpy(((guint8 *)header) + sizeof(BITMAPV5HEADER), profile, profile_size);
    return header;
  }

  BITMAPINFO *bmi = g_try_malloc0(sizeof(BITMAPINFO));
  if(!bmi)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("out of memory preparing image for printer `%s'"),
                pinfo->printer.name);
    return NULL;
  }
  bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi->bmiHeader.biWidth = width;
  bmi->bmiHeader.biHeight = -height;
  bmi->bmiHeader.biPlanes = 1;
  bmi->bmiHeader.biBitCount = 32;
  bmi->bmiHeader.biCompression = BI_RGB;
  return bmi;
}

static void _dt_win_set_bitmap_header_height(gpointer header,
                                             const dt_print_color_context_t *color,
                                             const int height)
{
  if(color && color->mode == DT_PRINT_COLOR_DRIVER_MANAGED)
    ((BITMAPV5HEADER *)header)->bV5Height = -height;
  else
    ((BITMAPINFO *)header)->bmiHeader.biHeight = -height;
}

static gboolean _dt_win_round_to_int(const double value, int *result)
{
  if(!result || !isfinite(value) || value < INT_MIN || value > INT_MAX)
    return FALSE;
  *result = (int)lrint(value);
  return TRUE;
}

static gboolean _dt_win_stretch_image(HDC hdc,
                                      const dt_print_info_t *pinfo,
                                      const dt_print_color_context_t *color,
                                      const dt_images_box *imgs,
                                      const dt_image_box *box,
                                      dt_job_t *job,
                                      GError **error)
{
  const int physical_width = GetDeviceCaps(hdc, PHYSICALWIDTH);
  const int physical_height = GetDeviceCaps(hdc, PHYSICALHEIGHT);
  const int offset_x = GetDeviceCaps(hdc, PHYSICALOFFSETX);
  const int offset_y = GetDeviceCaps(hdc, PHYSICALOFFSETY);

  if(physical_width <= 0 || physical_height <= 0
     || !isfinite(imgs->page_width) || !isfinite(imgs->page_height)
     || imgs->page_width <= 0.0f || imgs->page_height <= 0.0f
     || !isfinite(box->print.x) || !isfinite(box->print.y)
     || !isfinite(box->print.width) || !isfinite(box->print.height)
     || box->print.width <= 0.0f || box->print.height <= 0.0f)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid printer geometry for `%s'"), pinfo->printer.name);
    return FALSE;
  }

  const double scale_x = (double)physical_width / (double)imgs->page_width;
  const double scale_y = (double)physical_height / (double)imgs->page_height;

  int dest_x = 0;
  int dest_y = 0;
  int dest_w = 0;
  int dest_h = 0;
  if(!_dt_win_round_to_int((double)box->print.x * scale_x - offset_x, &dest_x)
     || !_dt_win_round_to_int(((double)imgs->page_height
                               - (double)box->print.y - (double)box->print.height)
                                * scale_y - offset_y, &dest_y)
     || !_dt_win_round_to_int((double)box->print.width * scale_x, &dest_w)
     || !_dt_win_round_to_int((double)box->print.height * scale_y, &dest_h))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("image placement is outside printer `%s' limits"),
                pinfo->printer.name);
    return FALSE;
  }
  dest_w = MAX(1, dest_w);
  dest_h = MAX(1, dest_h);

  const int src_w = box->exp_width;
  const int src_h = box->exp_height;
  if(!box->buf || src_w <= 0 || src_h <= 0)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid exported image for printer `%s'"), pinfo->printer.name);
    return FALSE;
  }

  gsize source_row_bytes = 0;
  gsize band_bytes = 0;
  int band_rows = 0;
  if(!dt_win_print_get_band_layout(src_w, src_h, &source_row_bytes,
                                   &band_rows, &band_bytes))
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("exported image is too large for printer `%s'"),
                pinfo->printer.name);
    return FALSE;
  }

  guint8 *band = g_try_malloc(band_bytes);
  if(!band)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("out of memory preparing image for printer `%s'"),
                pinfo->printer.name);
    return FALSE;
  }

  gpointer header = _dt_win_create_bitmap_header(src_w, band_rows, pinfo,
                                                  color, box, error);
  if(!header)
  {
    g_free(band);
    return FALSE;
  }

  for(int src_y = 0; src_y < src_h; src_y += band_rows)
  {
    if(_dt_win_job_cancelled(job))
    {
      g_free(header);
      g_free(band);
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("printing on `%s' cancelled"), pinfo->printer.name);
      return FALSE;
    }

    const int rows = MIN(band_rows, src_h - src_y);
    const guint8 *src = ((const guint8 *)box->buf) + (gsize)src_y * source_row_bytes;

    for(int y = 0; y < rows; y++)
    {
      const guint8 *row = src + (gsize)y * source_row_bytes;
      guint8 *dst = band + (gsize)y * src_w * 4;
      for(int x = 0; x < src_w; x++)
      {
        dst[x * 4 + 0] = row[x * 3 + 2];
        dst[x * 4 + 1] = row[x * 3 + 1];
        dst[x * 4 + 2] = row[x * 3 + 0];
        dst[x * 4 + 3] = 0xff;
      }
    }

    _dt_win_set_bitmap_header_height(header, color, rows);

    const int band_dest_y0 = (int)floor((double)src_y * dest_h / (double)src_h);
    const int band_dest_y1 = (int)ceil((double)(src_y + rows) * dest_h / (double)src_h);
    const int band_dest_h = MAX(1, band_dest_y1 - band_dest_y0);
    const gint64 band_dest_y = (gint64)dest_y + band_dest_y0;
    if(band_dest_y < INT_MIN || band_dest_y > INT_MAX)
    {
      g_free(header);
      g_free(band);
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("image placement is outside printer `%s' limits"),
                  pinfo->printer.name);
      return FALSE;
    }

    const int rc = StretchDIBits(hdc,
                                 dest_x, (int)band_dest_y, dest_w, band_dest_h,
                                 0, 0, src_w, rows,
                                 band, (BITMAPINFO *)header, DIB_RGB_COLORS, SRCCOPY);

    if(rc <= 0)
    {
      g_free(header);
      g_free(band);
      if(_dt_win_job_cancelled(job))
        _dt_win_set_cancelled_error(error, pinfo->printer.name);
      else
        _dt_win_set_error(error, "StretchDIBits", pinfo->printer.name, GetLastError());
      return FALSE;
    }
  }

  g_free(header);
  g_free(band);
  return TRUE;
}

dt_print_result_t dt_win_print_submit(const dt_imgid_t imgid,
                                      const char *job_title,
                                      const dt_print_info_t *pinfo,
                                      const dt_print_color_context_t *color,
                                      dt_images_box *imgs,
                                      dt_job_t *job,
                                      GError **error)
{
  (void)imgid;

  if(!pinfo || !color || !imgs || !*pinfo->printer.name)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("invalid Windows print submission"));
    return DT_PRINT_RESULT_FAILED;
  }

  if(_dt_win_job_cancelled(job))
  {
    _dt_win_set_cancelled_error(error, pinfo->printer.name);
    return DT_PRINT_RESULT_CANCELLED;
  }

  if(!_dt_win_validate_images(pinfo, color, imgs, error))
    return DT_PRINT_RESULT_FAILED;

  wchar_t *printer_name = _dt_win_utf8_to_wide(pinfo->printer.name);
  wchar_t *title = _dt_win_utf8_to_wide(job_title ? job_title : "darktable");
  if(!printer_name || !title)
  {
    g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                _("failed to convert print job strings to UTF-16"));
    g_free(printer_name);
    g_free(title);
    return DT_PRINT_RESULT_FAILED;
  }

  wchar_t *output_filename = NULL;
  wchar_t *output_temp_filename = NULL;
  const char *output_filename_utf8 = g_getenv("DARKTABLE_WIN_PRINT_OUTPUT_FILE");
  if(output_filename_utf8 && *output_filename_utf8)
  {
    if(!g_path_is_absolute(output_filename_utf8))
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("DARKTABLE_WIN_PRINT_OUTPUT_FILE must be an absolute path"));
      g_free(printer_name);
      g_free(title);
      return DT_PRINT_RESULT_FAILED;
    }

    if(!_dt_win_printer_uses_pdf_driver(printer_name, pinfo->printer.name, error))
    {
      g_free(printer_name);
      g_free(title);
      return DT_PRINT_RESULT_FAILED;
    }

    output_filename = _dt_win_utf8_to_wide(output_filename_utf8);
    if(!output_filename)
    {
      g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                  _("failed to convert print output filename to UTF-16"));
      g_free(printer_name);
      g_free(title);
      return DT_PRINT_RESULT_FAILED;
    }

    DWORD output_path_error = ERROR_SUCCESS;
    if(!dt_win_print_prepare_output_path(output_filename, &output_temp_filename,
                                         &output_path_error))
    {
      if(output_path_error == ERROR_FILE_EXISTS)
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("print output file `%s' already exists"), output_filename_utf8);
      else
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("failed to check print output file `%s' (Windows error %lu)"),
                    output_filename_utf8, (unsigned long)output_path_error);
      const dt_print_result_t failure = _dt_win_cleanup_temporary_output(
        output_temp_filename, pinfo->printer.name, FALSE,
        DT_PRINT_RESULT_FAILED, error);
      g_free(printer_name);
      g_free(title);
      g_free(output_filename);
      g_free(output_temp_filename);
      return failure;
    }
  }

  DEVMODEW *devmode = _dt_win_create_devmode(pinfo, color, error);
  if(!devmode)
  {
    const dt_print_result_t failure = _dt_win_cleanup_temporary_output(
      output_temp_filename, pinfo->printer.name, FALSE,
      DT_PRINT_RESULT_FAILED, error);
    g_free(printer_name);
    g_free(title);
    g_free(output_filename);
    g_free(output_temp_filename);
    return failure;
  }

  HDC hdc = CreateDCW(L"WINSPOOL", printer_name, NULL, devmode);
  if(!hdc)
  {
    _dt_win_set_error(error, "CreateDCW", pinfo->printer.name, GetLastError());
    g_free(devmode);
    g_free(printer_name);
    g_free(title);
    g_free(output_filename);
    const dt_print_result_t failure = _dt_win_cleanup_temporary_output(
      output_temp_filename, pinfo->printer.name, FALSE,
      DT_PRINT_RESULT_FAILED, error);
    g_free(output_temp_filename);
    return failure;
  }

  dt_win_print_job_state_t state = { 0 };
  state.job = job;
  state.hdc = hdc;
  gpointer previous_abort_state = dt_win_print_get_abort_state();
  dt_win_print_set_abort_state(&state);

  dt_print_result_t result = DT_PRINT_RESULT_FAILED;
  gboolean output_unpublished_committed = FALSE;
  if(SetAbortProc(hdc, _dt_win_abort_proc) <= 0)
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "SetAbortProc",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = dt_print_cancellation_result(DT_PRINT_CANCEL_PRE_COMMIT, FALSE);
    goto cleanup;
  }

  DOCINFOW docinfo;
  memset(&docinfo, 0, sizeof(docinfo));
  docinfo.cbSize = sizeof(docinfo);
  docinfo.lpszDocName = title;

  if(output_filename_utf8 && *output_filename_utf8)
  {
    docinfo.lpszOutput = output_temp_filename;
    dt_print(DT_DEBUG_PRINT, "[print] DARKTABLE_WIN_PRINT_OUTPUT_FILE active: %s",
             output_filename_utf8);
  }

  if(_dt_win_cancel_requested(job, &state))
  {
    _dt_win_set_cancelled_error(error, pinfo->printer.name);
    result = dt_print_cancellation_result(DT_PRINT_CANCEL_PRE_COMMIT, FALSE);
    goto cleanup;
  }

  if(StartDocW(hdc, &docinfo) <= 0)
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "StartDocW",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = dt_print_cancellation_result(DT_PRINT_CANCEL_PRE_COMMIT, FALSE);
    goto cleanup;
  }
  InterlockedExchange(&state.started_doc, 1);

  if(_dt_win_cancel_requested(job, &state))
  {
    _dt_win_set_cancelled_error(error, pinfo->printer.name);
    result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }

  if(StartPage(hdc) <= 0)
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "StartPage",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }

  if(!_dt_win_apply_color_mode(hdc, pinfo->printer.name, color, error))
    goto cleanup;

  if(SetStretchBltMode(hdc, HALFTONE) == 0)
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "SetStretchBltMode",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }
  if(!SetBrushOrgEx(hdc, 0, 0, NULL))
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "SetBrushOrgEx",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }

  for(int k = 0; k < imgs->count; k++)
  {
    const dt_image_box *box = &imgs->box[k];
    if(!dt_is_valid_imgid(box->imgid)) continue;

    if(!_dt_win_stretch_image(hdc, pinfo, color, imgs, box, job, error))
    {
      if(_dt_win_cancel_requested(job, &state))
        result = DT_PRINT_RESULT_CANCELLED;
      goto cleanup;
    }
  }

  if(_dt_win_cancel_requested(job, &state))
  {
    _dt_win_set_cancelled_error(error, pinfo->printer.name);
    result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }

  if(EndPage(hdc) <= 0)
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "EndPage",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }

  if(_dt_win_cancel_requested(job, &state))
  {
    _dt_win_set_cancelled_error(error, pinfo->printer.name);
    result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }

  if(EndDoc(hdc) <= 0)
  {
    const DWORD submission_error = GetLastError();
    _dt_win_set_submission_error(error, job, &state, "EndDoc",
                                 pinfo->printer.name, submission_error);
    if(_dt_win_cancel_requested(job, &state))
      result = DT_PRINT_RESULT_CANCELLED;
    goto cleanup;
  }
  InterlockedExchange(&state.started_doc, 0);
  output_unpublished_committed =
    dt_win_print_output_requires_committed_cleanup(
      output_filename != NULL, TRUE, TRUE, FALSE);
  if(output_unpublished_committed)
  {
    guint64 output_size = 0;
    gulong wait_error = ERROR_SUCCESS;
    const dt_win_print_output_wait_result_t wait_result =
      dt_win_print_wait_for_output(output_temp_filename, &output_size, &wait_error);
    if(wait_result != DT_WIN_PRINT_OUTPUT_WAIT_READY)
    {
      char *temporary_path = _dt_win_wide_to_utf8(output_temp_filename);
      if(wait_result == DT_WIN_PRINT_OUTPUT_WAIT_INVALID)
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("temporary print output `%s' from printer `%s' is not a complete PDF"),
                    temporary_path ? temporary_path : "", pinfo->printer.name);
      else if(wait_result == DT_WIN_PRINT_OUTPUT_WAIT_TIMEOUT)
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("temporary print output `%s' from printer `%s' did not become complete "
                      "within the bounded wait (Windows error %lu); a late producer may still create it"),
                    temporary_path ? temporary_path : "", pinfo->printer.name,
                    (unsigned long)wait_error);
      else
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("failed to verify temporary print output `%s' from printer `%s' "
                      "(Windows error %lu)"),
                    temporary_path ? temporary_path : "", pinfo->printer.name,
                    (unsigned long)wait_error);
      g_free(temporary_path);
      result = DT_PRINT_RESULT_FAILED;
      goto cleanup;
    }
    dt_print(DT_DEBUG_PRINT,
             "[print] complete temporary PDF output ready (%" G_GUINT64_FORMAT " bytes)",
             output_size);
  }

  if(_dt_win_cancel_requested(job, &state))
  {
    if(output_filename)
    {
      _dt_win_set_cancelled_error(error, pinfo->printer.name);
      result = dt_print_cancellation_result(
        DT_PRINT_CANCEL_AFTER_UNPUBLISHED_FILE_COMMIT, FALSE);
    }
    else
    {
      char *message = g_strdup_printf(
        _("printer `%s' accepted the job before cancellation could be confirmed; "
          "the job may still print"), pinfo->printer.name);
      _dt_win_append_error(error, message);
      g_free(message);
      result = dt_print_cancellation_result(
        DT_PRINT_CANCEL_AFTER_PHYSICAL_COMMIT, FALSE);
    }
    goto cleanup;
  }

  if(output_filename)
  {
    DWORD publish_error = ERROR_SUCCESS;
    if(!dt_win_print_publish_output(output_temp_filename, output_filename,
                                    &publish_error))
    {
      if(publish_error == ERROR_ALREADY_EXISTS || publish_error == ERROR_FILE_EXISTS)
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("print output file `%s' appeared before publication"),
                    output_filename_utf8);
      else
        g_set_error(error, _dt_win_print_error_quark(), DT_WIN_PRINT_ERROR_FAILED,
                    _("failed to publish print output file `%s' (Windows error %lu)"),
                    output_filename_utf8, (unsigned long)publish_error);
      goto cleanup;
    }
    output_unpublished_committed = FALSE;
  }
  result = DT_PRINT_RESULT_SUCCESS;
  dt_control_log(_("printing `%s' on `%s'"), job_title, pinfo->printer.name);

cleanup:
  if(result != DT_PRINT_RESULT_SUCCESS
     && InterlockedCompareExchange(&state.started_doc, 0, 0))
  {
    const int abort_result = AbortDoc(hdc);
    const DWORD abort_error = abort_result > 0 ? ERROR_SUCCESS : GetLastError();
    const gboolean abort_succeeded = abort_result > 0;
    output_unpublished_committed =
      output_unpublished_committed
      || dt_win_print_output_requires_committed_cleanup(
           output_filename != NULL, TRUE, FALSE, abort_succeeded);
    if(result == DT_PRINT_RESULT_CANCELLED)
      result = dt_print_cancellation_result(DT_PRINT_CANCEL_ABORT_DOC,
                                            abort_succeeded);
    if(!abort_succeeded)
    {
      char *message = g_strdup_printf(
        _("AbortDoc failed for printer `%s' (Windows error %lu); "
          "cancellation is uncertain and the job may still print"),
        pinfo->printer.name, (unsigned long)abort_error);
      _dt_win_append_error(error, message);
      g_free(message);
      result = DT_PRINT_RESULT_FAILED;
    }
    InterlockedExchange(&state.started_doc, 0);
  }
  DeleteDC(hdc);
  dt_win_print_set_abort_state(previous_abort_state);
  result = _dt_win_cleanup_temporary_output(output_temp_filename,
                                             pinfo->printer.name,
                                             output_unpublished_committed,
                                             result, error);
  g_free(devmode);
  g_free(printer_name);
  g_free(title);
  g_free(output_filename);
  g_free(output_temp_filename);

  return result;
}

#endif // _WIN32

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
