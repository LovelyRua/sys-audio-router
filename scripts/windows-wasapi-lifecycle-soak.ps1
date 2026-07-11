param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDir,
  [ValidateSet("render", "duplex", "loopback", "both", "all")]
  [string]$Mode = "all",
  [ValidateRange(1, [uint32]::MaxValue)]
  [uint32]$Iterations = 10,
  [uint32]$DurationMs = 250,
  [uint32]$WaitTimeoutMs = 10,
  [ValidateRange(1, [uint32]::MaxValue)]
  [uint32]$StopTimeoutMs = 2000,
  [switch]$RequireHealthy
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "windows-wasapi-lifecycle-soak.psm1") -Force
$resolvedBuildDir = (Resolve-Path $BuildDir).Path
$executables = @{
  render = Join-Path $resolvedBuildDir "sar_measure_wasapi_render_loop.exe"
  duplex = Join-Path $resolvedBuildDir "sar_measure_wasapi_duplex_loop.exe"
  loopback = Join-Path $resolvedBuildDir "sar_measure_wasapi_loopback_loop.exe"
}

$output = @(Invoke-WasapiLifecycleSoak -Mode $Mode -Iterations $Iterations -RunAttempt {
  param($modeName, $iteration)
  $executable = $executables[$modeName]
  if (!(Test-Path -LiteralPath $executable)) {
    throw "Lifecycle executable not found: $executable"
  }

  $stdoutPath = Join-Path $env:TEMP "sar-lifecycle-$PID-$modeName-$iteration.stdout.log"
  $stderrPath = Join-Path $env:TEMP "sar-lifecycle-$PID-$modeName-$iteration.stderr.log"
  $arguments = @("--duration-ms", "$DurationMs", "--timeout-ms", "$WaitTimeoutMs")
  if ($RequireHealthy) { $arguments += "--require-healthy" }
  $process = $null
  try {
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $deadline = [DateTime]::UtcNow.AddMilliseconds([uint64]$DurationMs + [uint64]$StopTimeoutMs)
    while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
      Start-Sleep -Milliseconds 10
      $process.Refresh()
    }

    $timedOut = !$process.HasExited
    if ($timedOut) {
      Stop-Process -Id $process.Id -Force
      $process.WaitForExit()
    }
    $stdout = if (Test-Path $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { "" }
    $stderr = if (Test-Path $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { "" }
    if ($stdout) { Write-Output $stdout.TrimEnd() }
    if ($stderr) { Write-Output $stderr.TrimEnd() }

    $started = $stdout -match "wasapi_lifecycle phase=started mode=$modeName"
    $stopping = $stdout -match "wasapi_lifecycle phase=stopping mode=$modeName"
    $stopped = $stdout -match "wasapi_lifecycle phase=stopped mode=$modeName"
    $exitCode = if ($timedOut) { -1 } else { $process.ExitCode }
    $outcome = if (!$started) {
      "startup_failure"
    } elseif ($timedOut -and $stopping -and !$stopped) {
      "stop_timeout"
    } elseif ($timedOut -or $exitCode -ne 0 -or !$stopped) {
      "runtime_failure"
    } else {
      "success"
    }
    return [pscustomobject]@{ Outcome = $outcome; ExitCode = $exitCode }
  } finally {
    if ($null -ne $process) { $process.Dispose() }
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
  }
})

$output[0..($output.Count - 2)] | Write-Output
$result = $output[-1]
if ($result.FailureCount -ne 0) { exit 1 }
