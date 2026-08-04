[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$DarktableExe,

  [Parameter(Mandatory = $true)]
  [string]$Image,

  [Parameter(Mandatory = $true)]
  [string]$OutputDirectory,

  [string]$PrinterName = 'Microsoft Print to PDF'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$darktablePath = (Resolve-Path -LiteralPath $DarktableExe).Path
$imagePath = (Resolve-Path -LiteralPath $Image).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)

$printer = Get-Printer -Name $PrinterName -ErrorAction Stop
if($printer.DriverName -ine 'Microsoft Print To PDF')
{
  throw "Queue '$PrinterName' uses '$($printer.DriverName)', not the Microsoft Print To PDF driver."
}

if(!(Test-Path -LiteralPath $outputRoot))
{
  New-Item -ItemType Directory -Path $outputRoot | Out-Null
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runs = @(
  [pscustomobject]@{
    Name = 'driver-managed'
    Instructions = 'Select no printer profile so the Windows driver manages color.'
  },
  [pscustomobject]@{
    Name = 'darktable-managed'
    Instructions = 'Select a real printer profile so darktable manages color.'
  }
)

$outputs = @()
$previousOutput = $env:DARKTABLE_WIN_PRINT_OUTPUT_FILE
try
{
  foreach($run in $runs)
  {
    $output = Join-Path $outputRoot ("darktable-$($run.Name)-$stamp.pdf")
    if(Test-Path -LiteralPath $output)
    {
      throw "Refusing to overwrite existing output: $output"
    }

    Write-Host ''
    Write-Host "Smoke run: $($run.Name)"
    Write-Host "Select queue '$PrinterName', A4 paper, and one image."
    Write-Host $run.Instructions
    Write-Host 'Print once, verify the job completes, then close darktable.'

    $env:DARKTABLE_WIN_PRINT_OUTPUT_FILE = $output
    $process = Start-Process -FilePath $darktablePath -ArgumentList @($imagePath) -PassThru
    $process.WaitForExit()
    if($process.ExitCode -ne 0)
    {
      throw "darktable exited with code $($process.ExitCode) during $($run.Name)."
    }

    if(!(Test-Path -LiteralPath $output -PathType Leaf))
    {
      throw "Expected PDF was not created: $output"
    }
    $file = Get-Item -LiteralPath $output
    if($file.Length -le 5)
    {
      throw "PDF is empty or truncated: $output"
    }
    $stream = [IO.File]::OpenRead($output)
    try
    {
      $header = New-Object byte[] 5
      if($stream.Read($header, 0, 5) -ne 5 -or [Text.Encoding]::ASCII.GetString($header) -ne '%PDF-')
      {
        throw "Output is not a PDF: $output"
      }
    }
    finally
    {
      $stream.Dispose()
    }
    $outputs += $output
  }
}
finally
{
  $env:DARKTABLE_WIN_PRINT_OUTPUT_FILE = $previousOutput
}

$hashes = $outputs | ForEach-Object { (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash }
if($hashes[0] -eq $hashes[1])
{
  throw 'The driver-managed and darktable-managed PDFs are byte-identical.'
}

Write-Host ''
Write-Host 'PASS: both PDFs are nonempty, structurally identifiable, and distinct.'
$outputs | ForEach-Object { Write-Host $_ }
