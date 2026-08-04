/*
    This file is part of darktable,
    Copyright (C) 2014-2025 darktable developers.

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

#include <cups/cups.h>
#include <cups/ppd.h>
#include <glib.h>
#include <stdio.h>
#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

#include "common/file_location.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/mipmap_cache.h"
#include "common/pdf.h"
#include "common/print_backend_utils.h"
#include "control/jobs/control_jobs.h"
#include "cups_print.h"
#include "cups_print_utils.h"

// enable weak linking in libcups on macOS
#if defined(__APPLE__) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_8 && ((CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR >= 6) || CUPS_VERSION_MAJOR > 1)
extern int cupsEnumDests() __attribute__((weak_import));
#endif
#if defined(__APPLE__) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_9 && ((CUPS_VERSION_MAJOR == 1 && CUPS_VERSION_MINOR >= 7) || CUPS_VERSION_MAJOR > 1)
extern http_t *cupsConnectDest() __attribute__((weak_import));
extern cups_dinfo_t *cupsCopyDestInfo() __attribute__((weak_import));
extern int cupsGetDestMediaCount() __attribute__((weak_import));
extern int cupsGetDestMediaByIndex() __attribute__((weak_import));
extern void cupsFreeDestInfo() __attribute__((weak_import));
#endif

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// some platforms are starting to provide CUPS 2.2.9 and there the
// CUPS API deprecated routines ate now flagged as such and reported as
// warning preventing the compilation.
//
// this seems wrong and PPD should be removed from this unit. but there
// still one missing piece discussed with the CUPS maintainers about the
// way to get media-type using the IPP API. nothing close to working at
// this stage, so instead of breaking the compilation on platforms using
// recent CUPS version we kill the warning.

typedef struct dt_prtctl_t
{
  void (*cb)(dt_printer_info_t *, void *);
  void *user_data;
} dt_prtctl_t;

void dt_cups_get_printer_info(const char *printer_name,
                              dt_printer_info_t *pinfo)
{
  cups_dest_t *dests;
  const int num_dests = cupsGetDests(&dests);
  cups_dest_t *dest = cupsGetDest(printer_name, NULL, num_dests, dests);

  if(dest)
  {
    const char *PPDFile = cupsGetPPD (printer_name);
    g_strlcpy(pinfo->name, dest->name, MAX_NAME);
    ppd_file_t *ppd = ppdOpenFile(PPDFile);

    if(ppd)
    {
      ppdMarkDefaults(ppd);
      cupsMarkOptions(ppd, dest->num_options, dest->options);

      // first check if this is turboprint drived printer, two solutions:
      // 1. ModelName contains TurboPrint
      // 2. zedoPrinterDriver exists
      ppd_attr_t *attr = ppdFindAttr(ppd, "ModelName", NULL);

      if(attr)
      {
        pinfo->is_turboprint = strstr(attr->value, "TurboPrint") != NULL;
      }

      // hardware margins

      attr = ppdFindAttr(ppd, "HWMargins", NULL);

      if(attr)
      {
        // scanf use local number format and PPD has en numbers
        dt_util_str_to_loc_numbers_format(attr->value);

        sscanf(attr->value, "%lf %lf %lf %lf",
               &pinfo->hw_margin_left, &pinfo->hw_margin_bottom,
               &pinfo->hw_margin_right, &pinfo->hw_margin_top);

        pinfo->hw_margin_left   = dt_pdf_point_to_mm (pinfo->hw_margin_left);
        pinfo->hw_margin_bottom = dt_pdf_point_to_mm (pinfo->hw_margin_bottom);
        pinfo->hw_margin_right  = dt_pdf_point_to_mm (pinfo->hw_margin_right);
        pinfo->hw_margin_top    = dt_pdf_point_to_mm (pinfo->hw_margin_top);
      }

      // default resolution

      attr = ppdFindAttr(ppd, "DefaultResolution", NULL);

      if(attr)
      {
        char *x = strstr(attr->value, "x");

        if(x)
          sscanf (x+1, "%ddpi", &pinfo->resolution);
        else
          sscanf (attr->value, "%ddpi", &pinfo->resolution);
      }
      else
        pinfo->resolution = 300;

      while(pinfo->resolution>360)
        pinfo->resolution /= 2.0;

      ppdClose(ppd);
      g_unlink(PPDFile);
    }
  }

  cupsFreeDests(num_dests, dests);
}

static int _dest_cb(void *user_data,
                    const unsigned flags,
                    cups_dest_t *dest)
{
  const dt_prtctl_t *pctl = (dt_prtctl_t *)user_data;
  const char *psvalue = cupsGetOption("printer-state", dest->num_options, dest->options);

  // check that the printer is ready
  if(psvalue!=NULL && strtol(psvalue, NULL, 10) < IPP_PRINTER_STOPPED)
  {
    dt_printer_info_t pr;
    memset(&pr, 0, sizeof(pr));
    dt_cups_get_printer_info(dest->name, &pr);
    if(pctl->cb) pctl->cb(&pr, pctl->user_data);
    dt_print(DT_DEBUG_PRINT, "[print] new printer %s found", dest->name);
  }
  else
    dt_print(DT_DEBUG_PRINT, "[print] skip printer %s as stopped", dest->name);

  return 1;
}

static int _cancel = 0;

static int _detect_printers_callback(dt_job_t *job)
{
  dt_prtctl_t *pctl = dt_control_job_get_params(job);
  int res;
#if((CUPS_VERSION_MAJOR == 1) && (CUPS_VERSION_MINOR >= 6)) || CUPS_VERSION_MAJOR > 1
#if defined(__APPLE__) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_8
  if(&cupsEnumDests != NULL)
#endif
    res = cupsEnumDests(CUPS_MEDIA_FLAGS_DEFAULT, 30000, &_cancel, 0, 0, _dest_cb, pctl);
#if defined(__APPLE__) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_8
  else
#endif
#endif
#if defined(__APPLE__) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_8 || !(((CUPS_VERSION_MAJOR == 1) && (CUPS_VERSION_MINOR >= 6)) || CUPS_VERSION_MAJOR > 1)
  {
    cups_dest_t *dests;
    const int num_dests = cupsGetDests(&dests);
    for(int k=0; k<num_dests; k++)
    {
      _dest_cb((void *)pctl, 0, &dests[k]);
    }
    cupsFreeDests(num_dests, dests);
    res=1;
  }
#endif
  dt_printers_discovery_settled();
  return !res;
}

void dt_cups_printers_abort_discovery(void)
{
  _cancel = 1;
}

void dt_cups_printers_discovery(void (*cb)(dt_printer_info_t *pr, void *user_data),
                                void *user_data)
{
  // asynchronously checks for available printers
  dt_job_t *job = dt_control_job_create(&_detect_printers_callback, "detect connected printers");
  _cancel = 0;
  if(job)
  {
    dt_prtctl_t *prtctl = g_malloc0(sizeof(dt_prtctl_t));

    prtctl->cb = cb;
    prtctl->user_data = user_data;

    dt_control_job_set_params(job, prtctl, g_free);
    dt_control_add_job(DT_JOB_QUEUE_SYSTEM_BG, job);
  }
  else
    dt_printers_discovery_settled();
}

static gboolean paper_exists(GList *papers,
                             const char *name)
{
  if(strstr(name,"custom_") == name)
    return TRUE;

  for(GList *p = papers; p; p = g_list_next(p))
  {
    const dt_paper_info_t *pi = (dt_paper_info_t*)p->data;
    if(!strcmp(pi->name,name) || !strcmp(pi->common_name,name))
      return TRUE;
  }
  return FALSE;
}

static gint
sort_papers (gconstpointer p1, gconstpointer p2)
{
  const dt_paper_info_t *n1 = (dt_paper_info_t *)p1;
  const dt_paper_info_t *n2 = (dt_paper_info_t *)p2;
  const int l1 = strlen(n1->common_name);
  const int l2 = strlen(n2->common_name);
  return l1==l2 ? strcmp(n1->common_name, n2->common_name) : (l1 < l2 ? -1 : +1);
}

GList *dt_cups_get_papers(const dt_printer_info_t *printer)
{
  const char *printer_name = printer->name;
  GList *result = NULL;

#if((CUPS_VERSION_MAJOR == 1) && (CUPS_VERSION_MINOR >= 7)) || CUPS_VERSION_MAJOR > 1
#if defined(__APPLE__) && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_9
  if(&cupsConnectDest != NULL && &cupsCopyDestInfo != NULL && &cupsGetDestMediaCount != NULL &&
      &cupsGetDestMediaByIndex != NULL && &cupsFreeDestInfo != NULL)
#endif
  {
    cups_dest_t *dests;
    const int num_dests = cupsGetDests(&dests);
    cups_dest_t *dest = cupsGetDest(printer_name, NULL, num_dests, dests);

    int cancel = 0; // important

    char resource[1024];

    if(dest)
    {
      http_t *hcon = cupsConnectDest(dest, 0, 2000, &cancel,
                                     resource, sizeof(resource), NULL, (void *)NULL);

      if(hcon)
      {
        cups_size_t size;
        cups_dinfo_t *info = cupsCopyDestInfo (hcon, dest);
        const int count = cupsGetDestMediaCount(hcon, dest, info, CUPS_MEDIA_FLAGS_DEFAULT);
        for(int k=0; k<count; k++)
        {
          if(cupsGetDestMediaByIndex(hcon, dest, info, k, CUPS_MEDIA_FLAGS_DEFAULT, &size))
          {
            if(size.width!=0 && size.length!=0 && !paper_exists(result, size.media))
            {
              pwg_media_t *med = pwgMediaForPWG (size.media);
              const char *common_name = med && med->ppd ? med->ppd : size.media;
              dt_paper_info_t *paper = calloc(1, sizeof(dt_paper_info_t));
              if(!dt_print_store_name(paper->name, sizeof(paper->name), size.media)
                 || !dt_print_store_name(paper->common_name,
                                         sizeof(paper->common_name), common_name))
              {
                dt_print(DT_DEBUG_PRINT,
                         "[print] rejected CUPS paper name outside the protocol bound");
                free(paper);
                continue;
              }
              paper->width = (double)size.width / 100.0;
              paper->height = (double)size.length / 100.0;
              result = g_list_append (result, paper);

              dt_print(DT_DEBUG_PRINT,
                       "[print] new media paper %4d %6.2f x %6.2f (%s) (%s)",
                       k, paper->width, paper->height, paper->name, paper->common_name);
            }
          }
        }

        cupsFreeDestInfo(info);
        httpClose(hcon);
      }
      else
        dt_print(DT_DEBUG_PRINT,
                 "[print] cannot connect to printer %s (cancel=%d)",
                 printer_name, cancel);
    }

    cupsFreeDests(num_dests, dests);
  }
#endif

  // check now PPD page sizes

  const char *PPDFile = cupsGetPPD(printer_name);
  ppd_file_t *ppd = ppdOpenFile(PPDFile);

  if(ppd)
  {
    ppd_size_t *size = ppd->sizes;

    for(int k=0; k<ppd->num_sizes; k++)
    {
      if(size->width!=0 && size->length!=0 && !paper_exists(result, size->name))
      {
        dt_paper_info_t *paper = calloc(1, sizeof(dt_paper_info_t));
        if(!dt_print_store_name(paper->name, sizeof(paper->name), size->name)
           || !dt_print_store_name(paper->common_name,
                                   sizeof(paper->common_name), size->name))
        {
          dt_print(DT_DEBUG_PRINT,
                   "[print] rejected PPD paper name outside the protocol bound");
          free(paper);
          size++;
          continue;
        }
        paper->width = (double)dt_pdf_point_to_mm(size->width);
        paper->height = (double)dt_pdf_point_to_mm(size->length);
        result = g_list_append (result, paper);

        dt_print(DT_DEBUG_PRINT,
                 "[print] new ppd paper %4d %6.2f x %6.2f (%s) (%s)",
                 k, paper->width, paper->height, paper->name, paper->common_name);
      }
      size++;
    }

    ppdClose(ppd);
    g_unlink(PPDFile);
  }

  result = g_list_sort_with_data (result, (GCompareDataFunc)sort_papers, NULL);
  return result;
}

GList *dt_cups_get_media_type(const dt_printer_info_t *printer)
{
  const char *printer_name = printer->name;
  GList *result = NULL;

  // check now PPD media type

  const char *PPDFile = cupsGetPPD(printer_name);
  ppd_file_t *ppd = ppdOpenFile(PPDFile);

  if(ppd)
  {
      ppd_option_t *opt = ppdFindOption(ppd, "MediaType");

      if(opt)
      {
        ppd_choice_t *choice = opt->choices;

        for(int k=0; k<opt->num_choices; k++)
        {
          dt_medium_info_t *media = calloc(1, sizeof(dt_medium_info_t));
          if(!dt_print_store_name(media->name, sizeof(media->name), choice->choice)
             || !dt_print_store_name(media->common_name,
                                     sizeof(media->common_name), choice->text))
          {
            dt_print(DT_DEBUG_PRINT,
                     "[print] rejected PPD media name outside the protocol bound");
            free(media);
            choice++;
            continue;
          }
          result = g_list_prepend (result, media);

          dt_print(DT_DEBUG_PRINT,
                   "[print] new media %2d (%s) (%s)",
                   k, media->name, media->common_name);
          choice++;
        }
      }
  }

  ppdClose(ppd);
  g_unlink(PPDFile);

  return g_list_reverse(result);  // list was built in reverse order, so un-reverse it
}

static gboolean _print_job_cancelled(dt_job_t *job)
{
  return job && dt_control_job_get_state(job) == DT_JOB_STATE_CANCELLED;
}

static void _set_cancelled_error(GError **error, const char *printer_name)
{
  g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"),
              DT_CUPS_PRINT_ERROR_CANCELLED,
              _("printing on `%s' cancelled"), printer_name ? printer_name : "");
}

dt_print_result_t dt_cups_print_submit(const dt_imgid_t imgid,
                                       const char *job_title,
                                       const dt_print_info_t *pinfo,
                                       const dt_print_color_context_t *color,
                                       dt_images_box *imgs,
                                       dt_job_t *job,
                                       GError **error)
{
  if(_print_job_cancelled(job))
  {
    _set_cancelled_error(error, pinfo->printer.name);
    return DT_PRINT_RESULT_CANCELLED;
  }

  char pdf_filename[PATH_MAX] = { 0 };
  dt_loc_get_tmp_dir(pdf_filename, sizeof(pdf_filename));
  g_strlcat(pdf_filename, "/pf.XXXXXX", sizeof(pdf_filename));

  const gint fd = g_mkstemp(pdf_filename);
  if(fd == -1)
  {
    g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                _("failed to create temporary PDF for printing"));
    return DT_PRINT_RESULT_FAILED;
  }
  if(close(fd) != 0)
  {
    g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                _("failed to close temporary PDF for printing"));
    return dt_cups_cleanup_temporary_file(pdf_filename, DT_PRINT_RESULT_FAILED,
                                          g_unlink, error);
  }

  dt_print_result_t result = dt_print_create_pdf(pdf_filename, pinfo, color, imgs, error)
                               ? DT_PRINT_RESULT_SUCCESS : DT_PRINT_RESULT_FAILED;
  if(result == DT_PRINT_RESULT_SUCCESS && _print_job_cancelled(job))
  {
    _set_cancelled_error(error, pinfo->printer.name);
    result = DT_PRINT_RESULT_CANCELLED;
  }
  if(result == DT_PRINT_RESULT_SUCCESS)
    result = dt_cups_print_file(imgid, pdf_filename, job_title, pinfo, color, job, error);

  return dt_cups_cleanup_temporary_file(pdf_filename, result, g_unlink, error);
}

dt_print_result_t dt_cups_print_file(const dt_imgid_t imgid,
                                     const char *filename,
                                     const char *job_title,
                                     const dt_print_info_t *pinfo,
                                     const dt_print_color_context_t *color,
                                     dt_job_t *job,
                                     GError **error)
{
  // first for safety check that filename exists and is readable

  if(!g_file_test(filename, G_FILE_TEST_IS_REGULAR))
  {
    dt_control_log(_("file `%s' to print not found for image %d on `%s'"),
                   filename, imgid, pinfo->printer.name);
    g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                _("file `%s' to print not found for image %d on `%s'"),
                filename, imgid, pinfo->printer.name);
    return DT_PRINT_RESULT_FAILED;
  }

  cups_option_t *options = NULL;
  int num_options = 0;

  // for turboprint drived printer, use the turboprint dialog
  if(pinfo->printer.is_turboprint)
  {
    const char *tp_intent_name[] = { "perception_0",
                                     "colorimetric-relative_1",
                                     "saturation_1",
                                     "colorimetric-absolute_1" };
    char tmpfile[PATH_MAX] = { 0 };

    dt_loc_get_tmp_dir(tmpfile, sizeof(tmpfile));
    g_strlcat(tmpfile, "/dt_cups_opts_XXXXXX", sizeof(tmpfile));

    gint fd = g_mkstemp(tmpfile);
    if(fd == -1)
    {
      dt_control_log(_("failed to create temporary file for printing options"));
      dt_print(DT_DEBUG_ALWAYS, "failed to create temporary PDF for printing options");
      g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                  _("failed to create temporary file for printing options"));
      return DT_PRINT_RESULT_FAILED;
    }
    if(close(fd) != 0)
    {
      g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                  _("failed to close temporary file for printing options"));
      if(g_unlink(tmpfile) != 0)
        dt_print(DT_DEBUG_PRINT,
                 "[print] failed to remove temporary TurboPrint options file after close failure");
      return DT_PRINT_RESULT_FAILED;
    }

    // ensure that intent is in the range, may happen if at some point
    // we add new intent in the list
    const int intent = (pinfo->printer.intent < 4) ? pinfo->printer.intent : 0;

    // spawn turboprint command
    gchar * argv[15] = { 0 };

    argv[0] = "turboprint";
    argv[1] = g_strdup_printf("--printer=%s", pinfo->printer.name);
    argv[2] = "--options";
    argv[3] = g_strdup_printf("--output=%s", tmpfile);
    argv[4] = "-o";
    argv[5] = "copies=1";
    argv[6] = "-o";
    argv[7] = g_strdup_printf("PageSize=%s", pinfo->paper.common_name);
    argv[8] = "-o";
    argv[9] = "InputSlot=AutoSelect";
    argv[10] = "-o";
    argv[11] = g_strdup_printf("zedoIntent=%s", tp_intent_name[intent]);
    argv[12] = "-o";
    argv[13] = g_strdup_printf("MediaType=%s", pinfo->medium.name);
    argv[14] = NULL;

    GError *turboprint_error = NULL;
    const gboolean turboprint_ok =
      dt_cups_run_turboprint_options(tmpfile, argv, &num_options,
                                     &options, &turboprint_error);

    g_free(argv[1]);
    g_free(argv[3]);
    g_free(argv[7]);
    g_free(argv[11]);
    g_free(argv[13]);

    if(!turboprint_ok)
    {
      const char *detail = turboprint_error
        ? turboprint_error->message : _("unknown TurboPrint error");
      dt_control_log(_("TurboPrint failed for printer `%s': %s"),
                     pinfo->printer.name, detail);
      dt_print(DT_DEBUG_PRINT, "[print]   TurboPrint failed: %s", detail);
      g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                  _("TurboPrint failed for printer `%s': %s"),
                  pinfo->printer.name, detail);
      g_clear_error(&turboprint_error);
      return DT_PRINT_RESULT_FAILED;
    }
    g_clear_error(&turboprint_error);
  }
  else
  {
    cups_dest_t *dests;
    const int num_dests = cupsGetDests(&dests);
    cups_dest_t *dest = cupsGetDest(pinfo->printer.name, NULL, num_dests, dests);

    if(!dest)
    {
      cupsFreeDests(num_dests, dests);
      dt_control_log(_("printer `%s' is no longer available"), pinfo->printer.name);
      g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"), 1,
                  _("printer `%s' is no longer available"), pinfo->printer.name);
      return DT_PRINT_RESULT_FAILED;
    }

    for(int j = 0; j < dest->num_options; j ++)
      if(cupsGetOption(dest->options[j].name, num_options,
                        options) == NULL)
        num_options = cupsAddOption(dest->options[j].name,
                                    dest->options[j].value,
                                    num_options, &options);

    cupsFreeDests(num_dests, dests);

    // If darktable already converted to the printer profile, disable CUPS color management.

    const gboolean application_managed = color
      && color->mode == DT_PRINT_COLOR_DARKTABLE_MANAGED
      && color->printer_profile && *color->printer_profile;

#ifdef __APPLE__
    const gboolean set_apple_color_matching = TRUE;
#else
    const gboolean set_apple_color_matching = FALSE;
#endif
    num_options = dt_cups_set_color_options(application_managed,
                                            set_apple_color_matching,
                                            num_options, &options);

    // media to print on

    num_options = cupsAddOption("media", pinfo->paper.name, num_options, &options);

    // the media type to print on

    num_options = cupsAddOption("MediaType", pinfo->medium.name, num_options, &options);

    // never print two-side

    num_options = cupsAddOption("sides", "one-sided", num_options, &options);

    // and a single image per page

    num_options = cupsAddOption("number-up", "1", num_options, &options);

    // if the printer has no hardware margins activate the borderless mode

    if(pinfo->printer.hw_margin_top == 0 || pinfo->printer.hw_margin_bottom == 0
        || pinfo->printer.hw_margin_left == 0 || pinfo->printer.hw_margin_right == 0)
    {
      // there is many variant for this parameter
      num_options = cupsAddOption("StpFullBleed", "true", num_options, &options);
      num_options = cupsAddOption("STP_FullBleed", "true", num_options, &options);
      num_options = cupsAddOption("Borderless", "true", num_options, &options);
    }

    // as cups-filter pdftopdf will autorotate the page, there is no
    // need to set an option in the case of landscape mode
    // images. Let's keep this as a conf option as some cups on macOS
    // seems to require it.
    if(dt_conf_get_bool("plugins/print/cups/force_landscape"))
       num_options = cupsAddOption("landscape",
                                   pinfo->page.landscape ? "true" : "false",
                                   num_options, &options);
  }

  // print lp options

  dt_print(DT_DEBUG_PRINT, "[print] printer options (%d)", num_options);
  for(int k=0; k<num_options; k++)
    dt_print(DT_DEBUG_PRINT, "[print]   %2d  %s=%s", k+1, options[k].name, options[k].value);

  if(_print_job_cancelled(job))
  {
    dt_control_log(_("printing on `%s' cancelled"), pinfo->printer.name);
    _set_cancelled_error(error, pinfo->printer.name);
    cupsFreeOptions(num_options, options);
    return DT_PRINT_RESULT_CANCELLED;
  }

  const int job_id = cupsPrintFile(pinfo->printer.name, filename, job_title, num_options, options);

  if(job_id == 0)
  {
    dt_control_log(_("error while printing `%s' on `%s'"), job_title, pinfo->printer.name);
    g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"),
                DT_CUPS_PRINT_ERROR_FAILED,
                _("error while printing `%s' on `%s'"), job_title, pinfo->printer.name);
    cupsFreeOptions(num_options, options);
    return DT_PRINT_RESULT_FAILED;
  }

  const gboolean cancelled_after_submit = _print_job_cancelled(job);
  ipp_status_t cancel_status = IPP_STATUS_OK;
  const dt_cups_cancel_result_t cancel_result =
    dt_cups_cancel_submitted_job(pinfo->printer.name, job_id,
                                 cancelled_after_submit, cupsCancelJob2,
                                 &cancel_status);

  if(cancel_result != DT_CUPS_CANCEL_NOT_REQUESTED)
  {
    if(cancel_result == DT_CUPS_CANCEL_SUCCEEDED)
    {
      dt_control_log(_("printing on `%s' cancelled; CUPS job %d cancelled"),
                     pinfo->printer.name, job_id);
      g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"),
                  DT_CUPS_PRINT_ERROR_CANCELLED,
                  _("printing on `%s' cancelled after submission; CUPS job %d cancelled"),
                  pinfo->printer.name, job_id);
    }
    else
    {
      dt_control_log(_("printing on `%s' cancelled, but CUPS job %d cancellation failed"),
                     pinfo->printer.name, job_id);
      g_set_error(error, g_quark_from_static_string("dt-print-cups-error-quark"),
                  DT_CUPS_PRINT_ERROR_CANCEL_FAILED,
                  _("printing on `%s' cancelled, but CUPS job %d cancellation failed: %s"),
                  pinfo->printer.name, job_id, ippErrorString(cancel_status));
    }
    cupsFreeOptions(num_options, options);
    return cancel_result == DT_CUPS_CANCEL_SUCCEEDED
             ? DT_PRINT_RESULT_CANCELLED : DT_PRINT_RESULT_FAILED;
  }

  dt_control_log(_("printing `%s' on `%s'"), job_title, pinfo->printer.name);

  cupsFreeOptions (num_options, options);

  return DT_PRINT_RESULT_SUCCESS;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
