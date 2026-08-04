/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/cups_print_utils.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static int _cancel_calls = 0;
static http_t *_cancel_http = (http_t *)1;
static const char *_cancel_printer = NULL;
static int _cancel_job_id = 0;
static int _cancel_purge = -1;
static ipp_status_t _cancel_status = IPP_STATUS_OK;

static ipp_status_t _fake_cancel_job(http_t *http,
                                     const char *printer_name,
                                     const int job_id,
                                     const int purge)
{
  _cancel_calls++;
  _cancel_http = http;
  _cancel_printer = printer_name;
  _cancel_job_id = job_id;
  _cancel_purge = purge;
  return _cancel_status;
}

static void _reset_cancel_fake(void)
{
  _cancel_calls = 0;
  _cancel_http = (http_t *)1;
  _cancel_printer = NULL;
  _cancel_job_id = 0;
  _cancel_purge = -1;
  _cancel_status = IPP_STATUS_OK;
}

static void _assert_color_options(const gboolean application_managed,
                                  const char *inherited_mode,
                                  const char *expected_mode,
                                  const char *expected_calibration)
{
  cups_option_t *options = NULL;
  int num_options = cupsAddOption("cm-calibration", "inherited",
                                  0, &options);
  num_options = cupsAddOption("AP.ColorMatchingMode", inherited_mode,
                              num_options, &options);
  num_options = cupsAddOption("AP_ColorMatchingMode", inherited_mode,
                              num_options, &options);

  num_options = dt_cups_set_color_options(application_managed, TRUE,
                                          num_options, &options);

  g_assert_cmpstr(cupsGetOption("cm-calibration", num_options, options),
                  ==, expected_calibration);
  g_assert_cmpstr(cupsGetOption("AP.ColorMatchingMode", num_options, options),
                  ==, expected_mode);
  g_assert_cmpstr(cupsGetOption("AP_ColorMatchingMode", num_options, options),
                  ==, expected_mode);
  cupsFreeOptions(num_options, options);
}

static void _test_application_color_options_replace_defaults(void)
{
  _assert_color_options(TRUE, "AP_VendorColorMatching",
                        "AP_ApplicationColorMatching", "true");
}

static void _test_driver_color_options_replace_defaults(void)
{
  _assert_color_options(FALSE, "AP_ApplicationColorMatching",
                        "AP_VendorColorMatching", "false");
}

static void _test_non_macos_color_options_preserve_apple_defaults(void)
{
  cups_option_t *options = NULL;
  int num_options = cupsAddOption("AP.ColorMatchingMode",
                                  "AP_ApplicationColorMatching", 0, &options);
  num_options = dt_cups_set_color_options(FALSE, FALSE, num_options, &options);

  g_assert_cmpstr(cupsGetOption("cm-calibration", num_options, options), ==, "false");
  g_assert_cmpstr(cupsGetOption("AP.ColorMatchingMode", num_options, options),
                  ==, "AP_ApplicationColorMatching");
  g_assert_null(cupsGetOption("AP_ColorMatchingMode", num_options, options));
  cupsFreeOptions(num_options, options);
}

static void _test_cancel_not_requested_does_not_call_spooler(void)
{
  _reset_cancel_fake();
  ipp_status_t status = IPP_STATUS_ERROR_INTERNAL;

  const dt_cups_cancel_result_t result =
    dt_cups_cancel_submitted_job("printer-a", 71, FALSE, _fake_cancel_job, &status);

  g_assert_cmpint(result, ==, DT_CUPS_CANCEL_NOT_REQUESTED);
  g_assert_cmpint(_cancel_calls, ==, 0);
  g_assert_cmpint(status, ==, IPP_STATUS_OK);
}

static void _test_cancel_zero_job_does_not_call_spooler(void)
{
  _reset_cancel_fake();
  ipp_status_t status = IPP_STATUS_ERROR_INTERNAL;

  const dt_cups_cancel_result_t result =
    dt_cups_cancel_submitted_job("printer-a", 0, TRUE, _fake_cancel_job, &status);

  g_assert_cmpint(result, ==, DT_CUPS_CANCEL_NOT_REQUESTED);
  g_assert_cmpint(_cancel_calls, ==, 0);
  g_assert_cmpint(status, ==, IPP_STATUS_OK);
}

