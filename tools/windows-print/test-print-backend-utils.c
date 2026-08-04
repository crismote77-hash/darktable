/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/print_backend_utils.h"

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

static void _test_pdf_icc_selection(void)
{
  g_assert_cmpint(dt_print_pdf_icc_kind(DT_PRINT_COLOR_DARKTABLE_MANAGED, TRUE),
                  ==, DT_PRINT_PDF_ICC_DESTINATION);
  g_assert_cmpint(dt_print_pdf_icc_kind(DT_PRINT_COLOR_DRIVER_MANAGED, TRUE),
                  ==, DT_PRINT_PDF_ICC_IMAGE_SOURCE);
  g_assert_cmpint(dt_print_pdf_icc_kind(DT_PRINT_COLOR_DARKTABLE_MANAGED, FALSE),
                  ==, DT_PRINT_PDF_ICC_NONE);
  g_assert_cmpint(dt_print_pdf_icc_kind(DT_PRINT_COLOR_DRIVER_MANAGED, FALSE),
                  ==, DT_PRINT_PDF_ICC_NONE);
}

static void _test_cancelled_result_is_not_backend_failure(void)
{
  g_assert_false(dt_print_result_is_failure(DT_PRINT_RESULT_SUCCESS));
  g_assert_false(dt_print_result_is_failure(DT_PRINT_RESULT_CANCELLED));
  g_assert_true(dt_print_result_is_failure(DT_PRINT_RESULT_FAILED));
  g_assert_true(dt_print_result_is_failure((dt_print_result_t)99));
}

static void _test_late_cancellation_after_backend_success_is_uncertain(void)
{
  const dt_print_result_t backend_checkpoint =
    dt_print_result_after_backend(DT_PRINT_RESULT_SUCCESS, TRUE);

  g_assert_cmpint(backend_checkpoint, ==, DT_PRINT_RESULT_FAILED);
  g_assert_false(dt_print_result_allows_metadata(backend_checkpoint));
  g_assert_cmpint(dt_print_result_after_backend(DT_PRINT_RESULT_SUCCESS, FALSE),
                  ==, DT_PRINT_RESULT_SUCCESS);
  g_assert_true(dt_print_result_allows_metadata(DT_PRINT_RESULT_SUCCESS));
  g_assert_cmpint(dt_print_result_after_backend(DT_PRINT_RESULT_CANCELLED, TRUE),
                  ==, DT_PRINT_RESULT_CANCELLED);
  g_assert_cmpint(dt_print_result_after_backend(DT_PRINT_RESULT_FAILED, TRUE),
                  ==, DT_PRINT_RESULT_FAILED);

  const dt_print_result_t accepted_backend =
    dt_print_result_after_backend(DT_PRINT_RESULT_SUCCESS, FALSE);
  const dt_print_result_t metadata_checkpoint =
    dt_print_result_before_metadata(accepted_backend, TRUE);
  g_assert_cmpint(metadata_checkpoint, ==, DT_PRINT_RESULT_FAILED);
  g_assert_false(dt_print_result_allows_metadata(metadata_checkpoint));
  g_assert_cmpint(dt_print_result_before_metadata(accepted_backend, FALSE),
                  ==, DT_PRINT_RESULT_SUCCESS);
  g_assert_cmpint(dt_print_result_before_metadata(DT_PRINT_RESULT_FAILED, TRUE),
                  ==, DT_PRINT_RESULT_FAILED);
}

