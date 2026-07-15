$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "windows-wasapi-soak-runner.psm1") -Force

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
}

function New-ValidSummary {
  param(
    [ValidateSet("render", "duplex", "loopback")]
    [string]$Mode,
    [hashtable]$Override = @{}
  )

  $values = [ordered]@{
    capture_discontinuity_cycles = "2"
    render_fifo_underflow_cycles = "3"
    wait_timeout_cycles = "0"
    render_wait_timeout_cycles = "0"
    duplex_event_wait_timeout_cycles = "0"
    capture_fifo_overflow_cycles = "0"
    capture_fifo_overflow_frames = "0"
    render_fifo_overflow_cycles = "0"
    render_fifo_overflow_frames = "0"
    process_error_cycles = "0"
    stream_start_error_cycles = "0"
    stream_stop_error_cycles = "0"
    rendered_frames = "48000"
    render_sample_rate = "48000"
    render_fifo_underflow_frames = "96"
    render_startup_silence_frames = "32"
    render_recovery_silence_frames = "48"
    render_recovery_silence_episodes = "1"
    render_capture_starvation_silence_frames = "16"
    maximum_render_recovery_silence_frames = "48"
  }
  foreach ($name in $Override.Keys) {
    $values[$name] = [string]$Override[$name]
  }
  return "wasapi_runtime_summary " + [string]::Join(
      " ", @($values.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }))
}

function Invoke-SingleSummary {
  param(
    [ValidateSet("render", "duplex", "loopback")]
    [string]$Mode,
    [string]$Summary,
    [string]$Duration = "1000",
    [int]$ExitCode = 0,
    [string[]]$ExtraLines = @(),
    [uint64]$MaximumRenderRecoverySilenceFrames = 0,
    [uint32]$MinimumRenderedFrameCoverageBasisPoints = 9900
  )

  return @(Invoke-WasapiSoak -Mode $Mode -Iterations 1 `
      -MaximumRenderRecoverySilenceFrames $MaximumRenderRecoverySilenceFrames `
      -MinimumRenderedFrameCoverageBasisPoints $MinimumRenderedFrameCoverageBasisPoints `
      -RunMeasurement {
    Write-Host "  Duration ms: $Duration"
    Write-Host $Summary
    foreach ($line in $ExtraLines) { Write-Host $line }
    return $ExitCode
  })
}

foreach ($scriptName in @("windows-wasapi-soak-runner.psm1", "windows-winrm-local-measure.ps1")) {
  $tokens = $null
  $parseErrors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile(
      (Join-Path $PSScriptRoot $scriptName), [ref]$tokens, [ref]$parseErrors)
  Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in '$scriptName'."
}

$expectedModes = @{
  render = "render"
  duplex = "duplex"
  loopback = "loopback"
  both = "render,duplex"
  all = "render,duplex,loopback"
}
foreach ($mode in $expectedModes.Keys) {
  $actual = [string]::Join(",", @(Get-WasapiSoakModeNames -Mode $mode))
  Assert-Equal $expectedModes[$mode] $actual "Unexpected expansion for mode '$mode'."
}

$calls = [System.Collections.Generic.List[string]]::new()
$output = @(Invoke-WasapiSoak -Mode all -Iterations 2 -RunMeasurement {
  param($modeName, $iteration)
  $calls.Add("$iteration`:$modeName") | Out-Null
  Write-Host "  Duration ms: 1000"
  Write-Host (New-ValidSummary -Mode $modeName)
  if ($modeName -eq "loopback" -and $iteration -eq 2) { return 1 }
  return 0
})
$result = $output[-1]
$text = [string]::Join("`n", $output[0..($output.Count - 2)])
Assert-Equal 6 $calls.Count "Unexpected invocation count."
Assert-Equal "1:render,1:duplex,1:loopback,2:render,2:duplex,2:loopback" ([string]::Join(",", $calls)) "Unexpected invocation order."
Assert-Equal 6 $result.Attempts "Unexpected attempt count."
Assert-Equal 1 $result.FailureCount "Unexpected process failure count."
Assert-Equal 0 $result.ParseFailureCount "Valid summaries should parse."
Assert-Equal 0 $result.GateFailureCount "Healthy summaries should pass the gate."
Assert-Equal 5 $result.TotalMetrics.AcceptedAttempts "A nonzero process exit must not be accepted."
Assert-Equal $true $text.Contains("[soak] completed modes=3 iterations=2 attempts=6 failures=1") "Missing aggregate summary."

