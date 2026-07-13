$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "windows-wasapi-soak-runner.psm1") -Force

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
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
$output = @(Invoke-WasapiSoak -Mode all -Iterations 3 -RunMeasurement {
  param($modeName, $iteration)
  $calls.Add("$iteration`:$modeName") | Out-Null
  if (($modeName -eq "render" -and $iteration -eq 2) -or $modeName -eq "loopback") {
    return 1
  }
  return 0
})
$result = $output[-1]
$text = [string]::Join("`n", $output[0..($output.Count - 2)])

Assert-Equal 9 $calls.Count "Unexpected invocation count."
Assert-Equal "1:render,1:duplex,1:loopback,2:render,2:duplex,2:loopback,3:render,3:duplex,3:loopback" ([string]::Join(",", $calls)) "Unexpected invocation order."
Assert-Equal 3 $result.Iterations "Unexpected result iteration count."
Assert-Equal 9 $result.Attempts "Unexpected result attempt count."
Assert-Equal 4 $result.FailureCount "Unexpected total failure count."
Assert-Equal 1 $result.FailuresByMode.render "Unexpected render failure count."
Assert-Equal 0 $result.FailuresByMode.duplex "Unexpected duplex failure count."
Assert-Equal 3 $result.FailuresByMode.loopback "Unexpected loopback failure count."
Assert-Equal $true $text.Contains("[soak] mode=render iterations=3 failures=1") "Missing render summary."
Assert-Equal $true $text.Contains("[soak] mode=duplex iterations=3 failures=0") "Missing duplex summary."
Assert-Equal $true $text.Contains("[soak] mode=loopback iterations=3 failures=3") "Missing loopback summary."
Assert-Equal $true $text.Contains("[soak] completed modes=3 iterations=3 attempts=9 failures=4") "Missing aggregate summary."
Assert-Equal 9 $result.ParseFailureCount "Missing output should be reported as a parse failure."

$metricOutput = @(Invoke-WasapiSoak -Mode duplex -Iterations 2 -RunMeasurement {
  param($modeName, $iteration)
  Write-Host "Measurement"
  Write-Host "  Duration ms: $($iteration * 1000)"
  if ($iteration -eq 1) {
    Write-Host "wasapi_runtime_summary capture_discontinuity_cycles=2 render_fifo_underflow_cycles=3 wait_timeout_cycles=4 capture_fifo_overflow_cycles=5 render_fifo_overflow_cycles=6"
  } else {
    Write-Host "wasapi_runtime_summary capture_discontinuity_cycles=4 render_fifo_underflow_cycles=6 wait_timeout_cycles=8 capture_fifo_overflow_cycles=10 render_fifo_overflow_cycles=12"
  }
  return 0
})
$metricResult = $metricOutput[-1]
$metricText = [string]::Join("`n", $metricOutput[0..($metricOutput.Count - 2)])
$duplexMetrics = $metricResult.MetricsByMode.duplex

Assert-Equal 0 $metricResult.ParseFailureCount "Valid measurement output should parse."
Assert-Equal 2 $duplexMetrics.ParsedAttempts "Unexpected parsed attempt count."
Assert-Equal 3000 $duplexMetrics.DurationMilliseconds "Unexpected measured duration."
Assert-Equal 6 $duplexMetrics.Totals.capture_discontinuity_cycles "Unexpected discontinuity total."
Assert-Equal 9 $duplexMetrics.Totals.render_fifo_underflow_cycles "Unexpected underflow total."
Assert-Equal 12 $duplexMetrics.Totals.wait_timeout_cycles "Unexpected timeout total."
Assert-Equal 15 $duplexMetrics.Totals.capture_fifo_overflow_cycles "Unexpected capture overflow total."
Assert-Equal 18 $duplexMetrics.Totals.render_fifo_overflow_cycles "Unexpected render overflow total."
Assert-Equal $true $metricText.Contains("duration_seconds=3.000") "Missing normalized duration."
Assert-Equal $true $metricText.Contains("capture_discontinuity_cycles_total=6 capture_discontinuity_cycles_per_second=2.000000") "Missing discontinuity rate."
Assert-Equal $true $metricText.Contains("render_fifo_underflow_cycles_total=9 render_fifo_underflow_cycles_per_second=3.000000") "Missing underflow rate."

$malformedOutput = @(Invoke-WasapiSoak -Mode render -Iterations 1 -RunMeasurement {
  Write-Host "  Duration ms: 1000"
  Write-Host "wasapi_runtime_summary capture_discontinuity_cycles=not-a-number"
  return 0
})
$malformedResult = $malformedOutput[-1]
$malformedText = [string]::Join("`n", $malformedOutput[0..($malformedOutput.Count - 2)])
Assert-Equal 1 $malformedResult.ParseFailureCount "Malformed metrics should count as a parse failure."
Assert-Equal $true $malformedText.Contains("[soak] parse_failure mode=render iteration=1 reason=invalid_metric:capture_discontinuity_cycles") "Missing explicit parse failure."
Assert-Equal $true $malformedText.Contains("parsed=0 parse_failures=1 duration_seconds=0.000") "Missing parse failure summary."

$rawOutput = @(Invoke-WasapiSoak -Mode render -Iterations 1 -RunMeasurement {
  Write-Host "raw measurement marker"
  Write-Host "  Duration ms: 1000"
  Write-Host "wasapi_runtime_summary capture_discontinuity_cycles=0 render_fifo_underflow_cycles=0 wait_timeout_cycles=0 capture_fifo_overflow_cycles=0 render_fifo_overflow_cycles=0"
  return 0
} 6>&1)
Assert-Equal $true ([string]::Join("`n", $rawOutput).Contains("raw measurement marker")) "Raw measurement output was not replayed."

$nullResult = @(Invoke-WasapiSoak -Mode render -Iterations 2 -RunMeasurement {
  return $null
})[-1]
Assert-Equal 0 $nullResult.FailureCount "Null runner output should mean success."

$invalidIterationsRejected = $false
try {
  Invoke-WasapiSoak -Mode render -Iterations 0 -RunMeasurement { return 0 } | Out-Null
} catch {
  $invalidIterationsRejected = $true
}
Assert-Equal $true $invalidIterationsRejected "Zero iterations should be rejected."

Write-Output "WASAPI soak runner tests passed"
