/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#pragma once

// MS-RPRN permits 539 WCHARs including separators and the terminating NUL.
// The 538 non-NUL UTF-16 code units need at most three UTF-8 bytes each.
#define DT_PRINT_MAX_PRINTER_NAME_WCHARS 539
#define DT_PRINT_MAX_PRINTER_NAME (((DT_PRINT_MAX_PRINTER_NAME_WCHARS - 1) * 3) + 1)

// Windows DC_PAPERNAMES/DC_MEDIATYPENAMES use fixed 64-WCHAR slots. A full
// slot needs at most 64 * 3 UTF-8 bytes plus NUL. IPP keyword values, including
// the stable media identifiers used by CUPS, are limited to 255 octets.
// The IPP limit is therefore the larger protocol-derived bound.
#define DT_PRINT_MAX_MEDIA_NAME 256

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
