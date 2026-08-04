/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wingdi.h>

#include "common/win_print_utils.h"

#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

extern gboolean dt_win_print_store_printer_name(char *destination,
                                                 gsize destination_size,
                                                 const char *printer_name);
extern gboolean dt_win_print_page_size_mm(int physical_width,
                                          int physical_height,
                                          int dpi_x,
                                          int dpi_y,
                                          double *width_mm,
                                          double *height_mm);

static void _test_intent_mapping(void)
{
  guint32 bitmap_intent = 0;
  guint32 devmode_intent = 0;

  g_assert_true(dt_win_print_map_intent(INTENT_PERCEPTUAL, &bitmap_intent, &devmode_intent));
  g_assert_cmpuint(bitmap_intent, ==, LCS_GM_IMAGES);
  g_assert_cmpuint(devmode_intent, ==, DMICM_CONTRAST);

  g_assert_true(dt_win_print_map_intent(INTENT_RELATIVE_COLORIMETRIC,
                                        &bitmap_intent, &devmode_intent));
  g_assert_cmpuint(bitmap_intent, ==, LCS_GM_GRAPHICS);
  g_assert_cmpuint(devmode_intent, ==, DMICM_COLORIMETRIC);

  g_assert_true(dt_win_print_map_intent(INTENT_SATURATION, &bitmap_intent, &devmode_intent));
  g_assert_cmpuint(bitmap_intent, ==, LCS_GM_BUSINESS);
  g_assert_cmpuint(devmode_intent, ==, DMICM_SATURATE);

  g_assert_true(dt_win_print_map_intent(INTENT_ABSOLUTE_COLORIMETRIC,
                                        &bitmap_intent, &devmode_intent));
  g_assert_cmpuint(bitmap_intent, ==, LCS_GM_ABS_COLORIMETRIC);
  g_assert_cmpuint(devmode_intent, ==, DMICM_ABS_COLORIMETRIC);

  g_assert_false(dt_win_print_map_intent(-1, &bitmap_intent, &devmode_intent));
  g_assert_false(dt_win_print_map_intent(4, &bitmap_intent, &devmode_intent));
  g_assert_false(dt_win_print_map_intent(INTENT_PERCEPTUAL, NULL, &devmode_intent));
  g_assert_false(dt_win_print_map_intent(INTENT_PERCEPTUAL, &bitmap_intent, NULL));
}

static void _test_band_layout(void)
{
  gsize source_row_bytes = 0;
  gsize band_bytes = 0;
  int band_rows = 0;

  g_assert_true(dt_win_print_get_band_layout(100, 200, &source_row_bytes,
                                             &band_rows, &band_bytes));
  g_assert_cmpuint(source_row_bytes, ==, 300);
  g_assert_cmpint(band_rows, ==, 200);
  g_assert_cmpuint(band_bytes, ==, 80000);

  g_assert_false(dt_win_print_get_band_layout(0, 200, &source_row_bytes,
                                              &band_rows, &band_bytes));
  g_assert_false(dt_win_print_get_band_layout(100, 0, &source_row_bytes,
                                              &band_rows, &band_bytes));
  g_assert_false(dt_win_print_get_band_layout(INT_MAX, 1, &source_row_bytes,
                                              &band_rows, &band_bytes));
  g_assert_false(dt_win_print_get_band_layout(100, 200, NULL,
                                              &band_rows, &band_bytes));
}

static void _test_bitmap_header_size(void)
{
  gsize header_size = 0;

  g_assert_true(dt_win_print_get_bitmap_header_size(128, &header_size));
  g_assert_cmpuint(header_size, ==, sizeof(BITMAPV5HEADER) + 128);
  g_assert_false(dt_win_print_get_bitmap_header_size(0, &header_size));
  g_assert_false(dt_win_print_get_bitmap_header_size((gsize)UINT32_MAX + 1, &header_size));
  g_assert_false(dt_win_print_get_bitmap_header_size(128, NULL));
}

static void _fill_three_byte_utf8(char *name, const int utf16_code_units)
{
  char *p = name;
  for(int k = 0; k < utf16_code_units; k++)
  {
    *p++ = (char)0xe0;
    *p++ = (char)0xa0;
    *p++ = (char)0x80;
  }
  *p = '\0';
}

static void _test_printer_name_identity_bound(void)
{
  // MS-RPRN permits 539 WCHARs including NUL. U+0800 is three UTF-8
  // bytes per non-NUL UTF-16 code unit, so 538 instances fill the bound.
  enum { max_code_units = 538, expected_capacity = max_code_units * 3 + 1 };
  char valid_name[expected_capacity];
  _fill_three_byte_utf8(valid_name, max_code_units);

  char stored_name[DT_PRINT_MAX_PRINTER_NAME] = { 0 };
  g_assert_cmpuint(sizeof(stored_name), ==, expected_capacity);
  g_assert_true(dt_win_print_store_printer_name(stored_name, sizeof(stored_name), valid_name));
  g_assert_cmpstr(stored_name, ==, valid_name);
  g_assert_true(g_utf8_validate(stored_name, -1, NULL));

  char oversized_name[expected_capacity + 3];
  _fill_three_byte_utf8(oversized_name, max_code_units + 1);
  memset(stored_name, 'x', sizeof(stored_name));
  g_assert_false(dt_win_print_store_printer_name(stored_name, sizeof(stored_name), oversized_name));
  g_assert_cmpint(stored_name[0], ==, '\0');

  char oversized_ascii[max_code_units + 2];
  memset(oversized_ascii, 'a', sizeof(oversized_ascii) - 1);
  oversized_ascii[sizeof(oversized_ascii) - 1] = '\0';
  g_assert_false(dt_win_print_store_printer_name(stored_name, sizeof(stored_name), oversized_ascii));
  g_assert_cmpint(stored_name[0], ==, '\0');

  const char invalid_utf8[] = { (char)0xe0, (char)0x80, (char)0x80, '\0' };
  g_assert_false(dt_win_print_store_printer_name(stored_name, sizeof(stored_name), invalid_utf8));
  g_assert_cmpint(stored_name[0], ==, '\0');
  g_assert_false(dt_win_print_store_printer_name(stored_name, sizeof(stored_name) - 1, valid_name));
  g_assert_cmpint(stored_name[0], ==, '\0');
}

