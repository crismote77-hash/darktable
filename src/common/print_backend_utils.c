/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/print_backend_utils.h"

#include <string.h>

dt_print_pdf_icc_kind_t dt_print_pdf_icc_kind(const dt_print_color_mode_t color_mode,
                                               const gboolean is_macos)
{
  if(!is_macos) return DT_PRINT_PDF_ICC_NONE;

  return color_mode == DT_PRINT_COLOR_DARKTABLE_MANAGED
           ? DT_PRINT_PDF_ICC_DESTINATION
           : DT_PRINT_PDF_ICC_IMAGE_SOURCE;
}

gboolean dt_print_result_is_failure(const dt_print_result_t result)
{
  return result != DT_PRINT_RESULT_SUCCESS
         && result != DT_PRINT_RESULT_CANCELLED;
}

dt_print_result_t dt_print_result_after_backend(const dt_print_result_t backend_result,
                                                const gboolean cancelled_after_return)
{
  return backend_result == DT_PRINT_RESULT_SUCCESS && cancelled_after_return
           ? DT_PRINT_RESULT_FAILED : backend_result;
}

dt_print_result_t dt_print_result_before_metadata(
  const dt_print_result_t result,
  const gboolean cancelled_before_metadata)
{
  return dt_print_result_after_backend(result, cancelled_before_metadata);
}

gboolean dt_print_result_allows_metadata(const dt_print_result_t result)
{
  return result == DT_PRINT_RESULT_SUCCESS;
}

dt_print_result_t dt_print_cancellation_result(const dt_print_cancel_stage_t stage,
                                               const gboolean abort_succeeded)
{
  switch(stage)
  {
    case DT_PRINT_CANCEL_PRE_COMMIT:
    case DT_PRINT_CANCEL_AFTER_UNPUBLISHED_FILE_COMMIT:
      return DT_PRINT_RESULT_CANCELLED;
    case DT_PRINT_CANCEL_ABORT_DOC:
      return abort_succeeded ? DT_PRINT_RESULT_CANCELLED : DT_PRINT_RESULT_FAILED;
    case DT_PRINT_CANCEL_AFTER_PHYSICAL_COMMIT:
    default:
      return DT_PRINT_RESULT_FAILED;
  }
}

dt_print_result_t dt_print_result_after_cleanup(const dt_print_result_t result,
                                                const gboolean cleanup_succeeded)
{
  return cleanup_succeeded ? result : DT_PRINT_RESULT_FAILED;
}

gboolean dt_print_store_name(char *destination,
                             const gsize destination_size,
                             const char *source)
{
  if(destination && destination_size > 0) destination[0] = '\0';
  if(!destination || destination_size == 0 || !source) return FALSE;

  const gsize length = strlen(source);
  if(length >= destination_size) return FALSE;

  memcpy(destination, source, length + 1);
  return TRUE;
}

static gint _dt_print_sort_papers_by_display_name(gconstpointer p1,
                                                   gconstpointer p2,
                                                   gpointer user_data)
{
  (void)user_data;
  const dt_paper_info_t *paper1 = (const dt_paper_info_t *)p1;
  const dt_paper_info_t *paper2 = (const dt_paper_info_t *)p2;
  return g_strcmp0(paper1 ? paper1->common_name : NULL,
                   paper2 ? paper2->common_name : NULL);
}

GList *dt_print_sort_papers_default_first(GList *papers,
                                          const char *default_name)
{
  gpointer default_paper = NULL;
  if(default_name)
  {
    for(GList *item = papers; item; item = g_list_next(item))
    {
      const dt_paper_info_t *paper = (const dt_paper_info_t *)item->data;
      if(paper && !g_strcmp0(paper->name, default_name))
      {
        default_paper = item->data;
        papers = g_list_delete_link(papers, item);
        break;
      }
    }
  }

  papers = g_list_sort_with_data(papers, _dt_print_sort_papers_by_display_name, NULL);
  return default_paper ? g_list_prepend(papers, default_paper) : papers;
}

GList *dt_print_printer_names_merge_new(GList **displayed_names,
                                        const GList *discovered_names)
{
  if(!displayed_names) return NULL;

  GList *added_names = NULL;
  for(const GList *item = discovered_names; item; item = g_list_next(item))
  {
    const char *name = item->data;
    if(!name || !*name || g_list_find_custom(*displayed_names, name,
                                             (GCompareFunc)g_strcmp0))
      continue;

    char *stored_name = g_strdup(name);
    *displayed_names = g_list_append(*displayed_names, stored_name);
    added_names = g_list_append(added_names, stored_name);
  }
  return added_names;
}

