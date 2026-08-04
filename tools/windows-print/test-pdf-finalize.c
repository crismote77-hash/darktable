/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/pdf.h"

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

#include <errno.h>
#include <glib/gstdio.h>
#include <unistd.h>

const char darktable_package_string[] = "darktable pdf finalize test";

char *dt_read_file(const char *filename, size_t *filesize)
{
  (void)filename;
  if(filesize) *filesize = 0;
  return NULL;
}

static char *_temporary_pdf_path(void)
{
  char *path = g_build_filename(g_get_tmp_dir(), "darktable-pdf-finalize-XXXXXX", NULL);
  const int fd = g_mkstemp(path);
  g_assert_cmpint(fd, >=, 0);
  close(fd);
  g_assert_cmpint(g_unlink(path), ==, 0);
  return path;
}

static void _test_pdf_finish_reports_success(void)
{
  char *path = _temporary_pdf_path();
  dt_pdf_t *pdf = dt_pdf_start(path, 100.0f, 100.0f, 300.0f,
                               DT_PDF_STREAM_ENCODER_FLATE);
  g_assert_nonnull(pdf);
  char *owned_path = g_strdup(path);
  g_assert_true(dt_pdf_finish_output(pdf, NULL, 0, &owned_path));
  g_assert_null(owned_path);

  GStatBuf statbuf = { 0 };
  g_assert_cmpint(g_stat(path, &statbuf), ==, 0);
  g_assert_cmpint(statbuf.st_size, >, 0);
  g_assert_cmpint(g_unlink(path), ==, 0);
  g_free(path);
}

static void _test_pdf_finish_reports_stream_failure(void)
{
  char *path = _temporary_pdf_path();
  FILE *seed = g_fopen(path, "wb");
  g_assert_nonnull(seed);
  g_assert_cmpint(fputs("seed", seed), >=, 0);
  g_assert_cmpint(fclose(seed), ==, 0);

  FILE *read_only = g_fopen(path, "rb");
  g_assert_nonnull(read_only);
  dt_pdf_t *pdf = calloc(1, sizeof(*pdf));
  g_assert_nonnull(pdf);
  pdf->fd = read_only;
  pdf->next_id = 3;
  pdf->n_offsets = 4;
  pdf->offsets = calloc(pdf->n_offsets, sizeof(*pdf->offsets));
  g_assert_nonnull(pdf->offsets);

  g_assert_false(dt_pdf_finish(pdf, NULL, 0));
  g_assert_cmpint(g_unlink(path), ==, 0);
  g_free(path);
}

static void _test_pdf_finish_output_removes_failed_destination_and_clears_ownership(void)
{
  char *path = _temporary_pdf_path();
  FILE *seed = g_fopen(path, "wb");
  g_assert_nonnull(seed);
  g_assert_cmpint(fputs("incomplete", seed), >=, 0);
  g_assert_cmpint(fclose(seed), ==, 0);

  FILE *read_only = g_fopen(path, "rb");
  g_assert_nonnull(read_only);
  dt_pdf_t *pdf = calloc(1, sizeof(*pdf));
  g_assert_nonnull(pdf);
  pdf->fd = read_only;
  pdf->next_id = 3;
  pdf->n_offsets = 4;
  pdf->offsets = calloc(pdf->n_offsets, sizeof(*pdf->offsets));
  g_assert_nonnull(pdf->offsets);

  char *owned_path = g_strdup(path);
  g_assert_false(dt_pdf_finish_output(pdf, NULL, 0, &owned_path));
  g_assert_null(owned_path);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  g_free(path);
}

static void _test_pdf_finish_rejects_null_document(void)
{
  g_assert_false(dt_pdf_finish(NULL, NULL, 0));
}

static guint _calloc_calls = 0;

static void *_fail_second_calloc(const size_t count, const size_t size)
{
  _calloc_calls++;
  if(_calloc_calls == 2)
  {
    errno = ENOMEM;
    return NULL;
  }
  return calloc(count, size);
}

static void _test_pdf_start_cleans_destination_when_offsets_allocation_fails(void)
{
  char *path = _temporary_pdf_path();
  _calloc_calls = 0;
  errno = 0;

  g_assert_null(dt_pdf_start_with_allocator(
    path, 100.0f, 100.0f, 300.0f, DT_PDF_STREAM_ENCODER_FLATE,
    _fail_second_calloc));
  g_assert_cmpuint(_calloc_calls, ==, 2);
  g_assert_cmpint(errno, ==, ENOMEM);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));

  g_free(path);
}

static void *_fail_realloc(void *ptr, const size_t size)
{
  (void)ptr;
  (void)size;
  errno = ENOMEM;
  return NULL;
}

static void _test_pdf_second_image_offset_growth_failure_cleans_output(void)
{
  static const unsigned char pixel[] = { 0, 0, 0 };
  char *path = _temporary_pdf_path();
  dt_pdf_t *pdf = dt_pdf_start_with_allocators(
    path, 100.0f, 100.0f, 300.0f, DT_PDF_STREAM_ENCODER_FLATE,
    calloc, _fail_realloc);
  g_assert_nonnull(pdf);

  size_t *const original_offsets = pdf->offsets;
  GList *images = NULL;
  g_assert_true(dt_pdf_add_image_to_list(pdf, &images, pixel, 1, 1, 8, 0, 0.0f));
  g_assert_cmpuint(g_list_length(images), ==, 1);
  dt_pdf_image_t *const first_image = images->data;
  g_assert_nonnull(first_image);

  g_assert_false(dt_pdf_add_image_to_list(pdf, &images, pixel, 1, 1, 8, 0, 0.0f));
  g_assert_true(pdf->failed);
  g_assert_true(pdf->offsets == original_offsets);
  g_assert_cmpint(pdf->n_offsets, ==, 4);
  g_assert_cmpuint(g_list_length(images), ==, 1);
  g_assert_true(images->data == first_image);
  g_assert_null(images->next);

  char *owned_path = g_strdup(path);
  g_assert_false(dt_pdf_finish_output(pdf, NULL, 0, &owned_path));
  g_assert_null(owned_path);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));

  g_list_free_full(images, free);
  g_free(path);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/print/pdf/finish-success", _test_pdf_finish_reports_success);
  g_test_add_func("/print/pdf/finish-stream-failure",
                  _test_pdf_finish_reports_stream_failure);
  g_test_add_func("/print/pdf/finish-output-cleanup",
                  _test_pdf_finish_output_removes_failed_destination_and_clears_ownership);
  g_test_add_func("/print/pdf/finish-null",
                  _test_pdf_finish_rejects_null_document);
  g_test_add_func("/print/pdf/start-offset-allocation-failure",
                  _test_pdf_start_cleans_destination_when_offsets_allocation_fails);
  g_test_add_func("/print/pdf/second-image-offset-growth-failure",
                  _test_pdf_second_image_offset_growth_failure_cleans_output);
  return g_test_run();
}