static void _test_full_non_bmp_windows_media_slot_is_not_truncated(void)
{
  WCHAR slot[64];
  for(int k = 0; k < 32; k++)
  {
    slot[k * 2] = (WCHAR)0xd83d;
    slot[k * 2 + 1] = (WCHAR)0xde00;
  }

  char stored[DT_PRINT_MAX_MEDIA_NAME] = { 0 };
  g_assert_true(dt_win_print_store_wide_slot_name(stored, sizeof(stored),
                                                   slot, G_N_ELEMENTS(slot)));
  g_assert_true(g_utf8_validate(stored, -1, NULL));
  g_assert_cmpuint(strlen(stored), ==, 128);
  g_assert_cmpuint(g_utf8_strlen(stored, -1), ==, 32);
}

static void _test_default_page_size(void)
{
  double width_mm = 0.0;
  double height_mm = 0.0;
  g_assert_true(dt_win_print_page_size_mm(2480, 3508, 300, 300, &width_mm, &height_mm));
  g_assert_cmpfloat(fabs(width_mm - (2480.0 / 300.0 * 25.4)), <, 0.000001);
  g_assert_cmpfloat(fabs(height_mm - (3508.0 / 300.0 * 25.4)), <, 0.000001);
  g_assert_false(dt_win_print_page_size_mm(0, 3508, 300, 300, &width_mm, &height_mm));
  g_assert_false(dt_win_print_page_size_mm(2480, 3508, 0, 300, &width_mm, &height_mm));
  g_assert_false(dt_win_print_page_size_mm(2480, 3508, 300, 300, NULL, &height_mm));
}

static void _seed_stale_paper_fields(DEVMODEW *devmode)
{
  memset(devmode, 0, sizeof(*devmode));
  devmode->dmSize = sizeof(*devmode);
  devmode->dmFields = DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERWIDTH
                     | DM_PAPERLENGTH | DM_FORMNAME;
  devmode->dmPaperSize = DMPAPER_LETTER;
  devmode->dmPaperWidth = 2160;
  devmode->dmPaperLength = 2790;
  wcscpy(devmode->dmFormName, L"stale form");
}

static void _test_stock_paper_clears_custom_fields(void)
{
  DEVMODEW devmode;
  _seed_stale_paper_fields(&devmode);

  g_assert_true(dt_win_print_set_paper_fields(&devmode, sizeof(devmode),
                                               DT_WIN_PRINT_PAPER_STOCK,
                                               DMPAPER_A4, 0, 0));

  g_assert_true(devmode.dmFields & DM_ORIENTATION);
  g_assert_true(devmode.dmFields & DM_PAPERSIZE);
  g_assert_false(devmode.dmFields & DM_PAPERWIDTH);
  g_assert_false(devmode.dmFields & DM_PAPERLENGTH);
  g_assert_false(devmode.dmFields & DM_FORMNAME);
  g_assert_cmpint(devmode.dmPaperSize, ==, DMPAPER_A4);
  g_assert_cmpint(devmode.dmPaperWidth, ==, 0);
  g_assert_cmpint(devmode.dmPaperLength, ==, 0);
  g_assert_cmpint(devmode.dmFormName[0], ==, L'\0');
}

static void _test_custom_paper_clears_stock_fields(void)
{
  DEVMODEW devmode;
  _seed_stale_paper_fields(&devmode);

  g_assert_true(dt_win_print_set_paper_fields(&devmode, sizeof(devmode),
                                               DT_WIN_PRINT_PAPER_CUSTOM,
                                               0, 1234, 5678));

  g_assert_true(devmode.dmFields & DM_ORIENTATION);
  g_assert_false(devmode.dmFields & DM_PAPERSIZE);
  g_assert_true(devmode.dmFields & DM_PAPERWIDTH);
  g_assert_true(devmode.dmFields & DM_PAPERLENGTH);
  g_assert_false(devmode.dmFields & DM_FORMNAME);
  g_assert_cmpint(devmode.dmPaperSize, ==, 0);
  g_assert_cmpint(devmode.dmPaperWidth, ==, 1234);
  g_assert_cmpint(devmode.dmPaperLength, ==, 5678);
  g_assert_cmpint(devmode.dmFormName[0], ==, L'\0');
}

static void _test_default_paper_preserves_driver_fields(void)
{
  DEVMODEW devmode;
  _seed_stale_paper_fields(&devmode);
  const DEVMODEW before = devmode;

  g_assert_true(dt_win_print_set_paper_fields(&devmode, sizeof(devmode),
                                               DT_WIN_PRINT_PAPER_DEFAULT,
                                               DMPAPER_A4, 2100, 2970));

  g_assert_cmpmem(&devmode, sizeof(devmode), &before, sizeof(before));
}

static void _test_driver_managed_icm_preserves_default_and_validation_contract(void)
{
  DEVMODEW devmode = { 0 };
  devmode.dmSize = sizeof(devmode);
  devmode.dmFields = DM_ICMMETHOD;
  devmode.dmICMMethod = DMICMMETHOD_NONE;

  const guint32 requested_method =
    dt_win_print_select_icm_method(FALSE, devmode.dmICMMethod);
  g_assert_cmpuint(requested_method, ==, DMICMMETHOD_NONE);
  g_assert_true(dt_win_print_validate_color_fields(
    &devmode, sizeof(devmode), DM_ICMMETHOD, requested_method, 0));

  devmode.dmICMMethod = DMICMMETHOD_SYSTEM;
  g_assert_false(dt_win_print_validate_color_fields(
    &devmode, sizeof(devmode), DM_ICMMETHOD, requested_method, 0));
}

static void _test_darktable_managed_icm_selects_none(void)
{
  g_assert_cmpuint(dt_win_print_select_icm_method(TRUE, DMICMMETHOD_DRIVER),
                   ==, DMICMMETHOD_NONE);
}