$metricOutput = @(Invoke-WasapiSoak -Mode duplex -Iterations 2 -RunMeasurement {
  param($modeName, $iteration)
  Write-Host "  Duration ms: $($iteration * 1000)"
  if ($iteration -eq 1) {
    Write-Host (New-ValidSummary -Mode duplex)
  } else {
    Write-Host (New-ValidSummary -Mode duplex -Override @{
      capture_discontinuity_cycles = 4
      render_fifo_underflow_cycles = 6
      rendered_frames = 96000
      render_fifo_underflow_frames = 192
      render_startup_silence_frames = 64
      render_recovery_silence_frames = 96
      render_recovery_silence_episodes = 3
      render_capture_starvation_silence_frames = 32
    })
  }
  return 0
})
$metricResult = $metricOutput[-1]
$metricText = [string]::Join("`n", $metricOutput[0..($metricOutput.Count - 2)])
$duplexMetrics = $metricResult.MetricsByMode.duplex
Assert-Equal 2 $duplexMetrics.ParsedAttempts "Unexpected parsed attempt count."
Assert-Equal 2 $duplexMetrics.AcceptedAttempts "Unexpected accepted attempt count."
Assert-Equal 3000 $duplexMetrics.DurationMilliseconds "Unexpected measured duration."
Assert-Equal 6 $duplexMetrics.Totals.capture_discontinuity_cycles "Unexpected discontinuity total."
Assert-Equal 9 $duplexMetrics.Totals.render_fifo_underflow_cycles "Unexpected underflow total."
Assert-Equal 144000 $duplexMetrics.Totals.rendered_frames "Unexpected rendered frame total."
Assert-Equal 144000 $duplexMetrics.Totals.target_rendered_frames "Unexpected target frame total."
Assert-Equal 288 $duplexMetrics.Totals.render_fifo_underflow_frames "Unexpected underflow frame total."
Assert-Equal 4 $duplexMetrics.Totals.render_recovery_silence_episodes `
    "Unexpected recovery silence episode total."
Assert-Equal 48 $duplexMetrics.Totals.maximum_render_recovery_silence_frames `
    "Recovery episode maximum must not be summed across attempts."
Assert-Equal $true $metricText.Contains(
    "maximum_render_recovery_silence_frames_maximum=48") `
    "Missing recovery episode maximum summary."
Assert-Equal $true $metricText.Contains(
    "render_recovery_silence_episodes_total=4") `
    "Missing recovery silence episode total summary."
Assert-Equal $true $metricText.Contains(
    "render_recovery_silence_episodes_per_second=1.333333") `
    "Missing normalized recovery silence episode rate."
Assert-Equal $true $metricText.Contains("duration_seconds=3.000") "Missing normalized duration."

$gateCases = @(
  @{ Name = "process errors"; Override = @{ process_error_cycles = 1 }; Reason = "process_error_cycles_nonzero" },
  @{ Name = "start errors"; Override = @{ stream_start_error_cycles = 1 }; Reason = "stream_start_error_cycles_nonzero" },
  @{ Name = "stop errors"; Override = @{ stream_stop_error_cycles = 1 }; Reason = "stream_stop_error_cycles_nonzero" },
  @{ Name = "wait timeouts"; Override = @{ wait_timeout_cycles = 1 }; Reason = "wait_timeout_cycles_nonzero" },
  @{ Name = "render wait timeouts"; Override = @{ render_wait_timeout_cycles = 1 }; Reason = "render_wait_timeout_cycles_nonzero" },
  @{ Name = "capture overflow"; Override = @{ capture_fifo_overflow_cycles = 1 }; Reason = "capture_fifo_overflow_cycles_nonzero" },
  @{ Name = "capture overflow frames"; Override = @{ capture_fifo_overflow_frames = 1 }; Reason = "capture_fifo_overflow_frames_nonzero" },
  @{ Name = "render overflow"; Override = @{ render_fifo_overflow_cycles = 1 }; Reason = "render_fifo_overflow_cycles_nonzero" },
  @{ Name = "render overflow frames"; Override = @{ render_fifo_overflow_frames = 1 }; Reason = "render_fifo_overflow_frames_nonzero" },
  @{ Name = "low coverage"; Override = @{ rendered_frames = 47519 }; Reason = "rendered_frame_coverage_below_minimum" },
  @{ Name = "zero sample rate"; Override = @{ render_sample_rate = 0 }; Reason = "render_sample_rate_zero" },
  @{ Name = "excess recovery silence episodes"; Override = @{ render_recovery_silence_episodes = 3 }; Reason = "recovery_silence_episodes_exceed_discontinuities" },
  @{ Name = "unattributed underflow"; Override = @{ render_fifo_underflow_frames = 97 }; Reason = "render_underflow_not_exactly_attributed" },
  @{ Name = "over-attributed underflow"; Override = @{ render_fifo_underflow_frames = 95 }; Reason = "render_underflow_not_exactly_attributed" }
)
foreach ($case in $gateCases) {
  $caseOutput = Invoke-SingleSummary -Mode duplex `
      -Summary (New-ValidSummary -Mode duplex -Override $case.Override)
  $caseResult = $caseOutput[-1]
  $caseText = [string]::Join("`n", $caseOutput[0..($caseOutput.Count - 2)])
  Assert-Equal 1 $caseResult.FailureCount "$($case.Name) should fail the attempt."
  Assert-Equal 1 $caseResult.GateFailureCount "$($case.Name) should count as a gate failure."
  Assert-Equal 0 $caseResult.ParseFailureCount "$($case.Name) is syntactically valid."
  Assert-Equal $true $caseText.Contains("reason=$($case.Reason)") "Missing $($case.Name) reason."
}