static void _test_cancel_success_uses_exact_submitted_job(void)
{
  _reset_cancel_fake();
  ipp_status_t status = IPP_STATUS_ERROR_INTERNAL;

  const dt_cups_cancel_result_t result =
    dt_cups_cancel_submitted_job("printer-a", 71, TRUE, _fake_cancel_job, &status);

  g_assert_cmpint(result, ==, DT_CUPS_CANCEL_SUCCEEDED);
  g_assert_cmpint(_cancel_calls, ==, 1);
  g_assert_true(_cancel_http == CUPS_HTTP_DEFAULT);
  g_assert_cmpstr(_cancel_printer, ==, "printer-a");
  g_assert_cmpint(_cancel_job_id, ==, 71);
  g_assert_cmpint(_cancel_purge, ==, 0);
  g_assert_cmpint(status, ==, IPP_STATUS_OK);
}

static void _test_cancel_extended_success_is_reported(void)
{
  _reset_cancel_fake();
  _cancel_status = (ipp_status_t)(IPP_STATUS_OK_CONFLICTING + 1);
  ipp_status_t status = IPP_STATUS_ERROR_INTERNAL;

  g_assert_cmpint(_cancel_status, <, IPP_STATUS_REDIRECTION_OTHER_SITE);

  const dt_cups_cancel_result_t result =
    dt_cups_cancel_submitted_job("printer-a", 71, TRUE, _fake_cancel_job, &status);

  g_assert_cmpint(result, ==, DT_CUPS_CANCEL_SUCCEEDED);
  g_assert_cmpint(status, ==, _cancel_status);
}

static void _test_cancel_redirection_is_reported_as_failure(void)
{
  _reset_cancel_fake();
  _cancel_status = IPP_STATUS_REDIRECTION_OTHER_SITE;
  ipp_status_t status = IPP_STATUS_OK;

  const dt_cups_cancel_result_t result =
    dt_cups_cancel_submitted_job("printer-b", 92, TRUE, _fake_cancel_job, &status);

  g_assert_cmpint(result, ==, DT_CUPS_CANCEL_FAILED);
  g_assert_cmpint(status, ==, IPP_STATUS_REDIRECTION_OTHER_SITE);
}

static void _test_cancel_failure_is_reported(void)
{
  _reset_cancel_fake();
  _cancel_status = IPP_STATUS_ERROR_NOT_POSSIBLE;
  ipp_status_t status = IPP_STATUS_OK;

  const dt_cups_cancel_result_t result =
    dt_cups_cancel_submitted_job("printer-b", 92, TRUE, _fake_cancel_job, &status);

  g_assert_cmpint(result, ==, DT_CUPS_CANCEL_FAILED);
  g_assert_cmpint(_cancel_calls, ==, 1);
  g_assert_cmpstr(_cancel_printer, ==, "printer-b");
  g_assert_cmpint(_cancel_job_id, ==, 92);
  g_assert_cmpint(status, ==, IPP_STATUS_ERROR_NOT_POSSIBLE);
}

static int _test_unlink_succeeds(const char *path)
{
  g_assert_cmpstr(path, ==, "/tmp/image-bearing-print.pdf");
  return 0;
}

static int _test_unlink_fails(const char *path)
{
  g_assert_cmpstr(path, ==, "/tmp/image-bearing-print.pdf");
  errno = EACCES;
  return -1;
}

static int _test_unlink_reports_already_absent(const char *path)
{
  g_assert_cmpstr(path, ==, "/tmp/image-bearing-print.pdf");
  errno = ENOENT;
  return -1;
}

static int _test_unlink_fails_without_errno(const char *path)
{
  g_assert_cmpstr(path, ==, "/tmp/image-bearing-print.pdf");
  g_assert_cmpint(errno, ==, 0);
  return -1;
}