static void _test_color_field_validation_respects_driver_support(void)
{
  DEVMODEW devmode = { 0 };
  devmode.dmSize = sizeof(devmode);
  devmode.dmFields = DM_ICMMETHOD | DM_ICMINTENT;
  devmode.dmICMMethod = DMICMMETHOD_SYSTEM;
  devmode.dmICMIntent = DMICM_COLORIMETRIC;

  g_assert_true(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                    DM_ICMMETHOD | DM_ICMINTENT,
                                                    DMICMMETHOD_SYSTEM,
                                                    DMICM_COLORIMETRIC));

  devmode.dmFields &= ~DM_ICMMETHOD;
  g_assert_true(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                     DM_ICMMETHOD | DM_ICMINTENT,
                                                     DMICMMETHOD_SYSTEM,
                                                     DMICM_COLORIMETRIC));

  devmode.dmFields |= DM_ICMMETHOD;
  devmode.dmICMMethod = DMICMMETHOD_DRIVER;
  g_assert_false(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                     DM_ICMMETHOD | DM_ICMINTENT,
                                                     DMICMMETHOD_SYSTEM,
                                                     DMICM_COLORIMETRIC));

  devmode.dmICMMethod = DMICMMETHOD_SYSTEM;
  devmode.dmFields &= ~DM_ICMINTENT;
  g_assert_true(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                     DM_ICMMETHOD | DM_ICMINTENT,
                                                     DMICMMETHOD_SYSTEM,
                                                     DMICM_COLORIMETRIC));

  devmode.dmFields |= DM_ICMINTENT;
  devmode.dmICMIntent = DMICM_SATURATE;
  g_assert_false(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                     DM_ICMMETHOD | DM_ICMINTENT,
                                                     DMICMMETHOD_SYSTEM,
                                                     DMICM_COLORIMETRIC));

  // Darktable-managed mode intentionally does not request DM_ICMINTENT.
  devmode.dmICMIntent = DMICM_COLORIMETRIC;
  g_assert_false(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                     DM_ICMMETHOD,
                                                     DMICMMETHOD_SYSTEM,
                                                     DMICM_COLORIMETRIC));
  devmode.dmFields &= ~DM_ICMINTENT;
  g_assert_true(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                    DM_ICMMETHOD,
                                                    DMICMMETHOD_SYSTEM,
                                                    DMICM_COLORIMETRIC));

  // An unrequested method restored by the driver is equally contradictory.
  devmode.dmFields = DM_ICMMETHOD | DM_ICMINTENT;
  devmode.dmICMMethod = DMICMMETHOD_SYSTEM;
  devmode.dmICMIntent = DMICM_COLORIMETRIC;
  g_assert_false(dt_win_print_validate_color_fields(&devmode, sizeof(devmode),
                                                     DM_ICMINTENT,
                                                     DMICMMETHOD_SYSTEM,
                                                     DMICM_COLORIMETRIC));

  DEVMODEW truncated = { 0 };
  truncated.dmSize = G_STRUCT_OFFSET(DEVMODEW, dmICMMethod);
  truncated.dmFields = DM_ICMMETHOD;
  g_assert_false(dt_win_print_devmode_field_available(
    &truncated, sizeof(truncated), G_STRUCT_OFFSET(DEVMODEW, dmICMMethod),
    sizeof(truncated.dmICMMethod)));
  g_assert_false(dt_win_print_validate_color_fields(
    &truncated, sizeof(truncated), DM_ICMMETHOD, DMICMMETHOD_NONE, 0));
}

static void _test_devmode_layout_bounds(void)
{
  DEVMODEW devmode = { 0 };
  devmode.dmSize = sizeof(devmode);
  g_assert_true(dt_win_print_devmode_layout_valid(&devmode, sizeof(devmode)));

  devmode.dmDriverExtra = 8;
  g_assert_true(dt_win_print_devmode_layout_valid(&devmode, sizeof(devmode) + 8));
  g_assert_false(dt_win_print_devmode_layout_valid(&devmode, sizeof(devmode) + 7));

  devmode.dmDriverExtra = 0;
  devmode.dmSize = sizeof(devmode) + 1;
  g_assert_false(dt_win_print_devmode_layout_valid(&devmode, sizeof(devmode)));

  devmode.dmSize = G_STRUCT_OFFSET(DEVMODEW, dmDriverExtra);
  g_assert_false(dt_win_print_devmode_layout_valid(&devmode, sizeof(devmode)));
}

static void _test_devmode_field_bounds(void)
{
  DEVMODEW devmode = { 0 };
  devmode.dmSize = G_STRUCT_OFFSET(DEVMODEW, dmCopies) + sizeof(devmode.dmCopies);

  g_assert_true(dt_win_print_devmode_field_available(
    &devmode, sizeof(devmode), G_STRUCT_OFFSET(DEVMODEW, dmCopies),
    sizeof(devmode.dmCopies)));
  g_assert_false(dt_win_print_devmode_field_available(
    &devmode, sizeof(devmode), G_STRUCT_OFFSET(DEVMODEW, dmMediaType),
    sizeof(devmode.dmMediaType)));
  g_assert_false(dt_win_print_devmode_field_available(
    &devmode, G_STRUCT_OFFSET(DEVMODEW, dmCopies),
    G_STRUCT_OFFSET(DEVMODEW, dmCopies), sizeof(devmode.dmCopies)));
}

static void _test_short_devmode_rejects_late_paper_fields(void)
{
  DEVMODEW devmode = { 0 };
  devmode.dmSize = G_STRUCT_OFFSET(DEVMODEW, dmPaperWidth);

  g_assert_false(dt_win_print_set_paper_fields(&devmode, sizeof(devmode),
                                                DT_WIN_PRINT_PAPER_CUSTOM,
                                                0, 1234, 5678));
}

