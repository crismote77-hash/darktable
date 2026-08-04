/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#pragma once

#include "common/print_limits.h"

#include <glib.h>
#include <lcms2.h>

#ifdef _WIN32
#include <wchar.h>
typedef struct _devicemodeW DEVMODEW;
#endif

gboolean dt_win_print_store_printer_name(char *destination,
                                         gsize destination_size,
                                         const char *printer_name);

gboolean dt_win_print_page_size_mm(int physical_width,
                                   int physical_height,
                                   int dpi_x,
                                   int dpi_y,
                                   double *width_mm,
                                   double *height_mm);

gboolean dt_win_print_map_intent(const int intent,
                                 guint32 *bitmap_intent,
                                 guint32 *devmode_intent);

gboolean dt_win_print_get_band_layout(const int width,
                                      const int height,
                                      gsize *source_row_bytes,
                                      int *band_rows,
                                      gsize *band_bytes);

gboolean dt_win_print_get_bitmap_header_size(const gsize profile_size,
                                             gsize *header_size);

#ifdef _WIN32
gboolean dt_win_print_store_wide_slot_name(char *destination,
                                           gsize destination_size,
                                           const wchar_t *slot,
                                           gsize slot_characters);

typedef enum dt_win_print_paper_mode_t
{
  DT_WIN_PRINT_PAPER_DEFAULT,
  DT_WIN_PRINT_PAPER_STOCK,
  DT_WIN_PRINT_PAPER_CUSTOM
} dt_win_print_paper_mode_t;

gboolean dt_win_print_devmode_layout_valid(const DEVMODEW *devmode,
                                           gsize allocation_size);

gboolean dt_win_print_devmode_field_available(const DEVMODEW *devmode,
                                              gsize allocation_size,
                                              gsize field_offset,
                                              gsize field_size);

guint32 dt_win_print_select_icm_method(gboolean darktable_managed,
                                        guint32 driver_method);

gboolean dt_win_print_set_paper_fields(DEVMODEW *devmode,
                                       gsize allocation_size,
                                       dt_win_print_paper_mode_t mode,
                                       short paper_size,
                                       short paper_width,
                                       short paper_length);

gboolean dt_win_print_validate_paper_fields(const DEVMODEW *devmode,
                                             gsize allocation_size,
                                             dt_win_print_paper_mode_t mode,
                                             short requested_paper_size,
                                             short requested_paper_width,
                                             short requested_paper_length,
                                             const char *requested_form_name);

gboolean dt_win_print_validate_copies(const DEVMODEW *devmode,
                                      gsize allocation_size);

gboolean dt_win_print_validate_color_fields(const DEVMODEW *devmode,
                                             gsize allocation_size,
                                             guint32 requested_fields,
                                             guint32 requested_method,
                                             guint32 requested_intent);

gboolean dt_win_print_normalize_page_size(double *width, double *height);

gboolean dt_win_print_margins_to_landscape(double *left,
                                            double *top,
                                            double *right,
                                            double *bottom);

gboolean dt_win_print_margins_to_portrait(double *left,
                                           double *top,
                                           double *right,
                                           double *bottom);

gboolean dt_win_print_output_path_available(const wchar_t *path,
                                             gulong *error_code);

gboolean dt_win_print_prepare_output_path(const wchar_t *destination,
                                          wchar_t **temporary_path,
                                          gulong *error_code);

gboolean dt_win_print_publish_output(const wchar_t *temporary_path,
                                      const wchar_t *destination,
                                      gulong *error_code);

typedef enum dt_win_print_output_probe_result_t
{
  DT_WIN_PRINT_OUTPUT_PROBE_ABSENT,
  DT_WIN_PRINT_OUTPUT_PROBE_INCOMPLETE,
  DT_WIN_PRINT_OUTPUT_PROBE_READY,
  DT_WIN_PRINT_OUTPUT_PROBE_INVALID,
  DT_WIN_PRINT_OUTPUT_PROBE_ERROR
} dt_win_print_output_probe_result_t;

typedef enum dt_win_print_output_wait_result_t
{
  DT_WIN_PRINT_OUTPUT_WAIT_READY,
  DT_WIN_PRINT_OUTPUT_WAIT_TIMEOUT,
  DT_WIN_PRINT_OUTPUT_WAIT_INVALID,
  DT_WIN_PRINT_OUTPUT_WAIT_ERROR
} dt_win_print_output_wait_result_t;

typedef dt_win_print_output_probe_result_t (*dt_win_print_output_probe_t)(
  const wchar_t *path,
  guint64 *size,
  gulong *error_code,
  gpointer user_data);

typedef void (*dt_win_print_sleep_t)(guint milliseconds, gpointer user_data);

dt_win_print_output_probe_result_t dt_win_print_probe_output(
  const wchar_t *path,
  guint64 *size,
  gulong *error_code,
  gpointer user_data);

dt_win_print_output_wait_result_t dt_win_print_wait_for_output_with_ops(
  const wchar_t *path,
  guint max_probes,
  guint delay_milliseconds,
  dt_win_print_output_probe_t probe_operation,
  dt_win_print_sleep_t sleep_operation,
  gpointer user_data,
  guint64 *stable_size,
  gulong *error_code);

dt_win_print_output_wait_result_t dt_win_print_wait_for_output(
  const wchar_t *path,
  guint64 *stable_size,
  gulong *error_code);

gboolean dt_win_print_output_requires_committed_cleanup(
  gboolean redirected_output,
  gboolean document_started,
  gboolean end_doc_succeeded,
  gboolean abort_doc_succeeded);

typedef enum dt_win_print_temp_cleanup_result_t
{
  DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE,
  DT_WIN_PRINT_TEMP_CLEANUP_SCHEDULED,
  DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED
} dt_win_print_temp_cleanup_result_t;

typedef gboolean (*dt_win_print_file_operation_t)(const wchar_t *path,
                                                   gulong *error_code);

typedef gboolean (*dt_win_print_file_presence_operation_t)(
  const wchar_t *path,
  gboolean *present,
  gulong *error_code);

dt_win_print_temp_cleanup_result_t dt_win_print_cleanup_output_temp_with_ops(
  const wchar_t *temporary_path,
  dt_win_print_file_operation_t delete_operation,
  dt_win_print_file_operation_t schedule_operation,
  gulong *error_code);

dt_win_print_temp_cleanup_result_t dt_win_print_cleanup_output_temp(
  const wchar_t *temporary_path,
  gulong *error_code);

dt_win_print_temp_cleanup_result_t
dt_win_print_cleanup_committed_output_temp_with_ops(
  const wchar_t *temporary_path,
  dt_win_print_file_presence_operation_t presence_operation,
  dt_win_print_file_operation_t delete_operation,
  dt_win_print_file_operation_t schedule_operation,
  gulong *error_code);

dt_win_print_temp_cleanup_result_t dt_win_print_cleanup_committed_output_temp(
  const wchar_t *temporary_path,
  gulong *error_code);

void dt_win_print_set_abort_state(gpointer state);
gpointer dt_win_print_get_abort_state(void);
#endif
