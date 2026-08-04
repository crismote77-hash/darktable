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

#include "common/print_backend.h"

#include "common/pdf.h"
#include "common/print_backend_utils.h"
#include "common/printing.h"
#include "control/control.h"

#ifdef DT_PRINT_BACKEND_CUPS
#include "common/cups_print.h"
#endif
#ifdef DT_PRINT_BACKEND_WINDOWS
#include "common/win_print.h"
#endif

typedef enum dt_print_backend_error_t
{
  DT_PRINT_BACKEND_ERROR_FAILED
} dt_print_backend_error_t;

typedef struct dt_print_backend_t
{
  const char *name;
  gboolean shutdown_wait_required;
  void (*printers_discovery)(void (*cb)(dt_printer_info_t *, void *), void *user_data);
  void (*abort_discovery)(void);
  void (*get_printer_info)(const char *printer_name, dt_printer_info_t *pinfo);
  GList *(*get_papers)(const dt_printer_info_t *printer);
  GList *(*get_media_type)(const dt_printer_info_t *printer);
  dt_print_result_t (*submit)(const dt_imgid_t imgid,
                              const char *job_title,
                              const dt_print_info_t *pinfo,
                              const dt_print_color_context_t *color,
                              dt_images_box *imgs,
                              dt_job_t *job,
                              GError **error);
} dt_print_backend_t;

static GQuark _dt_print_backend_error_quark(void)
{
  return g_quark_from_static_string("dt-print-backend-error-quark");
}

static gint _discovery_settled = TRUE;

void dt_printers_discovery_settled(void)
{
  g_atomic_int_set(&_discovery_settled, TRUE);
}

gboolean dt_printers_discovery_is_settled(void)
{
  return g_atomic_int_get(&_discovery_settled);
}

#ifdef DT_PRINT_BACKEND_CUPS
static const dt_print_backend_t _dt_print_backend =
{
  .name = "cups",
  .shutdown_wait_required = TRUE,
  .printers_discovery = dt_cups_printers_discovery,
  .abort_discovery = dt_cups_printers_abort_discovery,
  .get_printer_info = dt_cups_get_printer_info,
  .get_papers = dt_cups_get_papers,
  .get_media_type = dt_cups_get_media_type,
  .submit = dt_cups_print_submit
};
#elif defined(DT_PRINT_BACKEND_WINDOWS)
static const dt_print_backend_t _dt_print_backend =
{
  .name = "windows",
  .shutdown_wait_required = FALSE,
  .printers_discovery = dt_win_printers_discovery,
  .abort_discovery = dt_win_printers_abort_discovery,
  .get_printer_info = dt_win_get_printer_info,
  .get_papers = dt_win_get_papers,
  .get_media_type = dt_win_get_media_type,
  .submit = dt_win_print_submit
};
#endif

static const dt_print_backend_t *_backend(void)
{
#if defined(DT_PRINT_BACKEND_CUPS) || defined(DT_PRINT_BACKEND_WINDOWS)
  return &_dt_print_backend;
#else
  return NULL;
#endif
}

gboolean dt_printers_discovery_shutdown_wait_required(void)
{
  const dt_print_backend_t *backend = _backend();
  return backend && backend->shutdown_wait_required;
}

void dt_printers_discovery(void (*cb)(dt_printer_info_t *pr, void *user_data),
                           void *user_data)
{
  const dt_print_backend_t *backend = _backend();
  if(backend && backend->printers_discovery)
  {
    g_atomic_int_set(&_discovery_settled, FALSE);
    backend->printers_discovery(cb, user_data);
  }
}

void dt_printers_abort_discovery(void)
{
  const dt_print_backend_t *backend = _backend();
  if(backend && backend->abort_discovery)
    backend->abort_discovery();
  else
    dt_printers_discovery_settled();
}

void dt_init_print_info(dt_print_info_t *pinfo)
{
  memset(&pinfo->printer, 0, sizeof(dt_printer_info_t));
  memset(&pinfo->page, 0, sizeof(dt_page_setup_t));
  memset(&pinfo->paper, 0, sizeof(dt_paper_info_t));
  pinfo->printer.intent = DT_INTENT_PERCEPTUAL;
  pinfo->printer.is_turboprint = FALSE;
  pinfo->num_printers = 0;
}

void dt_get_printer_info(const char *printer_name,
                         dt_printer_info_t *pinfo)
{
  const dt_print_backend_t *backend = _backend();
  if(backend && backend->get_printer_info)
    backend->get_printer_info(printer_name, pinfo);
}

GList *dt_get_papers(const dt_printer_info_t *printer)
{
  const dt_print_backend_t *backend = _backend();
  return backend && backend->get_papers ? backend->get_papers(printer) : NULL;
}

