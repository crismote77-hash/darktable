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
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

static void _dt_cups_set_or_append_error(GError **error,
                                         const GQuark domain,
                                         const int code,
                                         const char *message)
{
  if(!error || !message) return;

  if(*error)
  {
    const GQuark primary_domain = (*error)->domain;
    const int primary_code = (*error)->code;
    char *combined = g_strdup_printf("%s; %s", (*error)->message, message);
    g_clear_error(error);
    g_set_error_literal(error, primary_domain, primary_code, combined);
    g_free(combined);
  }
  else
    g_set_error_literal(error, domain, code, message);
}

int dt_cups_set_color_options(const gboolean application_managed,
                              const gboolean set_apple_color_matching,
                              int num_options,
                              cups_option_t **options)
{
  num_options = cupsAddOption("cm-calibration",
                              application_managed ? "true" : "false",
                              num_options, options);

  if(set_apple_color_matching)
  {
    const char *const color_matching = application_managed
      ? "AP_ApplicationColorMatching"
      : "AP_VendorColorMatching";
    num_options = cupsAddOption("AP.ColorMatchingMode", color_matching,
                                num_options, options);
    num_options = cupsAddOption("AP_ColorMatchingMode", color_matching,
                                num_options, options);
  }

  return num_options;
}

dt_print_result_t dt_cups_cleanup_temporary_file(
  const char *path,
  const dt_print_result_t result,
  const dt_cups_unlink_func unlink_operation,
  GError **error)
{
  int unlink_errno = EINVAL;
  if(path && *path && unlink_operation)
  {
    errno = 0;
    if(unlink_operation(path) == 0) return result;
    unlink_errno = errno;
    if(unlink_errno == ENOENT) return result;
    if(unlink_errno == 0) unlink_errno = EIO;
  }

  char *cleanup_message = g_strdup_printf(
    _("failed to remove temporary print file `%s': %s"),
    path ? path : "", g_strerror(unlink_errno));
  _dt_cups_set_or_append_error(error, G_FILE_ERROR,
                               g_file_error_from_errno(unlink_errno),
                               cleanup_message);
  g_free(cleanup_message);
  return DT_PRINT_RESULT_FAILED;
}

gboolean dt_cups_run_turboprint_options(const char *options_filename,
                                         gchar **argv,
                                         int *num_options,
                                         cups_option_t **options,
                                         GError **error)
{
  g_return_val_if_fail(options_filename != NULL, FALSE);
  g_return_val_if_fail(argv != NULL, FALSE);
  g_return_val_if_fail(num_options != NULL, FALSE);
  g_return_val_if_fail(options != NULL, FALSE);
  g_return_val_if_fail(*num_options == 0, FALSE);
  g_return_val_if_fail(*options == NULL, FALSE);

  gboolean ok = FALSE;
  gint wait_status = 0;
  GError *command_error = NULL;
  FILE *stream = NULL;
  guint parsed_options = 0;

  const gboolean spawned = g_spawn_sync(
    NULL, argv, NULL,
    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
    NULL, NULL, NULL, NULL, &wait_status, &command_error);
  if(!spawned)
  {
    g_propagate_error(error, command_error);
    command_error = NULL;
    goto cleanup;
  }

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  const gboolean exited_successfully =
    g_spawn_check_exit_status(wait_status, &command_error);
  G_GNUC_END_IGNORE_DEPRECATIONS
  if(!exited_successfully)
  {
    g_propagate_error(error, command_error);
    command_error = NULL;
    goto cleanup;
  }

  stream = g_fopen(options_filename, "rb");
  if(!stream)
  {
    const int saved_errno = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                _("failed to open TurboPrint options file: %s"),
                g_strerror(saved_errno));
    goto cleanup;
  }

  while(TRUE)
  {
    char optname[100];
    char optvalue[100];
    const int parsed = fscanf(stream, "%*s %99[^= ]=%99s", optname, optvalue);
    if(parsed == 2)
    {
      char *value = optvalue;
      if(*value == '\'') value++;
      const size_t value_length = strlen(value);
      if(value_length > 0 && value[value_length - 1] == '\'')
        value[value_length - 1] = '\0';
      *num_options = cupsAddOption(optname, value, *num_options, options);
      parsed_options++;
    }
    else if(parsed == EOF)
    {
      if(ferror(stream))
      {
        const int saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    _("failed to read TurboPrint options file: %s"),
                    g_strerror(saved_errno));
      }
      else if(parsed_options == 0)
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            _("empty TurboPrint options file"));
      else
        ok = TRUE;
      break;
    }
    else
    {
      g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                          _("invalid TurboPrint options file"));
      break;
    }
  }

cleanup:
  if(stream && fclose(stream) != 0)
  {
    const int saved_errno = errno;
    char *message = g_strdup_printf(
      _("failed to close TurboPrint options file: %s"),
      g_strerror(saved_errno));
    _dt_cups_set_or_append_error(error, G_FILE_ERROR,
                                 g_file_error_from_errno(saved_errno), message);
    g_free(message);
    ok = FALSE;
  }

  errno = 0;
  if(g_unlink(options_filename) != 0 && errno != ENOENT)
  {
    const int saved_errno = errno ? errno : EIO;
    char *message = g_strdup_printf(
      _("failed to remove TurboPrint options file: %s"),
      g_strerror(saved_errno));
    _dt_cups_set_or_append_error(error, G_FILE_ERROR,
                                 g_file_error_from_errno(saved_errno), message);
    g_free(message);
    ok = FALSE;
  }

  g_clear_error(&command_error);
  if(!ok)
  {
    cupsFreeOptions(*num_options, *options);
    *num_options = 0;
    *options = NULL;
  }
  return ok;
}

dt_cups_cancel_result_t dt_cups_cancel_submitted_job(
  const char *printer_name,
  const int job_id,
  const gboolean cancellation_requested,
  const dt_cups_cancel_job_func cancel_job,
  ipp_status_t *cancel_status)
{
  if(cancel_status) *cancel_status = IPP_STATUS_OK;
  if(!cancellation_requested || job_id <= 0) return DT_CUPS_CANCEL_NOT_REQUESTED;

  if(!cancel_job)
  {
    if(cancel_status) *cancel_status = IPP_STATUS_ERROR_INTERNAL;
    return DT_CUPS_CANCEL_FAILED;
  }

  const ipp_status_t status =
    cancel_job(CUPS_HTTP_DEFAULT, printer_name, job_id, 0);
  if(cancel_status) *cancel_status = status;

  return status < IPP_STATUS_REDIRECTION_OTHER_SITE
    ? DT_CUPS_CANCEL_SUCCEEDED
    : DT_CUPS_CANCEL_FAILED;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