static void _test_paper_validation_rejects_driver_contradictions_without_mutation(void)
{
  DEVMODEW stock = { 0 };
  stock.dmSize = sizeof(stock);
  stock.dmFields = DM_PAPERSIZE;
  stock.dmPaperSize = DMPAPER_A4;
  stock.dmPaperWidth = 2100;
  stock.dmPaperLength = 2970;
  wcscpy(stock.dmFormName, L"inactive stale form");
  const DEVMODEW stock_before = stock;
  g_assert_true(dt_win_print_validate_paper_fields(
    &stock, sizeof(stock), DT_WIN_PRINT_PAPER_STOCK, DMPAPER_A4, 0, 0, NULL));
  g_assert_cmpmem(&stock, sizeof(stock), &stock_before, sizeof(stock_before));

  DEVMODEW contradictory_stock;
  _seed_stale_paper_fields(&contradictory_stock);
  contradictory_stock.dmPaperSize = DMPAPER_A4;
  const DEVMODEW contradictory_stock_before = contradictory_stock;
  g_assert_false(dt_win_print_validate_paper_fields(
    &contradictory_stock, sizeof(contradictory_stock), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 0, 0, NULL));
  g_assert_cmpmem(&contradictory_stock, sizeof(contradictory_stock),
                  &contradictory_stock_before, sizeof(contradictory_stock_before));

  DEVMODEW custom = { 0 };
  custom.dmSize = sizeof(custom);
  custom.dmFields = DM_PAPERWIDTH | DM_PAPERLENGTH;
  custom.dmPaperSize = DMPAPER_LETTER;
  custom.dmPaperWidth = 1234;
  custom.dmPaperLength = 5678;
  wcscpy(custom.dmFormName, L"inactive stale form");
  const DEVMODEW custom_before = custom;
  g_assert_true(dt_win_print_validate_paper_fields(
    &custom, sizeof(custom), DT_WIN_PRINT_PAPER_CUSTOM, 0, 1234, 5678, NULL));
  g_assert_cmpmem(&custom, sizeof(custom), &custom_before, sizeof(custom_before));

  DEVMODEW contradictory_custom;
  _seed_stale_paper_fields(&contradictory_custom);
  contradictory_custom.dmPaperWidth = 1234;
  contradictory_custom.dmPaperLength = 5678;
  const DEVMODEW contradictory_custom_before = contradictory_custom;
  g_assert_false(dt_win_print_validate_paper_fields(
    &contradictory_custom, sizeof(contradictory_custom),
    DT_WIN_PRINT_PAPER_CUSTOM, 0, 1234, 5678, NULL));
  g_assert_cmpmem(&contradictory_custom, sizeof(contradictory_custom),
                  &contradictory_custom_before, sizeof(contradictory_custom_before));

  DEVMODEW defaults;
  _seed_stale_paper_fields(&defaults);
  const DEVMODEW before = defaults;
  g_assert_true(dt_win_print_validate_paper_fields(
    &defaults, sizeof(defaults), DT_WIN_PRINT_PAPER_DEFAULT, 0, 0, 0, NULL));
  g_assert_cmpmem(&defaults, sizeof(defaults), &before, sizeof(before));

  stock.dmPaperSize = DMPAPER_LETTER;
  g_assert_false(dt_win_print_validate_paper_fields(
    &stock, sizeof(stock), DT_WIN_PRINT_PAPER_STOCK, DMPAPER_A4, 0, 0, NULL));
}

static void _test_stock_paper_accepts_only_equivalent_driver_form_normalization(void)
{
  DEVMODEW normalized = { 0 };
  normalized.dmSize = sizeof(normalized);
  normalized.dmFields = DM_PAPERSIZE | DM_FORMNAME;
  normalized.dmPaperSize = DMPAPER_A4;
  normalized.dmPaperWidth = 2100;
  normalized.dmPaperLength = 2970;
  wcscpy(normalized.dmFormName, L"A4");

  DEVMODEW changed = normalized;
  changed.dmPaperSize = DMPAPER_LETTER;
  g_assert_false(dt_win_print_validate_paper_fields(
    &changed, sizeof(changed), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));

  changed = normalized;
  changed.dmPaperWidth = 2160;
  g_assert_false(dt_win_print_validate_paper_fields(
    &changed, sizeof(changed), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));

  changed = normalized;
  wcscpy(changed.dmFormName, L"Letter");
  g_assert_false(dt_win_print_validate_paper_fields(
    &changed, sizeof(changed), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));

  changed = normalized;
  changed.dmFields |= DM_PAPERWIDTH;
  g_assert_false(dt_win_print_validate_paper_fields(
    &changed, sizeof(changed), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));

  changed = normalized;
  changed.dmSize = G_STRUCT_OFFSET(DEVMODEW, dmFormName);
  g_assert_false(dt_win_print_validate_paper_fields(
    &changed, sizeof(changed), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));

  changed = normalized;
  wmemset(changed.dmFormName, L'x', G_N_ELEMENTS(changed.dmFormName));
  g_assert_false(dt_win_print_validate_paper_fields(
    &changed, sizeof(changed), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));

  g_assert_true(dt_win_print_validate_paper_fields(
    &normalized, sizeof(normalized), DT_WIN_PRINT_PAPER_STOCK,
    DMPAPER_A4, 2100, 2970, "A4"));
}

static void _test_margin_mapping_round_trips_between_portrait_and_landscape(void)
{
  double left = 1.0;
  double top = 2.0;
  double right = 3.0;
  double bottom = 4.0;

  g_assert_true(dt_win_print_margins_to_landscape(&left, &top, &right, &bottom));
  g_assert_cmpfloat(left, ==, 2.0);
  g_assert_cmpfloat(top, ==, 3.0);
  g_assert_cmpfloat(right, ==, 4.0);
  g_assert_cmpfloat(bottom, ==, 1.0);

  g_assert_true(dt_win_print_margins_to_portrait(&left, &top, &right, &bottom));
  g_assert_cmpfloat(left, ==, 1.0);
  g_assert_cmpfloat(top, ==, 2.0);
  g_assert_cmpfloat(right, ==, 3.0);
  g_assert_cmpfloat(bottom, ==, 4.0);
  g_assert_false(dt_win_print_margins_to_portrait(NULL, &top, &right, &bottom));
}

static void _test_forced_single_copy_validation(void)
{
  DEVMODEW devmode = { 0 };
  devmode.dmSize = sizeof(devmode);
  devmode.dmFields = DM_COPIES;
  devmode.dmCopies = 1;
  g_assert_true(dt_win_print_validate_copies(&devmode, sizeof(devmode)));

  devmode.dmCopies = 2;
  g_assert_false(dt_win_print_validate_copies(&devmode, sizeof(devmode)));
  devmode.dmCopies = 1;
  devmode.dmFields &= ~DM_COPIES;
  g_assert_false(dt_win_print_validate_copies(&devmode, sizeof(devmode)));
  devmode.dmFields |= DM_COPIES;
  devmode.dmSize = G_STRUCT_OFFSET(DEVMODEW, dmCopies);
  g_assert_false(dt_win_print_validate_copies(&devmode, sizeof(devmode)));
}