static void _test_pdf_cleanup_result_policy_preserves_primary_error(void)
{
  GError *error = g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                      "primary submission failure");
  g_assert_cmpint(dt_cups_cleanup_temporary_file(
                    "/tmp/image-bearing-print.pdf", DT_PRINT_RESULT_FAILED,
                    _test_unlink_fails, &error),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_nonnull(error);
  g_assert_true(g_str_has_prefix(error->message, "primary submission failure; "));
  g_assert_nonnull(strstr(error->message, "/tmp/image-bearing-print.pdf"));
  g_assert_nonnull(strstr(error->message, g_strerror(EACCES)));
  g_clear_error(&error);
}

static void _test_pdf_cleanup_failure_promotes_success_and_cancellation(void)
{
  GError *error = NULL;
  g_assert_cmpint(dt_cups_cleanup_temporary_file(
                    "/tmp/image-bearing-print.pdf", DT_PRINT_RESULT_SUCCESS,
                    _test_unlink_fails, &error),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_nonnull(error);
  g_clear_error(&error);

  g_assert_cmpint(dt_cups_cleanup_temporary_file(
                    "/tmp/image-bearing-print.pdf", DT_PRINT_RESULT_CANCELLED,
                    _test_unlink_fails, &error),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_nonnull(error);
  g_clear_error(&error);

  g_assert_cmpint(dt_cups_cleanup_temporary_file(
                    "/tmp/image-bearing-print.pdf", DT_PRINT_RESULT_CANCELLED,
                    _test_unlink_succeeds, &error),
                  ==, DT_PRINT_RESULT_CANCELLED);
  g_assert_no_error(error);

  g_assert_cmpint(dt_cups_cleanup_temporary_file(
                    "/tmp/image-bearing-print.pdf", DT_PRINT_RESULT_SUCCESS,
                    _test_unlink_reports_already_absent, &error),
                  ==, DT_PRINT_RESULT_SUCCESS);
  g_assert_no_error(error);
}

static void _test_pdf_cleanup_ignores_stale_errno(void)
{
  GError *error = g_error_new_literal(G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                      "primary submission failure");
  errno = ENOENT;
  g_assert_cmpint(dt_cups_cleanup_temporary_file(
                    "/tmp/image-bearing-print.pdf", DT_PRINT_RESULT_FAILED,
                    _test_unlink_fails_without_errno, &error),
                  ==, DT_PRINT_RESULT_FAILED);
  g_assert_nonnull(error);
  g_assert_true(g_str_has_prefix(error->message,
                                 "primary submission failure; "));
  g_assert_nonnull(strstr(error->message, g_strerror(EIO)));
  g_clear_error(&error);
}

static char *_new_options_file(void)
{
  char *path = g_build_filename(g_get_tmp_dir(), "dt cups options-XXXXXX", NULL);
  const int fd = g_mkstemp(path);
  g_assert_cmpint(fd, >=, 0);
  close(fd);
  return path;
}

static void _test_turboprint_success_reads_options_and_unlinks_file(void)
{
  char *path = _new_options_file();
  g_assert_cmpint(g_unlink(path), ==, 0);
  gchar *argv[] = {
    "sh", "-c",
    "printf '%s\\n' \"-o PageSize='A4'\" \"-o MediaType=Photo\" > \"$1\"",
    "darktable-turboprint-test", path, NULL
  };
  cups_option_t *options = NULL;
  int num_options = 0;
  GError *error = NULL;

  g_assert_true(dt_cups_run_turboprint_options(path, argv, &num_options,
                                                &options, &error));
  g_assert_no_error(error);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  g_assert_cmpstr(cupsGetOption("PageSize", num_options, options), ==, "A4");
  g_assert_cmpstr(cupsGetOption("MediaType", num_options, options), ==, "Photo");

  cupsFreeOptions(num_options, options);
  g_free(path);
}

static void _test_turboprint_spawn_failure_unlinks_file(void)
{
  char *path = _new_options_file();
  gchar *argv[] = { "darktable-command-that-does-not-exist", NULL };
  cups_option_t *options = NULL;
  int num_options = 0;
  GError *error = NULL;

  g_assert_false(dt_cups_run_turboprint_options(path, argv, &num_options,
                                                 &options, &error));
  g_assert_nonnull(error);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  g_assert_cmpint(num_options, ==, 0);
  g_assert_null(options);

  g_clear_error(&error);
  g_free(path);
}

static void _test_turboprint_child_failure_unlinks_file(void)
{
  char *path = _new_options_file();
  gchar *argv[] = { "sh", "-c", "exit 7", NULL };
  cups_option_t *options = NULL;
  int num_options = 0;
  GError *error = NULL;

  g_assert_false(dt_cups_run_turboprint_options(path, argv, &num_options,
                                                 &options, &error));
  g_assert_nonnull(error);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  g_assert_cmpint(num_options, ==, 0);
  g_assert_null(options);

  g_clear_error(&error);
  g_free(path);
}

static void _test_turboprint_missing_output_is_an_error(void)
{
  char *path = _new_options_file();
  g_assert_cmpint(g_unlink(path), ==, 0);
  gchar *argv[] = { "true", NULL };
  cups_option_t *options = NULL;
  int num_options = 0;
  GError *error = NULL;

  g_assert_false(dt_cups_run_turboprint_options(path, argv, &num_options,
                                                 &options, &error));
  g_assert_nonnull(error);
  g_assert_cmpint(num_options, ==, 0);
  g_assert_null(options);

  g_clear_error(&error);
  g_free(path);
}

static void _test_turboprint_empty_output_is_an_error(void)
{
  char *path = _new_options_file();
  gchar *argv[] = { "true", NULL };
  cups_option_t *options = NULL;
  int num_options = 0;
  GError *error = NULL;

  g_assert_false(dt_cups_run_turboprint_options(path, argv, &num_options,
                                                 &options, &error));
  g_assert_nonnull(error);
  g_assert_cmpint(error->domain, ==, G_FILE_ERROR);
  g_assert_cmpint(error->code, ==, G_FILE_ERROR_INVAL);
  g_assert_nonnull(strstr(error->message, "empty"));
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  g_assert_cmpint(num_options, ==, 0);
  g_assert_null(options);

  g_clear_error(&error);
  g_free(path);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/print/cups/options/application",
                  _test_application_color_options_replace_defaults);
  g_test_add_func("/print/cups/options/driver",
                  _test_driver_color_options_replace_defaults);
  g_test_add_func("/print/cups/options/non-macos",
                  _test_non_macos_color_options_preserve_apple_defaults);
  g_test_add_func("/print/cups/cancel/not-requested",
                  _test_cancel_not_requested_does_not_call_spooler);
  g_test_add_func("/print/cups/cancel/zero-job",
                  _test_cancel_zero_job_does_not_call_spooler);
  g_test_add_func("/print/cups/cancel/success",
                  _test_cancel_success_uses_exact_submitted_job);
  g_test_add_func("/print/cups/cancel/extended-success",
                  _test_cancel_extended_success_is_reported);
  g_test_add_func("/print/cups/cancel/redirection",
                  _test_cancel_redirection_is_reported_as_failure);
  g_test_add_func("/print/cups/cancel/failure",
                  _test_cancel_failure_is_reported);
  g_test_add_func("/print/cups/cleanup/preserve-primary",
                  _test_pdf_cleanup_result_policy_preserves_primary_error);
  g_test_add_func("/print/cups/cleanup/result-policy",
                  _test_pdf_cleanup_failure_promotes_success_and_cancellation);
  g_test_add_func("/print/cups/cleanup/stale-errno",
                  _test_pdf_cleanup_ignores_stale_errno);
  g_test_add_func("/print/cups/turboprint/success",
                  _test_turboprint_success_reads_options_and_unlinks_file);
  g_test_add_func("/print/cups/turboprint/spawn-failure",
                  _test_turboprint_spawn_failure_unlinks_file);
  g_test_add_func("/print/cups/turboprint/child-failure",
                  _test_turboprint_child_failure_unlinks_file);
  g_test_add_func("/print/cups/turboprint/missing-output",
                  _test_turboprint_missing_output_is_an_error);
  g_test_add_func("/print/cups/turboprint/empty-output",
                  _test_turboprint_empty_output_is_an_error);
  return g_test_run();
}
