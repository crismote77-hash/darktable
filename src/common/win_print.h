/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

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

#include "common/print_backend.h"

void dt_win_printers_discovery(void (*cb)(dt_printer_info_t *pr, void *user_data),
                               void *user_data);
void dt_win_printers_abort_discovery(void);
void dt_win_get_printer_info(const char *printer_name,
                             dt_printer_info_t *pinfo);
GList *dt_win_get_papers(const dt_printer_info_t *printer);
GList *dt_win_get_media_type(const dt_printer_info_t *printer);
dt_print_result_t dt_win_print_submit(const dt_imgid_t imgid,
                                      const char *job_title,
                                      const dt_print_info_t *pinfo,
                                      const dt_print_color_context_t *color,
                                      dt_images_box *imgs,
                                      dt_job_t *job,
                                      GError **error);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