static void _test_cancellation_and_cleanup_result_transitions(void)
{
  g_assert_cmpint(dt_print_cancellation_result(DT_PRINT_CANCEL_PRE_COMMIT, FALSE),
                  ==, DT_PRINT_RESULT_CANCELLED);
  g_assert_cmpint(dt_print_cancellation_result(DT_PRINT_CANCEL_ABORT_DOC, TRUE),
                  ==, DT_PRINT_RESULT_CANCELLED);
  g_assert_cmpint(dt_print_cancellation_result(DT_PRINT_CANCEL_ABORT_DOC, FALSE),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_cmpint(dt_print_cancellation_result(
                    DT_PRINT_CANCEL_AFTER_PHYSICAL_COMMIT, FALSE),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_cmpint(dt_print_cancellation_result(
                    DT_PRINT_CANCEL_AFTER_UNPUBLISHED_FILE_COMMIT, FALSE),
                  ==, DT_PRINT_RESULT_CANCELLED);
  g_assert_cmpint(dt_print_result_after_cleanup(DT_PRINT_RESULT_CANCELLED, FALSE),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_cmpint(dt_print_result_after_cleanup(DT_PRINT_RESULT_FAILED, FALSE),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_cmpint(dt_print_result_after_cleanup(DT_PRINT_RESULT_CANCELLED, TRUE),
                  ==, DT_PRINT_RESULT_CANCELLED);
}

static void _test_default_paper_sorts_first_by_stable_identity(void)
{
  dt_paper_info_t a4 = { .name = "a4", .common_name = "AAA stock" };
  dt_paper_info_t default_paper =
    { .name = "win_default", .common_name = "ZZZ localized default" };
  dt_paper_info_t letter = { .name = "letter", .common_name = "MMM stock" };
  GList *papers = NULL;
  papers = g_list_append(papers, &letter);
  papers = g_list_append(papers, &default_paper);
  papers = g_list_append(papers, &a4);

  papers = dt_print_sort_papers_default_first(papers, "win_default");

  g_assert_true(g_list_nth_data(papers, 0) == &default_paper);
  g_assert_true(g_list_nth_data(papers, 1) == &a4);
  g_assert_true(g_list_nth_data(papers, 2) == &letter);
  g_list_free(papers);
}

static void _test_stable_media_key_boundary_rejects_without_truncation(void)
{
  dt_paper_info_t paper = { 0 };
  char maximum_key[256];
  memset(maximum_key, 'k', sizeof(maximum_key) - 1);
  maximum_key[sizeof(maximum_key) - 1] = '\0';

  g_assert_true(dt_print_store_name(paper.name, sizeof(paper.name), maximum_key));
  g_assert_cmpstr(paper.name, ==, maximum_key);

  char oversized_key[257];
  memset(oversized_key, 'x', sizeof(oversized_key) - 1);
  oversized_key[sizeof(oversized_key) - 1] = '\0';
  g_assert_false(dt_print_store_name(paper.name, sizeof(paper.name), oversized_key));
  g_assert_cmpint(paper.name[0], ==, '\0');
}

static void _test_paper_identity_precedes_duplicate_label_fallback(void)
{
  dt_paper_info_t first = { .name = "paper-a", .common_name = "paper-b" };
  dt_paper_info_t second = { .name = "paper-b", .common_name = "paper-b" };
  GList *papers = NULL;
  papers = g_list_append(papers, &first);
  papers = g_list_append(papers, &second);

  g_assert_cmpint(dt_print_paper_index(papers, "paper-b"), ==, 1);
  g_assert_true(dt_print_paper_at(papers, 1) == &second);
  g_assert_null(dt_print_paper_at(papers, -1));
  g_assert_null(dt_print_paper_at(papers, 2));

  g_list_free(papers);
}

static void _test_paper_common_name_is_migration_fallback(void)
{
  dt_paper_info_t first = { .name = "paper-a", .common_name = "legacy label" };
  dt_paper_info_t second = { .name = "paper-b", .common_name = "legacy label" };
  GList *papers = NULL;
  papers = g_list_append(papers, &first);
  papers = g_list_append(papers, &second);

  g_assert_cmpint(dt_print_paper_index(papers, "legacy label"), ==, 0);
  g_assert_cmpint(dt_print_paper_index(papers, "missing"), ==, -1);
  g_assert_cmpint(dt_print_paper_index(papers, NULL), ==, -1);

  g_list_free(papers);
}

static void _test_medium_identity_precedes_duplicate_label_fallback(void)
{
  dt_medium_info_t first = { .name = "medium-a", .common_name = "medium-b" };
  dt_medium_info_t second = { .name = "medium-b", .common_name = "medium-b" };
  GList *media = NULL;
  media = g_list_append(media, &first);
  media = g_list_append(media, &second);

  g_assert_cmpint(dt_print_medium_index(media, "medium-b"), ==, 1);
  g_assert_true(dt_print_medium_at(media, 1) == &second);
  g_assert_cmpint(dt_print_medium_index(media, "missing"), ==, -1);
  g_assert_null(dt_print_medium_at(media, -1));
  g_assert_null(dt_print_medium_at(media, 2));

  g_list_free(media);
}

static void _test_medium_common_name_is_migration_fallback(void)
{
  dt_medium_info_t first = { .name = "medium-a", .common_name = "legacy label" };
  dt_medium_info_t second = { .name = "medium-b", .common_name = "legacy label" };
  GList *media = NULL;
  media = g_list_append(media, &first);
  media = g_list_append(media, &second);

  g_assert_cmpint(dt_print_medium_index(media, "legacy label"), ==, 0);
  g_assert_cmpint(dt_print_medium_index(media, NULL), ==, -1);

  g_list_free(media);
}

static void _test_late_printer_discovery_merges_without_duplicates(void)
{
  const char *initial_names[] =
  {
    "Microsoft Print to PDF",
    "Microsoft XPS Document Writer",
    "Fax",
    "Printer 4",
    "Printer 5",
    "Printer 6"
  };
  GList *discovered = NULL;
  for(guint k = 0; k < G_N_ELEMENTS(initial_names); k++)
    discovered = g_list_append(discovered, (gpointer)initial_names[k]);

  GList *displayed = NULL;
  GList *added = dt_print_printer_names_merge_new(&displayed, discovered);
  g_assert_cmpuint(g_list_length(added), ==, 6);
  g_assert_cmpuint(g_list_length(displayed), ==, 6);
  g_list_free(added);

  discovered = g_list_append(discovered, (gpointer)"Canon SELPHY CP1500");
  added = dt_print_printer_names_merge_new(&displayed, discovered);
  g_assert_cmpuint(g_list_length(added), ==, 1);
  g_assert_cmpstr(added->data, ==, "Canon SELPHY CP1500");
  g_assert_cmpuint(g_list_length(displayed), ==, 7);
  g_list_free(added);

  discovered = g_list_append(discovered, (gpointer)"Canon SELPHY CP1500");
  added = dt_print_printer_names_merge_new(&displayed, discovered);
  g_assert_null(added);
  g_assert_cmpuint(g_list_length(displayed), ==, 7);

  g_list_free(discovered);
  g_list_free_full(displayed, g_free);
}

static void _test_current_printer_selection_wins_when_displayed(void)
{
  GList *displayed = NULL;
  displayed = g_list_append(displayed, (gpointer)"Microsoft Print to PDF");
  displayed = g_list_append(displayed, (gpointer)"Epson ET-4850");

  g_assert_cmpstr(dt_print_printer_name_to_select(
                    displayed, "Epson ET-4850", "Microsoft Print to PDF", TRUE),
                  ==, "Epson ET-4850");

  g_list_free(displayed);
}

static void _test_preferred_printer_is_used_without_valid_current_selection(void)
{
  GList *displayed = NULL;
  displayed = g_list_append(displayed, (gpointer)"Microsoft Print to PDF");
  displayed = g_list_append(displayed, (gpointer)"Epson ET-4850");

  g_assert_cmpstr(dt_print_printer_name_to_select(
                    displayed, "Removed queue", "Epson ET-4850", TRUE),
                  ==, "Epson ET-4850");

  g_list_free(displayed);
}

static void _test_invalid_printer_selections_fall_back_to_first_displayed(void)
{
  GList *displayed = NULL;
  displayed = g_list_append(displayed, (gpointer)"Microsoft Print to PDF");
  displayed = g_list_append(displayed, (gpointer)"Epson ET-4850");

  g_assert_cmpstr(dt_print_printer_name_to_select(
                    displayed, "Removed queue", "Missing queue", TRUE),
                  ==, "Microsoft Print to PDF");

  g_list_free(displayed);
}

static void _test_no_displayed_printers_returns_no_selection(void)
{
  g_assert_null(dt_print_printer_name_to_select(
    NULL, "Epson ET-4850", "Microsoft Print to PDF", TRUE));
}

static void _test_settled_tick_finally_drains_last_cached_printer_once(void)
{
  dt_print_printer_refresh_state_t state = { 0 };
  GList *cached = NULL;
  GList *displayed = NULL;

  guint actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_INIT, TRUE);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_START_DISCOVERY);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_KEEP_TIMER);

  cached = g_list_append(cached, (gpointer)"Microsoft Print to PDF");
  actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_TICK, FALSE);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE);
  GList *snapshot = g_list_copy(cached);
  GList *added = dt_print_printer_names_merge_new(&displayed, snapshot);
  g_assert_cmpuint(g_list_length(added), ==, 1);
  g_list_free(added);
  g_list_free(snapshot);

  // The worker appends after the timer's cache copy, then publishes settled.
  cached = g_list_append(cached, (gpointer)"Canon SELPHY CP1500");
  actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_TICK, TRUE);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE);
  g_assert_false(actions & DT_PRINT_PRINTER_REFRESH_KEEP_TIMER);
  snapshot = g_list_copy(cached);
  added = dt_print_printer_names_merge_new(&displayed, snapshot);
  g_assert_cmpuint(g_list_length(added), ==, 1);
  g_assert_cmpstr(added->data, ==, "Canon SELPHY CP1500");
  g_list_free(added);
  g_list_free(snapshot);

  snapshot = g_list_copy(cached);
  added = dt_print_printer_names_merge_new(&displayed, snapshot);
  g_assert_null(added);
  g_assert_cmpuint(g_list_length(displayed), ==, 2);
  g_list_free(snapshot);

  g_list_free(cached);
  g_list_free_full(displayed, g_free);
}

