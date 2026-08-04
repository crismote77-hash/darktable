# AGENTS.md

## Scope

These instructions apply to this darktable source tree. For Windows printing,
read `tools/windows-print/README.md` before changing or exercising the backend.
That file contains the current measured acceptance status and handoff.

## Canonical Windows workspace

- Source: this repository.
- Build tree: `C:\dtb`.
- Toolchain: MSYS2 UCRT64 + Ninja.
- Required configuration: Release, `BUILD_PRINT=ON`, `BUILD_TESTING=ON`, and
  `BINARY_PACKAGE_BUILD=ON`.
- Isolated candidate deployment:
  `C:\Users\crism\AppData\Local\Programs\darktable-print-dev`.
- Do not qualify local backend work with
  `C:\Program Files\darktable`; that is the separate official installation.
- Keep the source mapped to a short Windows path when packaging if long paths
  make CPack/NSIS fail.

## Editing discipline

- Inspect definitions and all call sites before editing.
- Use one writer for implementation work.
- Preserve fail-closed validation at platform boundaries.
- Fix the class of defect and add a regression test before implementation.
- Do not weaken paper, `DEVMODE`, color, cancellation, output-path, ownership,
  or printer-identity checks merely to make one driver pass.
- Keep GTK work on the main thread; discovery workers may publish state but may
  not mutate widgets.
- Do not add generated executables, installers, PDFs, previews, runtime logs, or
  temporary printer probes to Git.

## Required gates

From the source root, run:

```powershell
tools\windows-print\run-hardening-tests.ps1
```

Then rebuild and run CTest from the canonical build tree:

```powershell
C:\msys64\ucrt64\bin\cmake.exe --build C:\dtb --parallel
cd C:\dtb
C:\msys64\ucrt64\bin\ctest.exe --output-on-failure
```

For a package candidate, also run:

```powershell
C:\msys64\ucrt64\bin\cmake.exe --build C:\dtb --target package
```

Record the package path, byte size, and SHA-256. Verify the deployed executable,
`libdarktable.dll`, `lib/darktable/views/libprint.dll`, and
`lib/darktable/plugins/lighttable/libprint_settings.dll` against the build before
any smoke test.

## Print safety

- Default to `Microsoft Print to PDF` and verify its actual driver before using
  `DARKTABLE_WIN_PRINT_OUTPUT_FILE`.
- A physical print requires fresh, explicit authorization naming the exact
  queue, paper/media, color use, and allowed sheet/ink consumption.
- Before a physical click, verify the queue, printer status, offline/error
  state, and every visible print setting. Start queue monitoring first.
- Submit exactly one authorized job. Never automatically retry an ambiguous or
  failed job.
- A synthetic UI event is not evidence. Require visual confirmation, backend
  logs, spooler evidence, and physical inspection as applicable.
- Keep measured facts separate from unverified physical properties. A virtual
  PDF cannot validate feed source, physical margins, scale, crop, borderless
  behavior, or printer color.
- After testing, restore `Microsoft Print to PDF`, leave physical queues empty,
  close darktable, and stop monitors/helpers.

## Git and handoff

- Recheck branch, HEAD, status, diff, and remotes immediately before staging or
  publishing. This checkout may be detached.
- Do not push directly to the upstream `darktable-org/darktable` remote unless
  upstream write access and destination were explicitly established. Prefer a
  dedicated branch on an authorized fork.
- Commit only reviewed source, tests, and documentation.
- Update `tools/windows-print/README.md` with exact PASS/FAIL/NO-GO evidence and
  the next safe step before ending a session.