$recoveryBoundOutput = Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex -Override @{
      maximum_render_recovery_silence_frames = 49
    }) -MaximumRenderRecoverySilenceFrames 48
$recoveryBoundResult = $recoveryBoundOutput[-1]
$recoveryBoundText = [string]::Join("`n", $recoveryBoundOutput[0..($recoveryBoundOutput.Count - 2)])
Assert-Equal 1 $recoveryBoundResult.FailureCount `
    "Recovery silence above the configured bound must fail."
Assert-Equal $true $recoveryBoundText.Contains(
    "maximum_render_recovery_silence_frames_exceeded") `
    "Missing recovery silence bound failure reason."

$strictCoverageOutput = Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex -Override @{ rendered_frames = 47995 }) `
    -MinimumRenderedFrameCoverageBasisPoints 9999
Assert-Equal 1 $strictCoverageOutput[-1].FailureCount `
    "99.99 percent coverage must reject 47,995 of 48,000 frames."

# Exactly 99% coverage passes; one frame below the ceiling-adjusted threshold fails.
$coveragePass = (Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex -Override @{ rendered_frames = 47520 }))[-1]
Assert-Equal 0 $coveragePass.FailureCount "Exact minimum rendered coverage should pass."

$observedClockCoverage = (Invoke-SingleSummary -Mode duplex `
    -Duration "30000" `
    -Summary (New-ValidSummary -Mode duplex -Override @{
      rendered_frames = 1419072
    }) `
    -ExtraLines @(
      "wasapi_duplex_clock render_drift_valid=1 render_observed_rate=47248.7"
    ))[-1]
Assert-Equal 0 $observedClockCoverage.FailureCount `
    "Coverage should use the valid observed render clock instead of its nominal rate."
Assert-Equal 1417461 `
    $observedClockCoverage.TotalMetrics.Totals.target_rendered_frames `
    "Unexpected observed-clock render target."

$invalidObservedClock = (Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex) `
    -ExtraLines @(
      "wasapi_duplex_clock render_drift_valid=1 render_observed_rate=not-a-number"
    ))[-1]
Assert-Equal 1 $invalidObservedClock.ParseFailureCount `
    "Invalid observed render clock rate should fail parsing."

$missingFields = @(
  "process_error_cycles",
  "stream_start_error_cycles",
  "stream_stop_error_cycles",
  "wait_timeout_cycles",
  "render_wait_timeout_cycles",
  "capture_fifo_overflow_cycles",
  "capture_fifo_overflow_frames",
  "render_fifo_overflow_cycles",
  "render_fifo_overflow_frames",
  "rendered_frames",
  "render_sample_rate",
  "render_fifo_underflow_frames",
  "render_startup_silence_frames",
  "render_recovery_silence_frames",
  "render_recovery_silence_episodes",
  "render_capture_starvation_silence_frames"
)
foreach ($missingName in $missingFields) {
  $summary = New-ValidSummary -Mode duplex
  $summary = $summary -replace "\s$([regex]::Escape($missingName))=[^\s]+", ""
  $missingOutput = Invoke-SingleSummary -Mode duplex -Summary $summary
  $missingResult = $missingOutput[-1]
  $missingText = [string]::Join("`n", $missingOutput[0..($missingOutput.Count - 2)])
  Assert-Equal 1 $missingResult.ParseFailureCount "Missing $missingName should fail parsing."
  Assert-Equal 1 $missingResult.FailureCount "Missing $missingName should fail the attempt."
  Assert-Equal $true $missingText.Contains("reason=missing_metric:$missingName") "Missing explicit $missingName reason."
}

$legacySummary = New-ValidSummary -Mode duplex
$legacySummary = $legacySummary -replace
    "\sduplex_event_wait_timeout_cycles=[^\s]+", ""
$legacyOutput = Invoke-SingleSummary -Mode duplex -Summary $legacySummary
Assert-Equal 0 $legacyOutput[-1].ParseFailureCount `
    "Legacy output without duplex event timeout telemetry should parse."

$invalidOptionalOutput = Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex -Override @{
      duplex_event_wait_timeout_cycles = "not-a-number"
    })
Assert-Equal 1 $invalidOptionalOutput[-1].ParseFailureCount `
    "Present but invalid duplex event timeout telemetry should fail parsing."

$invalidRecoveryEpisodesOutput = Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex -Override @{
      render_recovery_silence_episodes = "not-a-number"
    })