dt_paper_info_t *dt_get_paper(GList *papers,
                              const char *name)
{
  return (dt_paper_info_t *)dt_print_paper_at(
    papers, dt_print_paper_index(papers, name));
}

GList *dt_get_media_type(const dt_printer_info_t *printer)
{
  const dt_print_backend_t *backend = _backend();
  return backend && backend->get_media_type ? backend->get_media_type(printer) : NULL;
}

dt_medium_info_t *dt_get_medium(GList *media,
                                const char *name)
{
  return (dt_medium_info_t *)dt_print_medium_at(
    media, dt_print_medium_index(media, name));
}

gboolean dt_print_create_pdf(const char *filename,
                             const dt_print_info_t *pinfo,
                             const dt_print_color_context_t *color,
                             const dt_images_box *imgs,
                             GError **error)
{
  const float page_width = pinfo->page.landscape
    ? dt_pdf_mm_to_point(pinfo->paper.height)
    : dt_pdf_mm_to_point(pinfo->paper.width);
  const float page_height = pinfo->page.landscape
    ? dt_pdf_mm_to_point(pinfo->paper.width)
    : dt_pdf_mm_to_point(pinfo->paper.height);
  int destination_icc_id = 0;

#ifdef __APPLE__
  const gboolean is_macos = TRUE;
#else
  const gboolean is_macos = FALSE;
#endif
  const dt_print_pdf_icc_kind_t icc_kind =
    dt_print_pdf_icc_kind(color ? color->mode : DT_PRINT_COLOR_DRIVER_MANAGED,
                          is_macos);

  dt_pdf_t *pdf = dt_pdf_start(filename, page_width, page_height,
                               pinfo->printer.resolution,
                               DT_PDF_STREAM_ENCODER_FLATE);
  if(!pdf)
  {
    g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                _("failed to create temporary PDF for printing"));
    return FALSE;
  }

  // Application-managed macOS pixels are already in the destination space.
  // Linux deliberately keeps DeviceRGB and relies on cm-calibration instead.
  if(icc_kind == DT_PRINT_PDF_ICC_DESTINATION)
  {
    if(!color || !color->printer_profile || !*color->printer_profile)
    {
      g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                  _("application-managed macOS printing requires a destination ICC profile"));
      if(!dt_pdf_finish(pdf, NULL, 0))
        dt_print(DT_DEBUG_PRINT, "[print] failed to finalize temporary PDF during ICC cleanup");
      return FALSE;
    }

    destination_icc_id = dt_pdf_add_icc(pdf, color->printer_profile);
    if(destination_icc_id <= 0)
    {
      g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                  _("failed to embed destination ICC profile `%s' in temporary PDF"),
                  color->printer_profile);
      if(!dt_pdf_finish(pdf, NULL, 0))
        dt_print(DT_DEBUG_PRINT, "[print] failed to finalize temporary PDF during ICC cleanup");
      return FALSE;
    }
  }

  dt_pdf_image_t *pdf_image[MAX_IMAGE_PER_PAGE] = { 0 };
  int32_t count = 0;

  for(int k = 0; k < imgs->count; k++)
  {
    const int resolution = pinfo->printer.resolution;
    const dt_image_box *box = &imgs->box[k];

    if(dt_is_valid_imgid(box->imgid))
    {
      int image_icc_id = destination_icc_id;
      if(icc_kind == DT_PRINT_PDF_ICC_IMAGE_SOURCE)
      {
        gsize profile_size = 0;
        const guint8 *profile = box->source_icc_blob
          ? g_bytes_get_data(box->source_icc_blob, &profile_size) : NULL;
        image_icc_id = profile && profile_size > 0
          ? dt_pdf_add_icc_from_data(pdf, profile, profile_size) : 0;
        if(image_icc_id <= 0)
        {
          g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                      _("failed to embed source ICC profile for image %d in temporary PDF"),
                      k + 1);
          if(!dt_pdf_finish(pdf, NULL, 0))
            dt_print(DT_DEBUG_PRINT,
                     "[print] failed to finalize temporary PDF during source ICC cleanup");
          for(int i = 0; i < count; i++)
            free(pdf_image[i]);
          return FALSE;
        }
      }

      pdf_image[count] =
        dt_pdf_add_image(pdf, (uint8_t *)box->buf, box->exp_width, box->exp_height,
                         8, image_icc_id, 0.0);

      if(!pdf_image[count])
      {
        g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                    _("failed to add image %d to temporary PDF"), k + 1);
        if(!dt_pdf_finish(pdf, NULL, 0))
          dt_print(DT_DEBUG_PRINT, "[print] failed to finalize temporary PDF during image cleanup");
        for(int i = 0; i < count; i++)
          free(pdf_image[i]);
        return FALSE;
      }

      //  PDF bounding-box has origin on bottom-left
      pdf_image[count]->bb_x      = dt_pdf_pixel_to_point(box->print.x, resolution);
      pdf_image[count]->bb_y      = dt_pdf_pixel_to_point(box->print.y, resolution);
      pdf_image[count]->bb_width  = dt_pdf_pixel_to_point(box->print.width, resolution);
      pdf_image[count]->bb_height = dt_pdf_pixel_to_point(box->print.height, resolution);
      count++;
    }
  }

  dt_pdf_page_t *pdf_page = dt_pdf_add_page(pdf, pdf_image, count);
  if(!pdf_page)
  {
    g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                _("failed to add page to temporary PDF"));
    if(!dt_pdf_finish(pdf, NULL, 0))
      dt_print(DT_DEBUG_PRINT, "[print] failed to finalize temporary PDF during page cleanup");
    for(int k = 0; k < count; k++)
      free(pdf_image[k]);
    return FALSE;
  }

  const gboolean finish_ok = dt_pdf_finish(pdf, &pdf_page, 1);

  for(int k = 0; k < count; k++)
    free(pdf_image[k]);
  free(pdf_page);

  if(!finish_ok)
  {
    g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
                _("failed to finalize temporary PDF for printing"));
    return FALSE;
  }

  return TRUE;
}

