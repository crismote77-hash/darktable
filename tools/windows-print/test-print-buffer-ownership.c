/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/printing.h"

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef DT_PRINT_OWNERSHIP_STANDALONE
darktable_t darktable = { 0 };

void dt_print_ext(const char *msg, ...)
{
  (void)msg;
}

gboolean dt_image_get_final_size(const dt_imgid_t imgid, int *width, int *height)
{
  (void)imgid;
  (void)width;
  (void)height;
  return FALSE;
}
#endif

static int _released_profiles = 0;

static void _profile_released(gpointer data)
{
  _released_profiles++;
  g_free(data);
}

static GBytes *_profile_blob(const guint8 marker)
{
  guint8 *data = g_malloc(1);
  *data = marker;
  return g_bytes_new_with_free_func(data, 1, _profile_released, data);
}

static void _test_clear_box_is_null_safe(void)
{
  dt_printing_clear_box(NULL);
}

static void _test_init_boxes_sanitizes_fresh_storage(void)
{
  dt_images_box imgs;
  memset(&imgs, 0xa5, sizeof(imgs));

  dt_printing_init_boxes(&imgs);

  g_assert_cmpint(imgs.count, ==, 0);
  g_assert_cmpint(imgs.motion_over, ==, -1);
  g_assert_cmpint(imgs.imgid_to_load, ==, NO_IMGID);
  g_assert_cmpfloat(imgs.page_width, ==, 0.0f);
  g_assert_cmpfloat(imgs.page_height, ==, 0.0f);
  g_assert_cmpfloat(imgs.page_width_mm, ==, 0.0f);
  g_assert_cmpfloat(imgs.page_height_mm, ==, 0.0f);
  g_assert_cmpint(imgs.box[0].imgid, ==, NO_IMGID);
  g_assert_cmpint(imgs.box[0].alignment, ==, ALIGNMENT_CENTER);
  g_assert_null(imgs.box[0].buf);
  g_assert_null(imgs.box[0].source_icc_blob);
  g_assert_cmpint(imgs.box[MAX_IMAGE_PER_PAGE - 1].imgid, ==, NO_IMGID);
  g_assert_cmpint(imgs.box[MAX_IMAGE_PER_PAGE - 1].alignment, ==, ALIGNMENT_CENTER);
  g_assert_null(imgs.box[MAX_IMAGE_PER_PAGE - 1].buf);
  g_assert_null(imgs.box[MAX_IMAGE_PER_PAGE - 1].source_icc_blob);

  dt_printing_clear_boxes(&imgs);
}

static void _test_clear_box_releases_owned_resources(void)
{
  _released_profiles = 0;
  dt_image_box box = { 0 };
  box.imgid = 42;
  box.buf = malloc(3);
  box.source_icc_blob = _profile_blob(1);

  dt_printing_clear_box(&box);

  g_assert_cmpint(box.imgid, ==, NO_IMGID);
  g_assert_null(box.buf);
  g_assert_null(box.source_icc_blob);
  g_assert_cmpint(_released_profiles, ==, 1);
}

static void _test_clear_boxes_releases_every_slot(void)
{
  _released_profiles = 0;
  dt_images_box imgs = { 0 };
  imgs.count = 1;
  imgs.box[0].buf = malloc(3);
  imgs.box[0].source_icc_blob = _profile_blob(1);
  imgs.box[MAX_IMAGE_PER_PAGE - 1].buf = malloc(3);
  imgs.box[MAX_IMAGE_PER_PAGE - 1].source_icc_blob = _profile_blob(2);

  dt_printing_clear_boxes(&imgs);

  g_assert_cmpint(imgs.count, ==, 0);
  g_assert_null(imgs.box[0].buf);
  g_assert_null(imgs.box[0].source_icc_blob);
  g_assert_null(imgs.box[MAX_IMAGE_PER_PAGE - 1].buf);
  g_assert_null(imgs.box[MAX_IMAGE_PER_PAGE - 1].source_icc_blob);
  g_assert_cmpint(_released_profiles, ==, 2);
}

static void _test_free_image_buffers_releases_active_slots(void)
{
  _released_profiles = 0;
  dt_images_box imgs = { 0 };
  imgs.count = 2;

  imgs.box[0].buf = malloc(3);
  imgs.box[0].source_icc_blob = _profile_blob(1);
  imgs.box[1].buf = malloc(3);
  imgs.box[1].source_icc_blob = _profile_blob(2);

  dt_printing_free_image_buffers(&imgs);

  g_assert_null(imgs.box[0].buf);
  g_assert_null(imgs.box[0].source_icc_blob);
  g_assert_null(imgs.box[1].buf);
  g_assert_null(imgs.box[1].source_icc_blob);
  g_assert_cmpint(_released_profiles, ==, 2);
}

static void _test_remove_box_transfers_shifted_ownership(void)
{
  _released_profiles = 0;
  dt_images_box imgs = { 0 };
  imgs.count = 3;

  for(int k = 0; k < imgs.count; k++)
  {
    imgs.box[k].imgid = k + 1;
    imgs.box[k].buf = malloc(3);
    imgs.box[k].source_icc_blob = _profile_blob(k + 1);
  }

  imgs.box[3].imgid = 99;
  imgs.box[3].buf = malloc(3);
  imgs.box[3].source_icc_blob = _profile_blob(9);

  uint16_t *const shifted_buf = imgs.box[2].buf;
  GBytes *const shifted_profile = imgs.box[2].source_icc_blob;
  uint16_t *const inactive_buf = imgs.box[3].buf;
  GBytes *const inactive_profile = imgs.box[3].source_icc_blob;

  g_assert_true(dt_printing_remove_box(&imgs, 1));

  g_assert_cmpint(imgs.count, ==, 2);
  g_assert_cmpint(_released_profiles, ==, 1);
  g_assert_cmpint(imgs.box[1].imgid, ==, 3);
  g_assert_true(imgs.box[1].buf == shifted_buf);
  g_assert_true(imgs.box[1].source_icc_blob == shifted_profile);
  g_assert_cmpint(imgs.box[2].imgid, ==, NO_IMGID);
  g_assert_null(imgs.box[2].buf);
  g_assert_null(imgs.box[2].source_icc_blob);
  g_assert_cmpint(imgs.box[3].imgid, ==, 99);
  g_assert_true(imgs.box[3].buf == inactive_buf);
  g_assert_true(imgs.box[3].source_icc_blob == inactive_profile);
  g_assert_cmpint(imgs.box[MAX_IMAGE_PER_PAGE - 1].imgid, ==, NO_IMGID);
  g_assert_null(imgs.box[MAX_IMAGE_PER_PAGE - 1].buf);
  g_assert_null(imgs.box[MAX_IMAGE_PER_PAGE - 1].source_icc_blob);

  dt_printing_clear_boxes(&imgs);
  g_assert_cmpint(_released_profiles, ==, 4);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/print/ownership/clear-box-null-safe", _test_clear_box_is_null_safe);
  g_test_add_func("/print/ownership/init-boxes", _test_init_boxes_sanitizes_fresh_storage);
  g_test_add_func("/print/ownership/clear-box", _test_clear_box_releases_owned_resources);
  g_test_add_func("/print/ownership/clear-boxes", _test_clear_boxes_releases_every_slot);
  g_test_add_func("/print/ownership/free-image-buffers",
                  _test_free_image_buffers_releases_active_slots);
  g_test_add_func("/print/ownership/remove-box-transfer",
                  _test_remove_box_transfers_shifted_ownership);
  return g_test_run();
}
