/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#pragma once

#include "common/print_backend.h"

#include <cups/cups.h>
#include <glib.h>

typedef enum dt_cups_print_error_t
{
  DT_CUPS_PRINT_ERROR_FAILED = 1,
  DT_CUPS_PRINT_ERROR_CANCELLED,
  DT_CUPS_PRINT_ERROR_CANCEL_FAILED
} dt_cups_print_error_t;

typedef enum dt_cups_cancel_result_t
{
  DT_CUPS_CANCEL_NOT_REQUESTED,
  DT_CUPS_CANCEL_SUCCEEDED,
  DT_CUPS_CANCEL_FAILED
} dt_cups_cancel_result_t;

typedef ipp_status_t (*dt_cups_cancel_job_func)(http_t *http,
                                                 const char *printer_name,
                                                 int job_id,
                                                 int purge);

typedef int (*dt_cups_unlink_func)(const char *path);

int dt_cups_set_color_options(gboolean application_managed,
                              gboolean set_apple_color_matching,
                              int num_options,
                              cups_option_t **options);

gboolean dt_cups_run_turboprint_options(const char *options_filename,
                                         gchar **argv,
                                         int *num_options,
                                         cups_option_t **options,
                                         GError **error);

dt_cups_cancel_result_t dt_cups_cancel_submitted_job(
  const char *printer_name,
  int job_id,
  gboolean cancellation_requested,
  dt_cups_cancel_job_func cancel_job,
  ipp_status_t *cancel_status);

dt_print_result_t dt_cups_cleanup_temporary_file(
  const char *path,
  dt_print_result_t result,
  dt_cups_unlink_func unlink_operation,
  GError **error);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
