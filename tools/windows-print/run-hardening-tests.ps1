[CmdletBinding()]
param(
  [string]$Msys2Root = 'C:\msys64',
  [string]$ArtifactsRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$ucrtBin = Join-Path $Msys2Root 'ucrt64\bin'
$usrBin = Join-Path $Msys2Root 'usr\bin'
$gcc = Join-Path $ucrtBin 'gcc.exe'
$pkgconf = Join-Path $ucrtBin 'pkgconf.exe'

foreach($tool in @($gcc, $pkgconf))
{
  if(!(Test-Path -LiteralPath $tool -PathType Leaf))
  {
    throw "Required existing MSYS2 UCRT64 tool not found: $tool"
  }
}

$oldPath = $env:PATH
$oldLocation = Get-Location
if([string]::IsNullOrWhiteSpace($ArtifactsRoot))
{
  $ArtifactsRoot = Join-Path (Split-Path -Parent $repoRoot) `
    'runtime-smoke-5.6\fourth-pass-build-hardening'
}
$artifactsRoot = [IO.Path]::GetFullPath($ArtifactsRoot)
$repoPrefix = $repoRoot.TrimEnd('\') + '\'
if($artifactsRoot -eq $repoRoot `
   -or $artifactsRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase))
{
  throw "Hardening artifacts must be outside the repository: $artifactsRoot"
}
$testDir = Join-Path $artifactsRoot ("run-{0}" -f [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $testDir | Out-Null

function Invoke-Checked
{
  param([string]$FilePath, [string[]]$Arguments)
  & $FilePath @Arguments
  if($LASTEXITCODE -ne 0)
  {
    throw "$FilePath failed with exit code $LASTEXITCODE"
  }
}

try
{
  $env:PATH = "$ucrtBin;$usrBin;$oldPath"
  Set-Location $repoRoot

  $utilsPackages = @('glib-2.0', 'lcms2')
  $utilsCflags = (& $pkgconf --cflags @utilsPackages) -split ' '
  $utilsLibs = (& $pkgconf --libs @utilsPackages) -split ' '
  $utilsExe = Join-Path $testDir 'test-win-print-utils.exe'
  Invoke-Checked $gcc (@(
      '-std=c11', '-Wall', '-Wextra', '-Werror', '-Isrc'
    ) + $utilsCflags + @(
      'tools/windows-print/test-win-print-utils.c',
      'src/common/win_print_utils.c'
    ) + $utilsLibs + @('-o', $utilsExe))
  Invoke-Checked $utilsExe @()
  Write-Host 'PASS: Windows DEVMODE bounds, paper modes, atomic PDF publication, color, AbortProc, size, overflow, and printer-name helpers'

  $ownershipPackages = @(
    'glib-2.0', 'lcms2', 'gtk+-3.0', 'librsvg-2.0', 'json-glib-1.0'
  )
  $ownershipCflags = (& $pkgconf --cflags @ownershipPackages) -split ' '
  $ownershipLibs = (& $pkgconf --libs @ownershipPackages) -split ' '
  $ownershipExe = Join-Path $testDir 'test-print-buffer-ownership.exe'
  Invoke-Checked $gcc (@(
      '-std=c11', '-ffunction-sections', '-fdata-sections',
      '-DDT_PRINT_OWNERSHIP_STANDALONE', '-Isrc'
    ) + $ownershipCflags + @(
      'tools/windows-print/test-print-buffer-ownership.c',
      'src/common/printing.c', '-Wl,--gc-sections'
    ) + $ownershipLibs + @('-o', $ownershipExe))
  Invoke-Checked $ownershipExe @()
  Write-Host 'PASS: per-image buffer and ICC ownership cleanup'

  $backendSource = Get-Content -Raw -LiteralPath 'src/common/print_backend.c'
  $backendHeader = Get-Content -Raw -LiteralPath 'src/common/print_backend.h'
  $cupsHeader = Get-Content -Raw -LiteralPath 'src/common/cups_print.h'
  $cupsSource = Get-Content -Raw -LiteralPath 'src/common/cups_print.c'
  $cupsUtilsPath = 'src/common/cups_print_utils.c'
  $cupsUtilsSource = if(Test-Path -LiteralPath $cupsUtilsPath)
  {
    Get-Content -Raw -LiteralPath $cupsUtilsPath
  }
  else
  {
    ''
  }
  $submitStart = $backendSource.IndexOf('dt_print_result_t dt_print_submit(')
  $submitEnd = $backendSource.IndexOf('void dt_get_print_layout(', $submitStart)
  if($submitStart -lt 0 -or $submitEnd -lt 0)
  {
    throw 'Could not locate the generic print submit function'
  }
  $genericSubmit = $backendSource.Substring($submitStart, $submitEnd - $submitStart)
  if($genericSubmit.Contains('DT_PRINT_BACKEND_CUPS') -or $genericSubmit.Contains('dt_cups_'))
  {
    throw 'Generic print submission still contains a CUPS-specific path'
  }
  if($backendSource -notmatch '\.submit\s*=\s*dt_cups_print_submit')
  {
    throw 'The CUPS backend does not register its submit adapter'
  }
  if($backendSource -match '_dt_cups_print_submit' `
     -or $cupsHeader -match '_dt_cups_print_submit' `
     -or $cupsSource -match '_dt_cups_print_submit')
  {
    throw 'The private CUPS submit adapter is still declared or implemented'
  }
  if($cupsHeader -notmatch 'dt_print_result_t\s+dt_cups_print_submit\(')
  {
    throw 'The CUPS submit adapter is not exported in cups_print.h'
  }

  $adapterStart = $cupsSource.IndexOf('dt_print_result_t dt_cups_print_submit(')
  if($adapterStart -lt 0)
  {
    throw 'Could not locate the CUPS submit adapter'
  }
  $adapterEnd = $cupsSource.IndexOf('dt_print_result_t dt_cups_print_file(', $adapterStart)
  if($adapterEnd -lt 0)
  {
    throw 'Could not isolate the CUPS submit adapter'
  }
  $adapter = $cupsSource.Substring($adapterStart, $adapterEnd - $adapterStart)
  $firstCancel = $adapter.IndexOf('_print_job_cancelled(job)')
  $createPdf = $adapter.IndexOf('dt_print_create_pdf(')
  $secondCancel = $adapter.IndexOf('_print_job_cancelled(job)', $firstCancel + 1)
  $printFile = $adapter.IndexOf('dt_cups_print_file(')
  if($firstCancel -lt 0 -or $createPdf -lt 0 -or $secondCancel -lt 0 -or $printFile -lt 0 `
     -or $firstCancel -gt $createPdf -or $secondCancel -lt $createPdf -or $secondCancel -gt $printFile)
  {
    throw 'The CUPS adapter lacks ordered pre-PDF and post-PDF cancellation gates'
  }
  if($adapter -notmatch 'dt_print_create_pdf\([^;]*color[^;]*\)' `
     -or $adapter -notmatch 'dt_cups_print_file\([^;]*color[^;]*job[^;]*\)')
  {
    throw 'The CUPS adapter does not pass the full color and job contexts'
  }
  $fileSubmit = $cupsSource.Substring($adapterEnd)
  if($cupsHeader -notmatch 'dt_cups_print_file\([^;]*color[^;]*job[^;]*\)')
  {
    throw 'The exported CUPS file submission signature lacks color or job context'
  }
  $fileCancel = $fileSubmit.IndexOf('_print_job_cancelled(job)')
  $cupsPrintFile = $fileSubmit.IndexOf('cupsPrintFile(')
  if($fileCancel -lt 0 -or $cupsPrintFile -lt 0 -or $fileCancel -gt $cupsPrintFile)
  {
    throw 'The CUPS file path lacks a cancellation gate immediately before cupsPrintFile'
  }
  $postSubmitCancel = $fileSubmit.IndexOf('_print_job_cancelled(job)', $cupsPrintFile)
  $zeroJobGuard = $fileSubmit.IndexOf('if(job_id == 0)', $cupsPrintFile)
  $cancelSubmittedJob = $fileSubmit.IndexOf('dt_cups_cancel_submitted_job(', $cupsPrintFile)
  $cupsCancelJob = $fileSubmit.IndexOf('cupsCancelJob2', $cupsPrintFile)
  if($zeroJobGuard -lt 0 -or $postSubmitCancel -lt 0 -or $cancelSubmittedJob -lt 0 `
     -or $cupsCancelJob -lt 0 -or $zeroJobGuard -gt $postSubmitCancel `
     -or $postSubmitCancel -gt $cancelSubmittedJob -or $cancelSubmittedJob -gt $cupsCancelJob)
  {
    throw 'The CUPS file path lacks a nonzero-job guard before ordered post-submit cancellation'
  }
  if($fileSubmit -notmatch '(?s)const gboolean application_managed\s*=\s*color\s*&&\s*color->mode\s*==\s*DT_PRINT_COLOR_DARKTABLE_MANAGED\s*&&\s*color->printer_profile\s*&&\s*\*color->printer_profile\s*;' `
     -or $fileSubmit -notmatch 'dt_cups_set_color_options\(application_managed,\s*set_apple_color_matching,' `
     -or $fileSubmit -match 'pinfo->printer\.profile')
  {
    throw 'The CUPS file path does not wire the color context through the option helper'
  }
  if($cupsUtilsSource -notmatch 'application_managed\s*\?\s*"true"\s*:\s*"false"' `
     -or $cupsUtilsSource -notmatch '"AP\.ColorMatchingMode"' `
     -or $cupsUtilsSource -notmatch '"AP_ColorMatchingMode"' `
     -or $cupsUtilsSource -notmatch 'application_managed\s*\?\s*"AP_ApplicationColorMatching"\s*:\s*"AP_VendorColorMatching"')
  {
    throw 'The CUPS helper does not explicitly replace both macOS color modes'
  }
  $cancelHelperStart = $cupsUtilsSource.IndexOf('dt_cups_cancel_submitted_job(')
  $cancelHelperGuard = $cupsUtilsSource.IndexOf('job_id <= 0', $cancelHelperStart)
  $cancelHelperCall = $cupsUtilsSource.IndexOf('cancel_job(', $cancelHelperStart)
  if($cancelHelperStart -lt 0 -or $cancelHelperGuard -lt 0 -or $cancelHelperCall -lt 0 `
     -or $cancelHelperGuard -gt $cancelHelperCall)
  {
    throw 'The CUPS cancellation helper can dispatch a zero or negative job ID'
  }
  if($backendHeader -match 'common/win_print_utils\.h')
  {
    throw 'The generic print backend header still includes a Windows utility header'
  }
  $destLookup = $fileSubmit.IndexOf('cupsGetDest(pinfo->printer.name')
  $destNullCheck = -1
  $destDereference = -1
  if($destLookup -ge 0)
  {
    $destNullCheck = $fileSubmit.IndexOf('if(!dest)', $destLookup)
    $destDereference = $fileSubmit.IndexOf('dest->', $destLookup)
  }
  if($destLookup -lt 0 -or $destNullCheck -lt 0 -or $destDereference -lt 0 `
     -or $destNullCheck -gt $destDereference)
  {
    throw 'The CUPS file path may dereference a missing destination'
  }
  if($backendSource -match 'dt_pdf_finish\(pdf, NULL, 0\);\s*g_unlink\(filename\)')
  {
    throw 'PDF creation still unlinks the caller-owned filename after ICC embedding failure'
  }
  Write-Host 'PASS: exported CUPS adapter, full context, cancellation, and PDF ownership structure'
}
finally
{
  $env:PATH = $oldPath
  Set-Location $oldLocation
  if(Test-Path -LiteralPath $testDir)
  {
    Remove-Item -LiteralPath $testDir -Recurse -Force
  }
  $artifactsEmpty = (Test-Path -LiteralPath $artifactsRoot) `
                    -and -not (Get-ChildItem -LiteralPath $artifactsRoot -Force)
  if($artifactsEmpty)
  {
    Remove-Item -LiteralPath $artifactsRoot -Force
  }
}