Assert-Equal 1 $invalidRecoveryEpisodesOutput[-1].ParseFailureCount `
    "Invalid recovery silence episode telemetry should fail parsing."

foreach ($invalidValue in @("-1", "+1", "1.5", "not-a-number", "18446744073709551616")) {
  $invalidOutput = Invoke-SingleSummary -Mode duplex `
      -Summary (New-ValidSummary -Mode duplex -Override @{ process_error_cycles = $invalidValue })
  $invalidResult = $invalidOutput[-1]
  Assert-Equal 1 $invalidResult.ParseFailureCount "Invalid unsigned value '$invalidValue' should fail parsing."
  Assert-Equal 1 $invalidResult.FailureCount "Invalid unsigned value '$invalidValue' should fail the attempt."
}

$duplicateOutput = Invoke-SingleSummary -Mode duplex `
    -Summary ((New-ValidSummary -Mode duplex) + " process_error_cycles=0")
$duplicateText = [string]::Join("`n", $duplicateOutput[0..($duplicateOutput.Count - 2)])
Assert-Equal 1 $duplicateOutput[-1].ParseFailureCount "Duplicate metrics should fail parsing."
Assert-Equal $true $duplicateText.Contains("reason=duplicate_metric:process_error_cycles") "Missing duplicate metric reason."

$duplicateSummaryOutput = Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex) `
    -ExtraLines @(New-ValidSummary -Mode duplex)
Assert-Equal 1 $duplicateSummaryOutput[-1].ParseFailureCount "Duplicate summaries should fail parsing."

$invalidDurationOutput = Invoke-SingleSummary -Mode duplex `
    -Summary (New-ValidSummary -Mode duplex) -Duration "0"
Assert-Equal 1 $invalidDurationOutput[-1].ParseFailureCount "Zero duration should fail parsing."

# Render does not require duplex-only attribution fields.
$renderSummary = New-ValidSummary -Mode render
foreach ($name in @(
    "render_fifo_underflow_frames",
    "render_startup_silence_frames",
    "render_recovery_silence_frames",
    "render_recovery_silence_episodes",
    "render_capture_starvation_silence_frames")) {
  $renderSummary = $renderSummary -replace "\s$([regex]::Escape($name))=[^\s]+", ""
}
$renderResult = (Invoke-SingleSummary -Mode render -Summary $renderSummary)[-1]
Assert-Equal 0 $renderResult.FailureCount "Render mode should not require duplex attribution."

# Loopback is capture-only and therefore does not require render coverage or render wait fields.
$loopbackSummary = New-ValidSummary -Mode loopback
foreach ($name in @("render_wait_timeout_cycles", "rendered_frames", "render_sample_rate")) {
  $loopbackSummary = $loopbackSummary -replace "\s$([regex]::Escape($name))=[^\s]+", ""
}
$loopbackResult = (Invoke-SingleSummary -Mode loopback -Summary $loopbackSummary)[-1]
Assert-Equal 0 $loopbackResult.FailureCount "Loopback mode should preserve capture-only behavior."

$rawOutput = @(Invoke-WasapiSoak -Mode render -Iterations 1 -RunMeasurement {
  Write-Host "raw measurement marker"
  Write-Host "  Duration ms: 1000"
  Write-Host (New-ValidSummary -Mode render)
  return 0
} 6>&1)
Assert-Equal $true ([string]::Join("`n", $rawOutput).Contains("raw measurement marker")) "Raw measurement output was not replayed."

$nullResult = @(Invoke-WasapiSoak -Mode render -Iterations 2 -RunMeasurement {
  return $null
})[-1]
Assert-Equal 2 $nullResult.FailureCount "Missing output must fail every attempt."
Assert-Equal 2 $nullResult.ParseFailureCount "Missing output should be reported as parse failures."

$invalidIterationsRejected = $false
try {
  Invoke-WasapiSoak -Mode render -Iterations 0 -RunMeasurement { return 0 } | Out-Null
} catch {
  $invalidIterationsRejected = $true
}
Assert-Equal $true $invalidIterationsRejected "Zero iterations should be rejected."

Write-Output "WASAPI soak runner tests passed"
