/*
    This file is part of darktable,
    Copyright (C) 2014-2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "common/colorspaces.h"
#include "common/print_limits.h"

#include <glib.h>
#include <inttypes.h>

#define MAX_NAME DT_PRINT_MAX_MEDIA_NAME

#ifdef _WIN32
#define MAX_PRINTER_NAME DT_PRINT_MAX_PRINTER_NAME
#else
#define MAX_PRINTER_NAME MAX_NAME
#endif

typedef struct dt_images_box dt_images_box;
typedef struct _dt_job_t dt_job_t;

typedef enum dt_alignment_t
{
  ALIGNMENT_TOP_LEFT,
  ALIGNMENT_TOP,
  ALIGNMENT_TOP_RIGHT,
  ALIGNMENT_LEFT,
  ALIGNMENT_CENTER,
  ALIGNMENT_RIGHT,
  ALIGNMENT_BOTTOM_LEFT,
  ALIGNMENT_BOTTOM,
  ALIGNMENT_BOTTOM_RIGHT
} dt_alignment_t;

typedef struct dt_paper_info_t
{
  char name[MAX_NAME], common_name[MAX_NAME];
  double width, height;
} dt_paper_info_t;

typedef struct dt_medium_info_t
{
  char name[MAX_NAME], common_name[MAX_NAME];
} dt_medium_info_t;

typedef struct dt_page_setup_t
{
  gboolean landscape;
  double margin_top, margin_bottom, margin_left, margin_right;
} dt_page_setup_t;

typedef struct dt_printer_info_t
{
  char name[MAX_PRINTER_NAME];
  int resolution;
  double hw_margin_top, hw_margin_bottom, hw_margin_left, hw_margin_right;
  dt_iop_color_intent_t intent;
  gboolean is_turboprint;
} dt_printer_info_t;

typedef struct dt_print_info_t
{
  dt_printer_info_t printer;
  dt_page_setup_t page;
  dt_paper_info_t paper;
  dt_medium_info_t medium;
  int num_printers;  // the number of printers found
} dt_print_info_t;

typedef enum dt_print_color_mode_t
{
  DT_PRINT_COLOR_DARKTABLE_MANAGED,
  DT_PRINT_COLOR_DRIVER_MANAGED
} dt_print_color_mode_t;

typedef enum dt_print_result_t
{
  DT_PRINT_RESULT_SUCCESS,
  DT_PRINT_RESULT_CANCELLED,
  DT_PRINT_RESULT_FAILED
} dt_print_result_t;

typedef struct dt_print_color_context_t
{
  dt_print_color_mode_t mode;
  // Non-owning full destination ICC path; job parameters keep it alive through submission.
  const char *printer_profile;
} dt_print_color_context_t;

// Asynchronous printer discovery, cb will be called for each printer found
void dt_printers_discovery(void (*cb)(dt_printer_info_t *pr, void *user_data),
                           void *user_data);
void dt_printers_abort_discovery(void);
void dt_printers_discovery_settled(void);
gboolean dt_printers_discovery_is_settled(void);
gboolean dt_printers_discovery_shutdown_wait_required(void);

// initialize the pinfo structure
void dt_init_print_info(dt_print_info_t *pinfo);

// get printer information for the given printer name
void dt_get_printer_info(const char *printer_name,
                         dt_printer_info_t *pinfo);

// get all available papers for the given printer
GList *dt_get_papers(const dt_printer_info_t *printer);

// get paper information for the given paper name
dt_paper_info_t *dt_get_paper(GList *papers,
                              const char *name);

// get all available media type for the given printer
GList *dt_get_media_type(const dt_printer_info_t *printer);

// get paper information for the given paper name
dt_medium_info_t *dt_get_medium(GList *media,
                                const char *name);

dt_print_result_t dt_print_submit(const dt_imgid_t imgid,
                                  const char *job_title,
                                  const dt_print_info_t *pinfo,
                                  const dt_print_color_context_t *color,
                                  dt_images_box *imgs,
                                  dt_job_t *job,
                                  GError **error);

gboolean dt_print_create_pdf(const char *filename,
                             const dt_print_info_t *pinfo,
                             const dt_print_color_context_t *color,
                             const dt_images_box *imgs,
                             GError **error);

// given the page settings (media size and border) and the printer (hardware margins) returns the
// page and printable area layout in the area_width and area_height (the area that dt allocate
// for the central display).
//  - the page area (px, py, pwidth, pheight)
//  - the printable area (ax, ay, awidth and aheight), the area without the borders
// there is no unit, every returned values are based on the area size.
void dt_get_print_layout(const dt_print_info_t *prt,
                         const int32_t area_width,
                         const int32_t area_height,
                         float *px,
                         float *py,
                         float *pwidth,
                         float *pheight,
                         float *ax,
                         float *ay,
                         float *awidth,
                         float *aheight,
                         gboolean *borderless);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