dt_print_result_t dt_print_submit(const dt_imgid_t imgid,
                                  const char *job_title,
                                  const dt_print_info_t *pinfo,
                                  const dt_print_color_context_t *color,
                                  dt_images_box *imgs,
                                  dt_job_t *job,
                                  GError **error)
{
  const dt_print_backend_t *backend = _backend();
  if(backend && backend->submit)
    return backend->submit(imgid, job_title, pinfo, color, imgs, job, error);

  g_set_error(error, _dt_print_backend_error_quark(), DT_PRINT_BACKEND_ERROR_FAILED,
              _("no print backend is available"));
  return DT_PRINT_RESULT_FAILED;
}

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
                         gboolean *borderless)
{
  /* this is where the layout is done for the display and for the
     print too. So this routine is one of the most critical for the
     print circuitry. */

  // page w/h
  float pg_width  = prt->paper.width;
  float pg_height = prt->paper.height;

  /* here, width and height correspond to the area for the picture */

  // non-printable
  float np_top = prt->printer.hw_margin_top;
  float np_left = prt->printer.hw_margin_left;
  float np_right = prt->printer.hw_margin_right;
  float np_bottom = prt->printer.hw_margin_bottom;

  /* do some arrangements for the landscape mode. */

  if(prt->page.landscape)
  {
    float tmp = pg_width;
    pg_width = pg_height;
    pg_height = tmp;

    // rotate the non-printable margins
    tmp       = np_top;
    np_top    = np_right;
    np_right  = np_bottom;
    np_bottom = np_left;
    np_left   = tmp;
  }

  // the image area aspect
  const float a_aspect = (float)area_width / (float)area_height;

  // page aspect
  const float pg_aspect = pg_width / pg_height;

  // display page
  float p_bottom, p_right;

  if(a_aspect > pg_aspect)
  {
    *px = (area_width - (area_height * pg_aspect)) / 2.0f;
    *py = 0;
    p_bottom = area_height;
    p_right = area_width - *px;
  }
  else
  {
    *px = 0;
    *py = (area_height - (area_width / pg_aspect)) / 2.0f;
    p_right = area_width;
    p_bottom = area_height - *py;
  }

  *pwidth = p_right - *px;
  *pheight = p_bottom - *py;

  // page margins, note that we do not want to change those values for
  // the landscape mode.  these margins are those set by the user from
  // the GUI, and the top margin is *always* at the top of the screen.

  const float border_top = prt->page.margin_top;
  const float border_left = prt->page.margin_left;
  const float border_right = prt->page.margin_right;
  const float border_bottom = prt->page.margin_bottom;

  // display picture area, that is removing the non printable areas
  // and user's margins

  const float bx = *px + (border_left / pg_width) * (*pwidth);
  const float by = *py + (border_top / pg_height) * (*pheight);
  const float bb = p_bottom - (border_bottom / pg_height) * (*pheight);
  const float br = p_right - (border_right / pg_width) * (*pwidth);

  *borderless = border_left   < np_left
             || border_right  < np_right
             || border_top    < np_top
             || border_bottom < np_bottom;

  // now we have the printable area (ax, ay) -> (ax + awidth, ay + aheight)

  *ax      = bx;
  *ay      = by;
  *awidth  = br - bx;
  *aheight = bb - by;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