static void _test_pending_preferred_printer_displaces_no_automatic_fallback(void)
{
  GList *displayed = NULL;
  displayed = g_list_append(displayed, g_strdup("Printer A"));

  g_assert_null(dt_print_printer_name_to_select(
    displayed, NULL, "Printer B", FALSE));

  displayed = g_list_append(displayed, g_strdup("Printer B"));
  g_assert_cmpstr(dt_print_printer_name_to_select(
                    displayed, NULL, "Printer B", FALSE),
                  ==, "Printer B");

  g_list_free_full(displayed, g_free);
}

static void _test_user_selection_during_discovery_wins_over_late_preferred(void)
{
  GList *displayed = NULL;
  displayed = g_list_append(displayed, g_strdup("Printer A"));
  g_assert_null(dt_print_printer_name_to_select(
    displayed, NULL, "Printer B", FALSE));

  // A real selection commits Printer A as the current printer.
  displayed = g_list_append(displayed, g_strdup("Printer B"));
  g_assert_cmpstr(dt_print_printer_name_to_select(
                    displayed, "Printer A", "Printer B", FALSE),
                  ==, "Printer A");

  g_list_free_full(displayed, g_free);
}

static void _test_cleanup_reinit_restarts_once_after_old_discovery_settles(void)
{
  dt_print_printer_refresh_state_t state =
  {
    .discovery_active = TRUE,
    .restart_requested = FALSE
  };
  guint restart_starts = 0;

  guint actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_CLEANUP, FALSE);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_ABORT_DISCOVERY);
  g_assert_true(state.restart_requested);

  actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_INIT, FALSE);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_KEEP_TIMER);
  g_assert_false(actions & DT_PRINT_PRINTER_REFRESH_START_DISCOVERY);

  actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_TICK, FALSE);
  g_assert_false(actions & DT_PRINT_PRINTER_REFRESH_START_DISCOVERY);
  g_assert_false(actions & DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE);

  actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_TICK, TRUE);
  restart_starts += !!(actions & DT_PRINT_PRINTER_REFRESH_START_DISCOVERY);
  g_assert_true(actions & DT_PRINT_PRINTER_REFRESH_KEEP_TIMER);
  g_assert_false(actions & DT_PRINT_PRINTER_REFRESH_DRAIN_CACHE);
  g_assert_false(state.restart_requested);

  // The fresh discovery is now active; another tick must not overlap it.
  actions = dt_print_printer_refresh_reduce(
    &state, DT_PRINT_PRINTER_REFRESH_TICK, FALSE);
  restart_starts += !!(actions & DT_PRINT_PRINTER_REFRESH_START_DISCOVERY);
  g_assert_cmpuint(restart_starts, ==, 1);
  g_assert_true(state.discovery_active);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/print/backend/pdf-icc-selection", _test_pdf_icc_selection);
  g_test_add_func("/print/backend/result-disposition",
                  _test_cancelled_result_is_not_backend_failure);
  g_test_add_func("/print/backend/late-cancellation",
                  _test_late_cancellation_after_backend_success_is_uncertain);
  g_test_add_func("/print/backend/cancellation-transitions",
                  _test_cancellation_and_cleanup_result_transitions);
  g_test_add_func("/print/backend/paper/default-first",
                  _test_default_paper_sorts_first_by_stable_identity);
  g_test_add_func("/print/backend/media/stable-key-boundary",
                  _test_stable_media_key_boundary_rejects_without_truncation);
  g_test_add_func("/print/backend/paper/stable-identity",
                  _test_paper_identity_precedes_duplicate_label_fallback);
  g_test_add_func("/print/backend/paper/legacy-label",
                  _test_paper_common_name_is_migration_fallback);
  g_test_add_func("/print/backend/medium/stable-identity",
                  _test_medium_identity_precedes_duplicate_label_fallback);
  g_test_add_func("/print/backend/medium/legacy-label",
                  _test_medium_common_name_is_migration_fallback);
  g_test_add_func("/print/backend/printers/late-discovery",
                  _test_late_printer_discovery_merges_without_duplicates);
  g_test_add_func("/print/backend/printers/current-selection-wins",
                  _test_current_printer_selection_wins_when_displayed);
  g_test_add_func("/print/backend/printers/preferred-selection-fallback",
                  _test_preferred_printer_is_used_without_valid_current_selection);
  g_test_add_func("/print/backend/printers/first-selection-fallback",
                  _test_invalid_printer_selections_fall_back_to_first_displayed);
  g_test_add_func("/print/backend/printers/no-selection",
                  _test_no_displayed_printers_returns_no_selection);
  g_test_add_func("/print/backend/printers/final-settled-drain",
                  _test_settled_tick_finally_drains_last_cached_printer_once);
  g_test_add_func("/print/backend/printers/pending-preferred",
                  _test_pending_preferred_printer_displaces_no_automatic_fallback);
  g_test_add_func("/print/backend/printers/user-selection-wins",
                  _test_user_selection_during_discovery_wins_over_late_preferred);
  g_test_add_func("/print/backend/printers/cleanup-reinit-restart",
                  _test_cleanup_reinit_restarts_once_after_old_discovery_settles);
  return g_test_run();
}