const char *dt_print_printer_name_to_select(const GList *displayed_names,
                                            const char *current_name,
                                            const char *preferred_name,
                                            const gboolean discovery_settled)
{
  for(const GList *item = displayed_names; item; item = g_list_next(item))
    if(!g_strcmp0(item->data, current_name)) return item->data;

  for(const GList *item = displayed_names; item; item = g_list_next(item))
    if(!g_strcmp0(item->data, preferred_name)) return item->data;

  return discovery_settled && displayed_names ? displayed_names->data : NULL;
}

guint dt_print_printer_refresh_reduce(dt_print_printer_refresh_state_t *state,
                                      const dt_print_printer_refresh_event_t event,
                                      const gboolean discovery_settled)
{
  if(!state) return DT_PRINT_PRINTER_REFRESH_NONE;

  switch(event)
  {
    case DT_PRINT_PRINTER_REFRESH_INIT:
      if(!state->discovery_active)
      {
        state->discovery_active = TRUE;
        state->restart_requested = FALSE;
        return DT_PRINT_PRINTER_REFRESH_START_DISCOVERY
               | DT_PRINT_PRINTER_REFRESH_KEEP_TIMER;
      }
      if(state->restart_requested && discovery_settled)
      {
        state->restart_requested = FALSE;
        return DT_PRINT_PRINTER_REFRESH_START_DISCOVERY
               | DT_PRINT_PRINTER_REFRESH_KEEP_TIMER;
      }
      return DT_PRINT_PRINTER_REFRESH_KEEP_TIMER;

    case DT_PRINT_PRINTER_REFRESH_TICK:
      if(!state->discovery_active) return DT_PRINT_PRINTER_REFRESH_NONE;
      if(state->restart_requested)
      {
        if(!discovery_settled) return DT_PRINT_PRINTER_REFRESH_KEEP_TIMER;
        state->restart_requested = FALSE;
        return DT_PRINT_PRINTER_REFRESH_START_DISCOVERY
               | DT_PRINT_PRINTER_REFRESH_KEEP_TIMER;
      }
      if(!discovery_settled)
        return DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE
               | DT_PRINT_PRINTER_REFRESH_KEEP_TIMER;
      state->discovery_active = FALSE;
      return DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE;

    case DT_PRINT_PRINTER_REFRESH_CLEANUP:
      if(state->discovery_active)
      {
        state->restart_requested = TRUE;
        return DT_PRINT_PRINTER_REFRESH_ABORT_DISCOVERY;
      }
      return DT_PRINT_PRINTER_REFRESH_NONE;

    default:
      return DT_PRINT_PRINTER_REFRESH_NONE;
  }
}

static int _dt_print_named_item_index(GList *items,
                                      const char *saved_key,
                                      const gsize name_offset,
                                      const gsize common_name_offset)
{
  if(!saved_key) return -1;

  int index = 0;
  for(GList *item = items; item; item = g_list_next(item), index++)
  {
    const char *data = item->data;
    if(data && !g_strcmp0(data + name_offset, saved_key)) return index;
  }

  index = 0;
  for(GList *item = items; item; item = g_list_next(item), index++)
  {
    const char *data = item->data;
    if(data && !g_strcmp0(data + common_name_offset, saved_key)) return index;
  }

  return -1;
}

int dt_print_paper_index(GList *papers, const char *saved_key)
{
  return _dt_print_named_item_index(papers, saved_key,
                                    G_STRUCT_OFFSET(dt_paper_info_t, name),
                                    G_STRUCT_OFFSET(dt_paper_info_t, common_name));
}

int dt_print_medium_index(GList *media, const char *saved_key)
{
  return _dt_print_named_item_index(media, saved_key,
                                    G_STRUCT_OFFSET(dt_medium_info_t, name),
                                    G_STRUCT_OFFSET(dt_medium_info_t, common_name));
}

const dt_paper_info_t *dt_print_paper_at(GList *papers, const int index)
{
  return index >= 0 ? g_list_nth_data(papers, index) : NULL;
}

const dt_medium_info_t *dt_print_medium_at(GList *media, const int index)
{
  return index >= 0 ? g_list_nth_data(media, index) : NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