static void _test_landscape_fallback_geometry_is_canonical(void)
{
  double width = 297.0;
  double height = 210.0;
  g_assert_true(dt_win_print_normalize_page_size(&width, &height));
  g_assert_cmpfloat(width, ==, 210.0);
  g_assert_cmpfloat(height, ==, 297.0);

  g_assert_false(dt_win_print_normalize_page_size(NULL, &height));
  width = 0.0;
  g_assert_false(dt_win_print_normalize_page_size(&width, &height));
}

static void _make_pdf_test_path(WCHAR *path, const gsize path_count, const WCHAR *suffix)
{
  WCHAR temp_dir[MAX_PATH] = { 0 };
  g_assert_cmpuint(GetTempPathW(G_N_ELEMENTS(temp_dir), temp_dir), >, 0);
  g_assert_cmpint(swprintf(path, path_count, L"%lsdarktable-publish-%lu-%ls.pdf",
                          temp_dir, (unsigned long)GetCurrentProcessId(), suffix), >, 0);
  DeleteFileW(path);
}

static void _write_marker_file(const WCHAR *path, const char marker)
{
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
  g_assert_true(file != INVALID_HANDLE_VALUE);
  DWORD written = 0;
  g_assert_true(WriteFile(file, &marker, 1, &written, NULL));
  g_assert_cmpuint(written, ==, 1);
  g_assert_true(CloseHandle(file));
}

typedef struct dt_output_probe_step_t
{
  dt_win_print_output_probe_result_t result;
  guint64 size;
  gulong error_code;
} dt_output_probe_step_t;

typedef struct dt_output_wait_test_t
{
  const dt_output_probe_step_t *steps;
  guint step_count;
  guint probes;
  guint sleeps;
} dt_output_wait_test_t;

static dt_win_print_output_probe_result_t _test_output_probe(
  const wchar_t *path,
  guint64 *size,
  gulong *error_code,
  gpointer user_data)
{
  dt_output_wait_test_t *test = (dt_output_wait_test_t *)user_data;
  g_assert_cmpint(wcscmp(path, L"C:\\pending-output.pdf"), ==, 0);
  g_assert_cmpuint(test->probes, <, test->step_count);
  const dt_output_probe_step_t *step = &test->steps[test->probes++];
  if(size) *size = step->size;
  if(error_code) *error_code = step->error_code;
  return step->result;
}

static void _test_output_sleep(const guint milliseconds, gpointer user_data)
{
  dt_output_wait_test_t *test = (dt_output_wait_test_t *)user_data;
  g_assert_cmpuint(milliseconds, ==, 25);
  test->sleeps++;
}

static void _assert_output_wait_sequence(
  const dt_output_probe_step_t *steps,
  const guint step_count,
  const dt_win_print_output_wait_result_t expected_result,
  const guint64 expected_size,
  const gulong expected_error)
{
  dt_output_wait_test_t test = {
    .steps = steps,
    .step_count = step_count
  };
  guint64 stable_size = 0;
  gulong error_code = ERROR_SUCCESS;

  g_assert_cmpint(dt_win_print_wait_for_output_with_ops(
                    L"C:\\pending-output.pdf", step_count, 25,
                    _test_output_probe, _test_output_sleep, &test,
                    &stable_size, &error_code),
                  ==, expected_result);
  g_assert_cmpuint(test.probes, ==, step_count);
  g_assert_cmpuint(test.sleeps, ==, step_count > 0 ? step_count - 1 : 0);
  g_assert_cmpuint(stable_size, ==, expected_size);
  g_assert_cmpuint(error_code, ==, expected_error);
}

static void _test_pdf_output_waits_for_absent_file_to_become_stably_ready(void)
{
  const dt_output_probe_step_t steps[] = {
    { DT_WIN_PRINT_OUTPUT_PROBE_ABSENT, 0, ERROR_FILE_NOT_FOUND },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 120, ERROR_SUCCESS },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 120, ERROR_SUCCESS }
  };
  _assert_output_wait_sequence(steps, G_N_ELEMENTS(steps),
                               DT_WIN_PRINT_OUTPUT_WAIT_READY, 120,
                               ERROR_SUCCESS);
}

static void _test_pdf_output_waits_through_partial_and_changing_sizes(void)
{
  const dt_output_probe_step_t steps[] = {
    { DT_WIN_PRINT_OUTPUT_PROBE_INCOMPLETE, 20, ERROR_SUCCESS },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 100, ERROR_SUCCESS },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 140, ERROR_SUCCESS },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 140, ERROR_SUCCESS }
  };
  _assert_output_wait_sequence(steps, G_N_ELEMENTS(steps),
                               DT_WIN_PRINT_OUTPUT_WAIT_READY, 140,
                               ERROR_SUCCESS);
}

static void _test_pdf_output_wait_timeout_preserves_missing_state(void)
{
  const dt_output_probe_step_t steps[] = {
    { DT_WIN_PRINT_OUTPUT_PROBE_ABSENT, 0, ERROR_FILE_NOT_FOUND },
    { DT_WIN_PRINT_OUTPUT_PROBE_ABSENT, 0, ERROR_FILE_NOT_FOUND },
    { DT_WIN_PRINT_OUTPUT_PROBE_ABSENT, 0, ERROR_FILE_NOT_FOUND }
  };
  _assert_output_wait_sequence(steps, G_N_ELEMENTS(steps),
                               DT_WIN_PRINT_OUTPUT_WAIT_TIMEOUT, 0,
                               ERROR_FILE_NOT_FOUND);
}

static void _test_pdf_output_wait_timeout_sets_error_for_changing_ready_sizes(void)
{
  const dt_output_probe_step_t steps[] = {
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 100, ERROR_SUCCESS },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 120, ERROR_SUCCESS },
    { DT_WIN_PRINT_OUTPUT_PROBE_READY, 140, ERROR_SUCCESS }
  };
  _assert_output_wait_sequence(steps, G_N_ELEMENTS(steps),
                               DT_WIN_PRINT_OUTPUT_WAIT_TIMEOUT, 140,
                               ERROR_TIMEOUT);
}

