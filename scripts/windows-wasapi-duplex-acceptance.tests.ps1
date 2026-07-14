$ErrorActionPreference = "Stop"

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
}

function Invoke-DuplexAcceptanceCase {
  param(
    [string]$Name,
    [string[]]$Lines,
    [int]$ProcessExitCode = 0,
    [long]$DurationMs = 1000,
    [string[]]$ExtraArgument = @()
  )

  $logPath = Join-Path $temporaryDirectory "$Name.log"
  Set-Content -LiteralPath $logPath -Value $Lines -Encoding ASCII
  $arguments = @(
    "-NoProfile",
    "-File", (Join-Path $PSScriptRoot "windows-wasapi-duplex-acceptance.ps1"),
    "-InputPath", $logPath,
    "-ProcessExitCode", $ProcessExitCode,
    "-DurationMs", $DurationMs
  ) + $ExtraArgument
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = @(& $powershellPath @arguments 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Lines = $output
    Text = [string]::Join("`n", $output)
  }
}

function Assert-DuplexAcceptanceCase {
  param(
    [string]$Name,
    [string[]]$Lines,
    [int]$ExpectedExitCode,
    [string]$ExpectedText,
    [int]$ProcessExitCode = 0,
    [long]$DurationMs = 1000,
    [string[]]$ExtraArgument = @()
  )

  $result = Invoke-DuplexAcceptanceCase -Name $Name -Lines $Lines `
      -ProcessExitCode $ProcessExitCode -DurationMs $DurationMs `
      -ExtraArgument $ExtraArgument
  Assert-Equal $ExpectedExitCode $result.ExitCode `
      "Unexpected exit code for '$Name'. Output: $($result.Text)"
  Assert-Equal 1 @($result.Lines | Where-Object {
      $_ -match '^wasapi_duplex_acceptance(?:\s|$)'
    }).Count "Expected exactly one machine-readable summary for '$Name'."
  Assert-Equal $true $result.Text.Contains(
      "wasapi_duplex_acceptance passed=$([int]($ExpectedExitCode -eq 0))") `
      "Unexpected pass value for '$Name'."
  Assert-Equal $true $result.Text.Contains($ExpectedText) `
      "Unexpected summary for '$Name'."
}

$tokens = $null
$parseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot "windows-wasapi-duplex-acceptance.ps1"),
    [ref]$tokens,
    [ref]$parseErrors)
Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in acceptance script."

$powershellPath = Join-Path $PSHOME "powershell.exe"
if (!(Test-Path -LiteralPath $powershellPath -PathType Leaf)) {
  $powershellPath = (Get-Process -Id $PID).Path
}
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("sar-duplex-acceptance-" + [guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $temporaryDirectory)

$runtime = 'wasapi_runtime_summary health=healthy loop_cycles=100 graph_processed_cycles=90 captured_frames=48000 has_capture_stream=1 has_render_stream=1 render_sample_rate=48000 error_count=0'
$worker = 'wasapi_worker_stats render_wait_timeout_cycles=0 process_error_cycles=0 stream_start_error_cycles=0 stream_stop_error_cycles=0 rendered_frames=47520 render_recovery_silence_frames=48 render_startup_silence_frames=64 render_capture_starvation_silence_frames=32 maximum_render_recovery_silence_frames=48'
$engine = 'engine_diagnostics capture_fifo_overflow_cycles=0 capture_fifo_overflow_frames=0 render_fifo_overflow_cycles=0 render_fifo_overflow_frames=0 render_fifo_underflow_frames=144'
$goodLines = @($runtime, $worker, $engine)

try {
  Assert-DuplexAcceptanceCase -Name "pass" -Lines $goodLines -ExpectedExitCode 0 `
      -ExpectedText 'render_capture_starvation_silence_frames=32 unlabeled_render_underflow_frames=0'
  Assert-DuplexAcceptanceCase -Name "coverage-custom" -Lines @(
      $runtime,
      ($worker -replace 'rendered_frames=47520', 'rendered_frames=43200'),
      $engine) -ExpectedExitCode 0 -ExpectedText 'minimum_rendered_frames=43200' `
      -ExtraArgument @("-MinimumRenderedFrameCoverageBasisPoints", "9000")
  Assert-DuplexAcceptanceCase -Name "fractional-duration" -Lines @(
      ($runtime -replace 'render_sample_rate=48000', 'render_sample_rate=44101'),
      ($worker -replace 'rendered_frames=47520', 'rendered_frames=65534'),
      $engine) -DurationMs 1501 -ExpectedExitCode 0 `
      -ExpectedText 'target_rendered_frames=66195 minimum_rendered_frames=65534'
  Assert-DuplexAcceptanceCase -Name "recovery-threshold" -Lines $goodLines `
      -ExpectedExitCode 0 `
      -ExpectedText 'maximum_render_recovery_silence_frames_threshold=48' `
      -ExtraArgument @("-MaximumRenderRecoverySilenceFrames", "48")

  $legacyWorker = $worker -replace ' maximum_render_recovery_silence_frames=48', ''
  Assert-DuplexAcceptanceCase -Name "legacy-maximum-omitted" `
      -Lines @($runtime, $legacyWorker, $engine) -ExpectedExitCode 0 `
      -ExpectedText 'maximum_render_recovery_silence_frames=missing maximum_render_recovery_silence_frames_threshold=missing'
  Assert-DuplexAcceptanceCase -Name "legacy-maximum-required" `
      -Lines @($runtime, $legacyWorker, $engine) -ExpectedExitCode 1 `
      -ExpectedText 'reason="missing_maximum_render_recovery_silence_frames"' `
      -ExtraArgument @("-MaximumRenderRecoverySilenceFrames", "48")

  $oldBadRuntime = $runtime -replace 'error_count=0', 'error_count=9'
  $oldBadWorker = $worker -replace 'process_error_cycles=0', 'process_error_cycles=9'
  $oldBadEngine = $engine -replace 'capture_fifo_overflow_frames=0', 'capture_fifo_overflow_frames=9'
  Assert-DuplexAcceptanceCase -Name "last-summary-wins" `
      -Lines (@($oldBadRuntime, $oldBadWorker, $oldBadEngine) + $goodLines) `
      -ExpectedExitCode 0 -ExpectedText 'unlabeled_render_underflow_frames=0'

  $negativeCases = @(
    @("process-exit", $goodLines, 7, @(), "process_exit_code"),
    @("runtime-errors", @(($runtime -replace 'error_count=0', 'error_count=1'), $worker, $engine), 0, @(), "runtime_error_count_nonzero"),
    @("process-errors", @($runtime, ($worker -replace 'process_error_cycles=0', 'process_error_cycles=1'), $engine), 0, @(), "process_error_cycles_nonzero"),
    @("start-errors", @($runtime, ($worker -replace 'stream_start_error_cycles=0', 'stream_start_error_cycles=1'), $engine), 0, @(), "stream_start_error_cycles_nonzero"),
    @("stop-errors", @($runtime, ($worker -replace 'stream_stop_error_cycles=0', 'stream_stop_error_cycles=1'), $engine), 0, @(), "stream_stop_error_cycles_nonzero"),
    @("render-timeout", @($runtime, ($worker -replace 'render_wait_timeout_cycles=0', 'render_wait_timeout_cycles=1'), $engine), 0, @(), "render_wait_timeout_cycles_nonzero"),
    @("capture-overflow-cycle", @($runtime, $worker, ($engine -replace 'capture_fifo_overflow_cycles=0', 'capture_fifo_overflow_cycles=1')), 0, @(), "capture_fifo_overflow_cycles_nonzero"),
    @("capture-overflow-frame", @($runtime, $worker, ($engine -replace 'capture_fifo_overflow_frames=0', 'capture_fifo_overflow_frames=1')), 0, @(), "capture_fifo_overflow_frames_nonzero"),
    @("render-overflow-cycle", @($runtime, $worker, ($engine -replace 'render_fifo_overflow_cycles=0', 'render_fifo_overflow_cycles=1')), 0, @(), "render_fifo_overflow_cycles_nonzero"),
    @("render-overflow-frame", @($runtime, $worker, ($engine -replace 'render_fifo_overflow_frames=0', 'render_fifo_overflow_frames=1')), 0, @(), "render_fifo_overflow_frames_nonzero"),
    @("no-loop-cycles", @(($runtime -replace 'loop_cycles=100', 'loop_cycles=0'), $worker, $engine), 0, @(), "loop_cycles_zero"),
    @("no-graph-cycles", @(($runtime -replace 'graph_processed_cycles=90', 'graph_processed_cycles=0'), $worker, $engine), 0, @(), "graph_processed_cycles_zero"),
    @("no-capture-frames", @(($runtime -replace 'captured_frames=48000', 'captured_frames=0'), $worker, $engine), 0, @(), "captured_frames_zero"),
    @("no-render-frames", @($runtime, ($worker -replace 'rendered_frames=47520', 'rendered_frames=0'), $engine), 0, @(), "rendered_frames_zero"),
    @("capture-stream-missing", @(($runtime -replace 'has_capture_stream=1', 'has_capture_stream=0'), $worker, $engine), 0, @(), "capture_stream_required"),
    @("render-stream-missing", @(($runtime -replace 'has_render_stream=1', 'has_render_stream=0'), $worker, $engine), 0, @(), "render_stream_required"),
    @("starvation-unlabeled-underflow", @($runtime, ($worker -replace 'render_capture_starvation_silence_frames=32', 'render_capture_starvation_silence_frames=31'), $engine), 0, @(), "unlabeled_render_underflow_frames_nonzero"),
    @("starvation-label-over-total", @($runtime, ($worker -replace 'render_capture_starvation_silence_frames=32', 'render_capture_starvation_silence_frames=33'), $engine), 0, @(), "render_underflow_label_frames_exceed_total"),
    @("coverage", @($runtime, ($worker -replace 'rendered_frames=47520', 'rendered_frames=47519'), $engine), 0, @(), "rendered_frame_coverage_below_minimum"),
    @("recovery-maximum", $goodLines, 0, @("-MaximumRenderRecoverySilenceFrames", "47"), "maximum_render_recovery_silence_frames_exceeded")
  )
  foreach ($case in $negativeCases) {
    Assert-DuplexAcceptanceCase -Name $case[0] -Lines $case[1] `
        -ProcessExitCode $case[2] -ExtraArgument $case[3] `
        -ExpectedExitCode 1 -ExpectedText $case[4]
  }

  Assert-DuplexAcceptanceCase -Name "missing-summaries" -Lines @("unrelated") `
      -ExpectedExitCode 1 -ExpectedText 'missing_wasapi_runtime_summary,missing_wasapi_worker_stats,missing_engine_diagnostics'
  Assert-DuplexAcceptanceCase -Name "missing-startup-field" `
      -Lines @($runtime, ($worker -replace ' render_startup_silence_frames=64', ''), $engine) `
      -ExpectedExitCode 1 -ExpectedText 'missing_render_startup_silence_frames'
  Assert-DuplexAcceptanceCase -Name "missing-capture-starvation-field" `
      -Lines @($runtime, ($worker -replace ' render_capture_starvation_silence_frames=32', ''), $engine) `
      -ExpectedExitCode 1 `
      -ExpectedText 'missing_render_capture_starvation_silence_frames'
  Assert-DuplexAcceptanceCase -Name "invalid-field" `
      -Lines @($runtime, ($worker -replace 'rendered_frames=47520', 'rendered_frames=-1'), $engine) `
      -ExpectedExitCode 1 -ExpectedText 'invalid_rendered_frames'
  Assert-DuplexAcceptanceCase -Name "zero-sample-rate" `
      -Lines @(($runtime -replace 'render_sample_rate=48000', 'render_sample_rate=0'), $worker, $engine) `
      -ExpectedExitCode 1 -ExpectedText 'render_sample_rate_zero'
  Assert-DuplexAcceptanceCase -Name "target-overflow" `
      -Lines @(($runtime -replace 'render_sample_rate=48000', 'render_sample_rate=9223372036854775807'), $worker, $engine) `
      -ExpectedExitCode 1 -ExpectedText 'target_rendered_frames_overflow' `
      -DurationMs 2000

  $fakeToolPath = Join-Path $temporaryDirectory "fake-duplex-measure.cmd"
  Set-Content -LiteralPath $fakeToolPath -Encoding ASCII -Value @(
    "@echo off",
    "if not `%1==--duration-ms exit /b 8",
    "if not `%2==1000 exit /b 9",
    "echo $runtime",
    "echo $worker",
    "echo $engine",
    "exit /b 0"
  )
  $runArguments = @(
    "-NoProfile",
    "-File", (Join-Path $PSScriptRoot "windows-wasapi-duplex-acceptance.ps1"),
    "-ToolPath", $fakeToolPath,
    "-DurationMs", 1000
  )
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $runOutput = @(& $powershellPath @runArguments 2>&1 | ForEach-Object { [string]$_ })
    $runExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  Assert-Equal 0 $runExitCode "Direct tool invocation should pass."
  Assert-Equal $true ([string]::Join("`n", $runOutput)).Contains(
      "wasapi_duplex_acceptance passed=1 process_exit_code=0") `
      "Direct invocation did not emit a passing summary."
} finally {
  Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
}

Write-Output "WASAPI duplex acceptance tests passed"
