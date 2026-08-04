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

typedef enum dt_print_pdf_icc_kind_t
{
  DT_PRINT_PDF_ICC_NONE,
  DT_PRINT_PDF_ICC_DESTINATION,
  DT_PRINT_PDF_ICC_IMAGE_SOURCE
} dt_print_pdf_icc_kind_t;

typedef enum dt_print_cancel_stage_t
{
  DT_PRINT_CANCEL_PRE_COMMIT,
  DT_PRINT_CANCEL_ABORT_DOC,
  DT_PRINT_CANCEL_AFTER_PHYSICAL_COMMIT,
  DT_PRINT_CANCEL_AFTER_UNPUBLISHED_FILE_COMMIT
} dt_print_cancel_stage_t;

typedef enum dt_print_printer_refresh_event_t
{
  DT_PRINT_PRINTER_REFRESH_INIT,
  DT_PRINT_PRINTER_REFRESH_TICK,
  DT_PRINT_PRINTER_REFRESH_CLEANUP
} dt_print_printer_refresh_event_t;

typedef enum dt_print_printer_refresh_action_t
{
  DT_PRINT_PRINTER_REFRESH_NONE = 0,
  DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE = 1 << 0,
  DT_PRINT_PRINTER_REFRESH_START_DISCOVERY = 1 << 1,
  DT_PRINT_PRINTER_REFRESH_ABORT_DISCOVERY = 1 << 2,
  DT_PRINT_PRINTER_REFRESH_KEEP_TIMER = 1 << 3
} dt_print_printer_refresh_action_t;

typedef struct dt_print_printer_refresh_state_t
{
  gboolean discovery_active;
  gboolean restart_requested;
} dt_print_printer_refresh_state_t;

dt_print_pdf_icc_kind_t dt_print_pdf_icc_kind(dt_print_color_mode_t color_mode,
                                               gboolean is_macos);

gboolean dt_print_result_is_failure(dt_print_result_t result);
dt_print_result_t dt_print_result_after_backend(dt_print_result_t backend_result,
                                                gboolean cancelled_after_return);
dt_print_result_t dt_print_result_before_metadata(dt_print_result_t result,
                                                  gboolean cancelled_before_metadata);
gboolean dt_print_result_allows_metadata(dt_print_result_t result);
dt_print_result_t dt_print_cancellation_result(dt_print_cancel_stage_t stage,
                                               gboolean abort_succeeded);
dt_print_result_t dt_print_result_after_cleanup(dt_print_result_t result,
                                                gboolean cleanup_succeeded);

gboolean dt_print_store_name(char *destination,
                             gsize destination_size,
                             const char *source);
GList *dt_print_sort_papers_default_first(GList *papers,
                                           const char *default_name);

GList *dt_print_printer_names_merge_new(GList **displayed_names,
                                        const GList *discovered_names);
const char *dt_print_printer_name_to_select(const GList *displayed_names,
                                            const char *current_name,
                                            const char *preferred_name,
                                            gboolean discovery_settled);
guint dt_print_printer_refresh_reduce(dt_print_printer_refresh_state_t *state,
                                      dt_print_printer_refresh_event_t event,
                                      gboolean discovery_settled);

int dt_print_paper_index(GList *papers, const char *saved_key);
int dt_print_medium_index(GList *media, const char *saved_key);

const dt_paper_info_t *dt_print_paper_at(GList *papers, int index);
const dt_medium_info_t *dt_print_medium_at(GList *media, int index);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