static void _test_pdf_output_wait_rejects_invalid_pdf(void)
{
  const dt_output_probe_step_t steps[] = {
    { DT_WIN_PRINT_OUTPUT_PROBE_INVALID, 64, ERROR_INVALID_DATA }
  };
  dt_output_wait_test_t test = {
    .steps = steps,
    .step_count = G_N_ELEMENTS(steps)
  };
  guint64 stable_size = 0;
  gulong error_code = ERROR_SUCCESS;
  g_assert_cmpint(dt_win_print_wait_for_output_with_ops(
                    L"C:\\pending-output.pdf", 4, 25, _test_output_probe,
                    _test_output_sleep, &test, &stable_size, &error_code),
                  ==, DT_WIN_PRINT_OUTPUT_WAIT_INVALID);
  g_assert_cmpuint(test.probes, ==, 1);
  g_assert_cmpuint(test.sleeps, ==, 0);
  g_assert_cmpuint(stable_size, ==, 64);
  g_assert_cmpuint(error_code, ==, ERROR_INVALID_DATA);
}

static void _test_pdf_output_probe_checks_header_and_terminal_eof(void)
{
  WCHAR path[MAX_PATH] = { 0 };
  _make_pdf_test_path(path, G_N_ELEMENTS(path), L"readiness");

  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
  g_assert_true(file != INVALID_HANDLE_VALUE);
  const char invalid[] = "not a PDF\n%%EOF\n";
  DWORD written = 0;
  g_assert_true(WriteFile(file, invalid, sizeof(invalid) - 1, &written, NULL));
  g_assert_true(CloseHandle(file));

  guint64 size = 0;
  gulong error_code = ERROR_SUCCESS;
  g_assert_cmpint(dt_win_print_probe_output(path, &size, &error_code, NULL),
                  ==, DT_WIN_PRINT_OUTPUT_PROBE_INVALID);
  g_assert_cmpuint(error_code, ==, ERROR_INVALID_DATA);

  file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL, NULL);
  g_assert_true(file != INVALID_HANDLE_VALUE);
  const char partial[] = "%PDF-1.7\npartial";
  g_assert_true(WriteFile(file, partial, sizeof(partial) - 1, &written, NULL));
  g_assert_true(CloseHandle(file));
  g_assert_cmpint(dt_win_print_probe_output(path, &size, &error_code, NULL),
                  ==, DT_WIN_PRINT_OUTPUT_PROBE_INCOMPLETE);

  file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL, NULL);
  g_assert_true(file != INVALID_HANDLE_VALUE);
  const char complete[] = "%PDF-1.7\nbody\n%%EOF\r\n";
  g_assert_true(WriteFile(file, complete, sizeof(complete) - 1, &written, NULL));
  g_assert_true(CloseHandle(file));
  g_assert_cmpint(dt_win_print_probe_output(path, &size, &error_code, NULL),
                  ==, DT_WIN_PRINT_OUTPUT_PROBE_READY);
  g_assert_cmpuint(size, ==, sizeof(complete) - 1);
  g_assert_cmpuint(error_code, ==, ERROR_SUCCESS);
  g_assert_true(DeleteFileW(path));
}

static void _test_atomic_pdf_publication(void)
{
  WCHAR destination[MAX_PATH] = { 0 };
  _make_pdf_test_path(destination, G_N_ELEMENTS(destination), L"success");

  WCHAR *temporary = NULL;
  WCHAR *second_temporary = NULL;
  DWORD path_error = ERROR_SUCCESS;
  g_assert_true(dt_win_print_prepare_output_path(destination, &temporary, &path_error));
  g_assert_nonnull(temporary);
  g_assert_cmpuint(path_error, ==, ERROR_SUCCESS);
  g_assert_cmpuint(GetFileAttributesW(temporary), ==, INVALID_FILE_ATTRIBUTES);
  const WCHAR *destination_separator = wcsrchr(destination, L'\\');
  g_assert_nonnull(destination_separator);
  const gsize directory_length = (gsize)(destination_separator - destination) + 1;
  g_assert_cmpint(wcsncmp(temporary, destination, directory_length), ==, 0);

  char *temporary_utf8 = g_utf16_to_utf8((const gunichar2 *)temporary, -1,
                                         NULL, NULL, NULL);
  g_assert_nonnull(temporary_utf8);
  const char *basename = strrchr(temporary_utf8, '\\');
  basename = basename ? basename + 1 : temporary_utf8;
  g_assert_true(g_regex_match_simple(
    "^\\.darktable-print-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.pdf$",
    basename, 0, 0));
  g_free(temporary_utf8);

  g_assert_true(dt_win_print_prepare_output_path(destination, &second_temporary,
                                                 &path_error));
  g_assert_cmpint(wcscmp(temporary, second_temporary), !=, 0);
  g_assert_cmpint(dt_win_print_cleanup_output_temp(second_temporary, &path_error),
                  ==, DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE);
  g_free(second_temporary);

  _write_marker_file(temporary, 'S');
  g_assert_true(dt_win_print_publish_output(temporary, destination, &path_error));
  g_assert_cmpuint(path_error, ==, ERROR_SUCCESS);
  g_assert_cmpuint(GetFileAttributesW(destination), !=, INVALID_FILE_ATTRIBUTES);
  g_assert_cmpuint(GetFileAttributesW(temporary), ==, INVALID_FILE_ATTRIBUTES);
  g_assert_cmpint(dt_win_print_cleanup_output_temp(temporary, &path_error),
                  ==, DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE);

  g_assert_true(DeleteFileW(destination));
  g_free(temporary);
}

static int _cleanup_delete_calls = 0;
static int _cleanup_schedule_calls = 0;

static gboolean _test_file_present(const wchar_t *path,
                                   gboolean *present,
                                   gulong *error_code)
{
  g_assert_nonnull(path);
  *present = TRUE;
  if(error_code) *error_code = ERROR_SUCCESS;
  return TRUE;
}

