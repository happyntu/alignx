param(
  [string]$Alignx = "build/windows-debug/Debug/alignx.exe",
  [string]$Samtools = "samtools",
  [string]$InputBam = "tests/toy_data/toy_alignment.sorted.bam",
  [string]$Region = "chrToy:1-250",
  [string]$Output = "benchmarks/results/phase1_view_chrtoy_samtools.tsv"
)

$ErrorActionPreference = "Stop"

function Invoke-TimedCommand {
  param(
    [string]$Name,
    [string]$Exe,
    [string[]]$Args
  )

  if (-not (Get-Command $Exe -ErrorAction SilentlyContinue) -and -not (Test-Path $Exe)) {
    throw "Required executable not found: $Exe"
  }

  $stdout = New-TemporaryFile
  $stderr = New-TemporaryFile
  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  $process = Start-Process -FilePath $Exe -ArgumentList $Args -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $timer.Stop()

  $stdoutBytes = (Get-Item $stdout).Length
  Remove-Item $stdout, $stderr -Force

  [pscustomobject]@{
    tool = $Name
    wall_time_ms = [math]::Round($timer.Elapsed.TotalMilliseconds, 3)
    exit_code = $process.ExitCode
    stdout_bytes = $stdoutBytes
  }
}

$outputDir = Split-Path -Parent $Output
if ($outputDir) {
  New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

$results = @(
  Invoke-TimedCommand -Name "alignx" -Exe $Alignx -Args @("view", $InputBam, $Region)
  Invoke-TimedCommand -Name "samtools" -Exe $Samtools -Args @("view", $InputBam, $Region)
)

$results | Export-Csv -Path $Output -Delimiter "`t" -NoTypeInformation
Write-Output "Wrote $Output"
