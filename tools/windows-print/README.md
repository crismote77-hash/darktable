# Windows print backend checks

These tools cover the Windows behavior that can be checked without sending a
job to a physical printer. They do not install dependencies or change printer
configuration.

## Hardening tests

From a PowerShell prompt in the source root:

```powershell
tools\windows-print\run-hardening-tests.ps1
```

The runner uses an existing MSYS2 UCRT64 installation, compiles into a unique
`tools/windows-print/artifacts/` run directory, and removes its generated
executables afterward. It tests:

- darktable-to-Win32 rendering-intent mappings;
- invalid intent, band geometry, allocation, and ICC-header bounds;
- worst-case UTF-8 storage for the MS-RPRN 539-WCHAR printer-name bound;
- paper-name and dimension sanitization;
- independent per-image ICC ownership and generic cleanup;
- structural CUPS adapter registration, cancellation, color-context, destination,
  and temporary-PDF ownership checks.

Use `-Msys2Root` if MSYS2 is installed somewhere other than `C:\msys64`.

## Microsoft Print to PDF smoke test

`DARKTABLE_WIN_PRINT_OUTPUT_FILE` is a test-only deterministic output override.
The backend accepts it only when the selected queue reports the exact
`Microsoft Print To PDF` driver. A relative path or any other driver fails the
job with an explicit error; it never falls back to silently redirecting a
physical-printer job.

Close any existing darktable process, then run:

```powershell
tools\windows-print\smoke-test.ps1 `
  -DarktableExe C:\path\to\darktable.exe `
  -Image C:\path\to\test-image.tif `
  -OutputDirectory C:\path\to\smoke-output
```

The helper verifies the queue's actual driver before launching darktable. It
guides two interactive A4 prints: one with no printer profile (driver-managed)
and one with a selected printer profile (darktable-managed). It then requires
two nonempty PDF files with `%PDF-` headers and different SHA-256 hashes.

This two-mode Microsoft PDF run is also the regression gate for optional
`DEVMODE` color-management capabilities. That behavior depends on the real
driver's default `dmFields` and its `DocumentPropertiesW` normalization, which
the isolated hardening tests cannot reproduce without exposing the backend's
private printer-submission path. Microsoft documents that `SetICMMode` must be
called after every `StartPage` because `StartPage` turns WCS off for a printer
DC: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-seticmmode

The helper does not automate UI choices or press Print. This keeps printer use
human-controlled and makes a wrong queue fail closed in the backend.

## Cancellation smoke checks

Run these manually without setting `DARKTABLE_WIN_PRINT_OUTPUT_FILE` for a
physical printer:

1. Cancel the darktable background job before submission. Confirm no spooler
   job appears and the image receives neither a `darktable|printed|...` tag nor
   a print timestamp.
2. With Microsoft Print to PDF selected, cancel while a large image is being
   rendered. Confirm the document is aborted, no success metadata is written,
   and no completed PDF is reported.
3. Repeat a successful job afterward to confirm cancellation state does not
   leak into the next submission.

## Windows acceptance status (2026-08-05)

The current Windows candidate was built from this source tree with MSYS2
UCRT64, Ninja, and the following CMake settings:

- `CMAKE_BUILD_TYPE=Release`
- `BUILD_PRINT=ON`
- `BUILD_TESTING=ON`
- `BINARY_PACKAGE_BUILD=ON`

The canonical build tree is `C:\dtb`. The isolated deployment is
`C:\Users\crism\AppData\Local\Programs\darktable-print-dev`; do not use the
separate installation under `C:\Program Files\darktable` to qualify this
backend.

The accepted automated evidence for this snapshot is:

- `tools\windows-print\run-hardening-tests.ps1`: PASS;
- complete CTest run in `C:\dtb`: 6/6 PASS;
- CPack/NSIS generation: PASS;
- package:
  `C:\dtb\darktable-5.6.0+dirty-win64-NSIS-deprecated.exe`;
- package size: 148,890,422 bytes;
- package SHA-256:
  `ecc255bb04d42c9cd93e720aa25b93ed7d03a827012bbe1ca36d03cde3f7d739`.

The explicit-A4 Microsoft Print to PDF acceptance artifact is:

- `C:\Users\crism\Documents\darktable-print-smoke\explicit-a4-final-20260804-205139.pdf`;
- one parseable PDF 1.7 page, A4 landscape, 1,120,910 bytes;
- SHA-256:
  `0ee1a082cc7fb1b6caa80f5d62985b394fce811af04a53800d429504bc762f96`.

### Driver-managed ICM normalization

For driver-managed color, the backend now preserves the driver's advertised
`dmICMMethod` through `DocumentPropertiesW` instead of replacing it with a
different policy. Darktable-managed color still requests `DMICMMETHOD_NONE`.
The post-merge validation remains fail-closed and rejects changes to the
preserved method or the requested intent. Unit coverage verifies driver-mode
preservation, darktable-mode `NONE`, absent fields, truncated `DEVMODE`
structures, and post-merge mismatch rejection.

### Epson ET-2810 physical gate: NO-GO

One authorized physical attempt targeted exactly
`EPSON2F2C65 (ET-2810 Series)` after a preflight that reported `Normal`, WMI
status 3, error 0, online, and an empty queue. The UI was visually confirmed as
A4, landscape, `Plain paper`, driver-managed color, one selected image, and
borderless disabled. Darktable rendered the image and logged a maximum render
size of 3727 x 2494 at 360 dpi, but it did not log the backend success message
`printing ... on ...`.

A 180-second spooler monitor observed no job, and the postflight remained:

- queue jobs: 0;
- `TotalJobsPrinted`: 0;
- printer status: `Normal`;
- WMI status: 3;
- detected error: 0;
- offline: false.

Therefore the physical result is **not accepted**. There is no evidence that a
job reached the spooler or that paper or ink was consumed. Physical scale,
margins, orientation, feed source, borderless behavior, crop, and color remain
unverified. Do not retry automatically.

A non-submitting HDC probe (no `StartDoc`) reported the Epson default
`dmICMMethod=1`, `dmICMIntent=2`, and successful `SetICMMode(ICM_ON)` with
Windows error 0. The remaining failure is therefore not classified as a
`SetICMMode(ICM_ON)` rejection. Before another physical authorization,
instrument the fail-closed submission stages (`SetAbortProc`, `StartDocW`,
`StartPage`, bitmap preparation/`StretchDIBits`, `EndPage`, and `EndDoc`) so the
exact failed API and Windows error are preserved in the debug log.

### Safe handoff

At closeout, darktable is stopped and the persistent print configuration is:

- printer: `Microsoft Print to PDF`;
- paper: `win_default`;
- medium: `default`.

Any future physical run requires a fresh, explicit authorization naming the
queue and allowed paper/ink consumption. Start a queue monitor before the
single click, inspect the queue immediately afterward, and never infer physical
quality from a UI click or virtual PDF.