static gboolean _test_file_absent(const wchar_t *path,
                                  gboolean *present,
                                  gulong *error_code)
{
  g_assert_nonnull(path);
  *present = FALSE;
  if(error_code) *error_code = ERROR_FILE_NOT_FOUND;
  return TRUE;
}

static gboolean _test_delete_fails(const wchar_t *path, gulong *error_code)
{
  g_assert_nonnull(path);
  _cleanup_delete_calls++;
  if(error_code) *error_code = ERROR_SHARING_VIOLATION;
  return FALSE;
}

static gboolean _test_schedule_succeeds(const wchar_t *path, gulong *error_code)
{
  g_assert_nonnull(path);
  _cleanup_schedule_calls++;
  if(error_code) *error_code = ERROR_SUCCESS;
  return TRUE;
}

static gboolean _test_schedule_fails(const wchar_t *path, gulong *error_code)
{
  g_assert_nonnull(path);
  _cleanup_schedule_calls++;
  if(error_code) *error_code = ERROR_ACCESS_DENIED;
  return FALSE;
}

static void _test_temp_cleanup_failure_is_recoverable_and_scheduled(void)
{
  gulong cleanup_error = ERROR_SUCCESS;
  _cleanup_delete_calls = 0;
  _cleanup_schedule_calls = 0;
  const dt_win_print_temp_cleanup_result_t scheduled =
    dt_win_print_cleanup_committed_output_temp_with_ops(
      L"C:\\kept-for-retry.pdf", _test_file_present, _test_delete_fails,
      _test_schedule_succeeds, &cleanup_error);
  g_assert_cmpint(scheduled, ==, DT_WIN_PRINT_TEMP_CLEANUP_SCHEDULED);
  g_assert_cmpuint(cleanup_error, ==, ERROR_SHARING_VIOLATION);
  g_assert_cmpint(_cleanup_delete_calls, ==, 1);
  g_assert_cmpint(_cleanup_schedule_calls, ==, 1);

  cleanup_error = ERROR_SUCCESS;
  const dt_win_print_temp_cleanup_result_t orphaned =
    dt_win_print_cleanup_committed_output_temp_with_ops(
      L"C:\\kept-for-manual-cleanup.pdf", _test_file_present,
      _test_delete_fails, _test_schedule_fails, &cleanup_error);
  g_assert_cmpint(orphaned, ==, DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED);
  g_assert_cmpuint(cleanup_error, ==, ERROR_SHARING_VIOLATION);

  cleanup_error = ERROR_SUCCESS;
  const dt_win_print_temp_cleanup_result_t late_missing =
    dt_win_print_cleanup_committed_output_temp_with_ops(
      L"C:\\late-producer-may-still-create.pdf", _test_file_absent,
      _test_delete_fails, _test_schedule_fails, &cleanup_error);
  g_assert_cmpint(late_missing, ==, DT_WIN_PRINT_TEMP_CLEANUP_ORPHANED);
  g_assert_cmpuint(cleanup_error, ==, ERROR_FILE_NOT_FOUND);
}

static void _test_redirected_output_committed_cleanup_policy(void)
{
  g_assert_false(dt_win_print_output_requires_committed_cleanup(
    TRUE, FALSE, FALSE, FALSE));
  g_assert_false(dt_win_print_output_requires_committed_cleanup(
    TRUE, TRUE, FALSE, TRUE));
  g_assert_true(dt_win_print_output_requires_committed_cleanup(
    TRUE, TRUE, FALSE, FALSE));
  g_assert_true(dt_win_print_output_requires_committed_cleanup(
    TRUE, TRUE, TRUE, FALSE));
  g_assert_false(dt_win_print_output_requires_committed_cleanup(
    FALSE, TRUE, TRUE, FALSE));
}

static void _test_atomic_pdf_publish_rejects_destination_race_and_cleans_temp(void)
{
  WCHAR destination[MAX_PATH] = { 0 };
  _make_pdf_test_path(destination, G_N_ELEMENTS(destination), L"race");

  WCHAR *temporary = NULL;
  DWORD path_error = ERROR_SUCCESS;
  g_assert_true(dt_win_print_prepare_output_path(destination, &temporary, &path_error));
  _write_marker_file(temporary, 'T');
  _write_marker_file(destination, 'D');

  g_assert_false(dt_win_print_publish_output(temporary, destination, &path_error));
  g_assert_true(path_error == ERROR_ALREADY_EXISTS || path_error == ERROR_FILE_EXISTS);
  g_assert_cmpuint(GetFileAttributesW(temporary), !=, INVALID_FILE_ATTRIBUTES);
  g_assert_cmpint(dt_win_print_cleanup_output_temp(temporary, &path_error),
                  ==, DT_WIN_PRINT_TEMP_CLEANUP_COMPLETE);
  g_assert_cmpuint(GetFileAttributesW(temporary), ==, INVALID_FILE_ATTRIBUTES);

  g_assert_true(DeleteFileW(destination));
  g_free(temporary);
}

static void _test_existing_pdf_destination_is_rejected_before_temp_creation(void)
{
  WCHAR destination[MAX_PATH] = { 0 };
  _make_pdf_test_path(destination, G_N_ELEMENTS(destination), L"existing");
  _write_marker_file(destination, 'E');

  WCHAR *temporary = (WCHAR *)1;
  DWORD path_error = ERROR_SUCCESS;
  g_assert_false(dt_win_print_prepare_output_path(destination, &temporary, &path_error));
  g_assert_null(temporary);
  g_assert_cmpuint(path_error, ==, ERROR_FILE_EXISTS);

  g_assert_true(DeleteFileW(destination));
}

static void _test_pdf_output_preflight_is_unicode_safe_and_fail_closed(void)
{
  WCHAR temp_dir[MAX_PATH] = { 0 };
  WCHAR output_path[MAX_PATH] = { 0 };
  WCHAR unique_path[MAX_PATH] = { 0 };
  WCHAR missing_child[MAX_PATH] = { 0 };
  WCHAR missing_output[MAX_PATH] = { 0 };
  g_assert_cmpuint(GetTempPathW(G_N_ELEMENTS(temp_dir), temp_dir), >, 0);
  g_assert_cmpint(swprintf(output_path, G_N_ELEMENTS(output_path),
                          L"%lsdarktable-print-\x2603-%lu.pdf", temp_dir,
                          (unsigned long)GetCurrentProcessId()), >, 0);

  HANDLE output = CreateFileW(output_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
  g_assert_true(output != INVALID_HANDLE_VALUE);
  CloseHandle(output);

  DWORD path_error = ERROR_SUCCESS;
  g_assert_false(dt_win_print_output_path_available(output_path, &path_error));
  g_assert_cmpuint(path_error, ==, ERROR_FILE_EXISTS);
  g_assert_true(DeleteFileW(output_path));

  path_error = ERROR_INVALID_DATA;
  g_assert_true(dt_win_print_output_path_available(output_path, &path_error));
  g_assert_cmpuint(path_error, ==, ERROR_SUCCESS);

  path_error = ERROR_SUCCESS;
  g_assert_false(dt_win_print_output_path_available(L"C:\\darktable<invalid>.pdf",
                                                     &path_error));
  g_assert_cmpuint(path_error, !=, ERROR_SUCCESS);
  g_assert_cmpuint(path_error, !=, ERROR_FILE_EXISTS);

  g_assert_cmpuint(GetTempFileNameW(temp_dir, L"dt", 0, unique_path), !=, 0);
  g_assert_true(DeleteFileW(unique_path));
  g_assert_cmpint(swprintf(missing_child, G_N_ELEMENTS(missing_child),
                          L"%ls-\x96ea-missing", unique_path), >, 0);
  g_assert_cmpint(swprintf(missing_output, G_N_ELEMENTS(missing_output),
                          L"%ls\\output.pdf", missing_child), >, 0);

  path_error = ERROR_SUCCESS;
  g_assert_false(dt_win_print_output_path_available(missing_output, &path_error));
  g_assert_cmpuint(path_error, ==, ERROR_PATH_NOT_FOUND);

  DeleteFileW(missing_output);
  RemoveDirectoryW(missing_child);
}

typedef struct dt_abort_thread_test_t
{
  GMutex *mutex;
  GCond *cond;
  int *ready;
  gboolean *release;
  gpointer value;
} dt_abort_thread_test_t;

static gpointer _test_abort_state_thread(gpointer user_data)
{
  dt_abort_thread_test_t *test = (dt_abort_thread_test_t *)user_data;
  dt_win_print_set_abort_state(test->value);

  g_mutex_lock(test->mutex);
  (*test->ready)++;
  g_cond_broadcast(test->cond);
  while(!*test->release)
    g_cond_wait(test->cond, test->mutex);
  g_mutex_unlock(test->mutex);

  g_assert_true(dt_win_print_get_abort_state() == test->value);
  dt_win_print_set_abort_state(NULL);
  g_assert_null(dt_win_print_get_abort_state());
  return NULL;
}

static void _test_abort_state_is_thread_local(void)
{
  int main_value = 1;
  int thread_values[] = { 2, 3 };
  GMutex mutex;
  GCond cond;
  int ready = 0;
  gboolean release = FALSE;
  dt_abort_thread_test_t tests[2];
  GThread *threads[2];

  g_mutex_init(&mutex);
  g_cond_init(&cond);
  dt_win_print_set_abort_state(&main_value);

  for(int k = 0; k < 2; k++)
  {
    tests[k] = (dt_abort_thread_test_t){
      .mutex = &mutex,
      .cond = &cond,
      .ready = &ready,
      .release = &release,
      .value = &thread_values[k]
    };
    threads[k] = g_thread_new("abort-state-test", _test_abort_state_thread, &tests[k]);
  }

  g_mutex_lock(&mutex);
  while(ready < 2)
    g_cond_wait(&cond, &mutex);
  g_assert_true(dt_win_print_get_abort_state() == &main_value);
  release = TRUE;
  g_cond_broadcast(&cond);
  g_mutex_unlock(&mutex);

  for(int k = 0; k < 2; k++)
    g_thread_join(threads[k]);

  g_assert_true(dt_win_print_get_abort_state() == &main_value);
  dt_win_print_set_abort_state(NULL);
  g_assert_null(dt_win_print_get_abort_state());
  g_cond_clear(&cond);
  g_mutex_clear(&mutex);
}

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  _test_intent_mapping();
  _test_band_layout();
  _test_bitmap_header_size();
  _test_printer_name_identity_bound();
  _test_full_non_bmp_windows_media_slot_is_not_truncated();
  _test_stock_paper_clears_custom_fields();
  _test_custom_paper_clears_stock_fields();
  _test_default_paper_preserves_driver_fields();
  _test_driver_managed_icm_preserves_default_and_validation_contract();
  _test_darktable_managed_icm_selects_none();
  _test_color_field_validation_respects_driver_support();
  _test_pdf_output_preflight_is_unicode_safe_and_fail_closed();
  _test_default_page_size();
  _test_abort_state_is_thread_local();
  _test_devmode_layout_bounds();
  _test_devmode_field_bounds();
  _test_short_devmode_rejects_late_paper_fields();
  _test_paper_validation_rejects_driver_contradictions_without_mutation();
  _test_stock_paper_accepts_only_equivalent_driver_form_normalization();
  _test_forced_single_copy_validation();
  _test_landscape_fallback_geometry_is_canonical();
  _test_margin_mapping_round_trips_between_portrait_and_landscape();
  _test_pdf_output_waits_for_absent_file_to_become_stably_ready();
  _test_pdf_output_waits_through_partial_and_changing_sizes();
  _test_pdf_output_wait_timeout_preserves_missing_state();
  _test_pdf_output_wait_timeout_sets_error_for_changing_ready_sizes();
  _test_pdf_output_wait_rejects_invalid_pdf();
  _test_pdf_output_probe_checks_header_and_terminal_eof();
  _test_atomic_pdf_publication();
  _test_temp_cleanup_failure_is_recoverable_and_scheduled();
  _test_redirected_output_committed_cleanup_policy();
  _test_atomic_pdf_publish_rejects_destination_race_and_cleans_temp();
  _test_existing_pdf_destination_is_rejected_before_temp_creation();
  return 0;
}
