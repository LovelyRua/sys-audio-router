[CmdletBinding(DefaultParameterSetName = "Run")]
param(
  [Parameter(Mandatory = $true, ParameterSetName = "Run")]
  [string]$ToolPath,

  [Parameter(ParameterSetName = "Run")]
  [string[]]$ToolArgument = @(),

  [Parameter(Mandatory = $true, ParameterSetName = "Log")]
  [string]$InputPath,

  [Parameter(Mandatory = $true, ParameterSetName = "Log")]
  [int]$ProcessExitCode,

  [Parameter(Mandatory = $true)]
  [ValidateRange(1, [long]::MaxValue)]
  [long]$DurationMs,

  [ValidateRange(0, 10000)]
  [int]$MinimumRenderedFrameCoverageBasisPoints = 9900,

  [Nullable[long]]$MaximumRenderRecoverySilenceFrames = $null
)

$ErrorActionPreference = "Stop"
if ($null -ne $MaximumRenderRecoverySilenceFrames -and
    $MaximumRenderRecoverySilenceFrames -lt 0) {
  throw "MaximumRenderRecoverySilenceFrames must be non-negative."
}

function ConvertFrom-DuplexKeyValueLine {
  param([string]$Line)

  $values = @{}
  $matches = [regex]::Matches(
      $Line,
      '(?:^|\s)(?<key>[A-Za-z_][A-Za-z0-9_]*)=(?<value>"(?:\\.|[^"\\])*"|[^\s]+)')
  foreach ($match in $matches) {
    $value = $match.Groups["value"].Value
    if ($value.Length -ge 2 -and $value[0] -eq '"' -and
        $value[$value.Length - 1] -eq '"') {
      $value = $value.Substring(1, $value.Length - 2)
      $value = $value -replace '\\([\\"])', '$1'
    }
    $values[$match.Groups["key"].Value] = $value
  }
  return $values
}

function ConvertTo-DuplexUnsignedInteger {
  param(
    [hashtable]$Values,
    [string]$Name,
    [System.Collections.Generic.List[string]]$Failures
  )

  if (!$Values.ContainsKey($Name)) {
    $Failures.Add("missing_$Name")
    return $null
  }

  [long]$parsed = 0
  if (![long]::TryParse(
          [string]$Values[$Name],
          [System.Globalization.NumberStyles]::None,
          [System.Globalization.CultureInfo]::InvariantCulture,
          [ref]$parsed) -or $parsed -lt 0) {
    $Failures.Add("invalid_$Name")
    return $null
  }
  return $parsed
}

function Get-DuplexLastSummary {
  param(
    [string[]]$Lines,
    [string]$Label
  )

  $last = $null
  foreach ($line in $Lines) {
    if ($line -match ('^' + [regex]::Escape($Label) + '(?:\s|$)')) {
      $last = ConvertFrom-DuplexKeyValueLine -Line $line
    }
  }
  return $last
}

function ConvertTo-DuplexSummaryNumber {
  param($Value)
  if ($null -eq $Value) { return "missing" }
  return [string]$Value
}

$lines = @()
$toolExitCode = $ProcessExitCode
if ($PSCmdlet.ParameterSetName -eq "Run") {
  if (!(Test-Path -LiteralPath $ToolPath -PathType Leaf)) {
    Write-Error "WASAPI duplex measurement tool was not found: $ToolPath"
  }
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $lines = @(& $ToolPath "--duration-ms" ([string]$DurationMs) @ToolArgument 2>&1 |
        ForEach-Object { [string]$_ })
    $toolExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
} else {
  if (!(Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    Write-Error "WASAPI duplex measurement log was not found: $InputPath"
  }
  $lines = @(Get-Content -LiteralPath $InputPath | ForEach-Object { [string]$_ })
}

$runtime = Get-DuplexLastSummary -Lines $lines -Label "wasapi_runtime_summary"
$worker = Get-DuplexLastSummary -Lines $lines -Label "wasapi_worker_stats"
$engine = Get-DuplexLastSummary -Lines $lines -Label "engine_diagnostics"
$failures = [System.Collections.Generic.List[string]]::new()
if ($toolExitCode -ne 0) {
  $failures.Add("process_exit_code")
}
if ($null -eq $runtime) { $failures.Add("missing_wasapi_runtime_summary") }
if ($null -eq $worker) { $failures.Add("missing_wasapi_worker_stats") }
if ($null -eq $engine) { $failures.Add("missing_engine_diagnostics") }

$runtimeErrorCount = $null
$loopCycles = $null
$graphProcessedCycles = $null
$capturedFrames = $null
$hasCaptureStream = $null
$hasRenderStream = $null
$renderSampleRate = $null
$processErrorCycles = $null
$streamStartErrorCycles = $null
$streamStopErrorCycles = $null
$renderWaitTimeoutCycles = $null
$renderedFrames = $null
$renderRecoverySilenceFrames = $null
$renderStartupSilenceFrames = $null
$observedMaximumRenderRecoverySilenceFrames = $null
$captureFifoOverflowCycles = $null
$captureFifoOverflowFrames = $null
$renderFifoOverflowCycles = $null
$renderFifoOverflowFrames = $null
$renderFifoUnderflowFrames = $null

if ($null -ne $runtime) {
  $runtimeErrorCount = ConvertTo-DuplexUnsignedInteger $runtime "error_count" $failures
  $loopCycles = ConvertTo-DuplexUnsignedInteger $runtime "loop_cycles" $failures
  $graphProcessedCycles = ConvertTo-DuplexUnsignedInteger $runtime "graph_processed_cycles" $failures
  $capturedFrames = ConvertTo-DuplexUnsignedInteger $runtime "captured_frames" $failures
  $hasCaptureStream = ConvertTo-DuplexUnsignedInteger $runtime "has_capture_stream" $failures
  $hasRenderStream = ConvertTo-DuplexUnsignedInteger $runtime "has_render_stream" $failures
  $renderSampleRate = ConvertTo-DuplexUnsignedInteger $runtime "render_sample_rate" $failures
}
if ($null -ne $worker) {
  $processErrorCycles = ConvertTo-DuplexUnsignedInteger $worker "process_error_cycles" $failures
  $streamStartErrorCycles = ConvertTo-DuplexUnsignedInteger $worker "stream_start_error_cycles" $failures
  $streamStopErrorCycles = ConvertTo-DuplexUnsignedInteger $worker "stream_stop_error_cycles" $failures
  $renderWaitTimeoutCycles = ConvertTo-DuplexUnsignedInteger $worker "render_wait_timeout_cycles" $failures
  $renderedFrames = ConvertTo-DuplexUnsignedInteger $worker "rendered_frames" $failures
  $renderRecoverySilenceFrames = ConvertTo-DuplexUnsignedInteger $worker "render_recovery_silence_frames" $failures
  $renderStartupSilenceFrames = ConvertTo-DuplexUnsignedInteger $worker "render_startup_silence_frames" $failures
  if ($worker.ContainsKey("maximum_render_recovery_silence_frames")) {
    $observedMaximumRenderRecoverySilenceFrames = ConvertTo-DuplexUnsignedInteger `
        $worker "maximum_render_recovery_silence_frames" $failures
  } elseif ($null -ne $MaximumRenderRecoverySilenceFrames) {
    $failures.Add("missing_maximum_render_recovery_silence_frames")
  }
}
if ($null -ne $engine) {
  $captureFifoOverflowCycles = ConvertTo-DuplexUnsignedInteger $engine "capture_fifo_overflow_cycles" $failures
  $captureFifoOverflowFrames = ConvertTo-DuplexUnsignedInteger $engine "capture_fifo_overflow_frames" $failures
  $renderFifoOverflowCycles = ConvertTo-DuplexUnsignedInteger $engine "render_fifo_overflow_cycles" $failures
  $renderFifoOverflowFrames = ConvertTo-DuplexUnsignedInteger $engine "render_fifo_overflow_frames" $failures
  $renderFifoUnderflowFrames = ConvertTo-DuplexUnsignedInteger $engine "render_fifo_underflow_frames" $failures
}

foreach ($check in @(
    @($runtimeErrorCount, "runtime_error_count_nonzero"),
    @($processErrorCycles, "process_error_cycles_nonzero"),
    @($streamStartErrorCycles, "stream_start_error_cycles_nonzero"),
    @($streamStopErrorCycles, "stream_stop_error_cycles_nonzero"),
    @($renderWaitTimeoutCycles, "render_wait_timeout_cycles_nonzero"),
    @($captureFifoOverflowCycles, "capture_fifo_overflow_cycles_nonzero"),
    @($captureFifoOverflowFrames, "capture_fifo_overflow_frames_nonzero"),
    @($renderFifoOverflowCycles, "render_fifo_overflow_cycles_nonzero"),
    @($renderFifoOverflowFrames, "render_fifo_overflow_frames_nonzero"))) {
  if ($null -ne $check[0] -and $check[0] -ne 0) {
    $failures.Add([string]$check[1])
  }
}

foreach ($check in @(
    @($loopCycles, "loop_cycles_zero"),
    @($graphProcessedCycles, "graph_processed_cycles_zero"),
    @($capturedFrames, "captured_frames_zero"),
    @($renderedFrames, "rendered_frames_zero"))) {
  if ($null -ne $check[0] -and $check[0] -eq 0) {
    $failures.Add([string]$check[1])
  }
}
if ($null -ne $hasCaptureStream -and $hasCaptureStream -ne 1) {
  $failures.Add("capture_stream_required")
}
if ($null -ne $hasRenderStream -and $hasRenderStream -ne 1) {
  $failures.Add("render_stream_required")
}

$targetRenderedFrames = $null
$minimumRenderedFrames = $null
if ($null -ne $renderSampleRate) {
  if ($renderSampleRate -eq 0) {
    $failures.Add("render_sample_rate_zero")
  } else {
    $targetAsBigInteger = ([System.Numerics.BigInteger]$renderSampleRate *
        [System.Numerics.BigInteger]$DurationMs) / 1000
    if ($targetAsBigInteger -gt [long]::MaxValue) {
      $failures.Add("target_rendered_frames_overflow")
    } else {
      $targetRenderedFrames = [long]$targetAsBigInteger
      $scaledTarget = [System.Numerics.BigInteger]$targetRenderedFrames *
                      $MinimumRenderedFrameCoverageBasisPoints
      $minimumRenderedFrames = [long](($scaledTarget + 9999) / 10000)
      if ($null -ne $renderedFrames -and $renderedFrames -lt $minimumRenderedFrames) {
        $failures.Add("rendered_frame_coverage_below_minimum")
      }
    }
  }
}

$unlabeledRenderUnderflowFrames = $null
if ($null -ne $renderFifoUnderflowFrames -and
    $null -ne $renderRecoverySilenceFrames -and
    $null -ne $renderStartupSilenceFrames) {
  if ($renderRecoverySilenceFrames -gt $renderFifoUnderflowFrames -or
      $renderStartupSilenceFrames -gt
          ($renderFifoUnderflowFrames - $renderRecoverySilenceFrames)) {
    $failures.Add("render_underflow_label_frames_exceed_total")
  } else {
    $unlabeledRenderUnderflowFrames = $renderFifoUnderflowFrames -
        $renderRecoverySilenceFrames - $renderStartupSilenceFrames
    if ($unlabeledRenderUnderflowFrames -ne 0) {
      $failures.Add("unlabeled_render_underflow_frames_nonzero")
    }
  }
}

if ($null -ne $MaximumRenderRecoverySilenceFrames -and
    $null -ne $observedMaximumRenderRecoverySilenceFrames -and
    $observedMaximumRenderRecoverySilenceFrames -gt
        $MaximumRenderRecoverySilenceFrames) {
  $failures.Add("maximum_render_recovery_silence_frames_exceeded")
}

$passed = $failures.Count -eq 0
$reason = if ($passed) { "none" } else { [string]::Join(",", $failures) }
Write-Output (
    "wasapi_duplex_acceptance" +
    " passed=$(if ($passed) { 1 } else { 0 })" +
    " process_exit_code=$toolExitCode" +
    " duration_ms=$DurationMs" +
    " minimum_rendered_frame_coverage_basis_points=$MinimumRenderedFrameCoverageBasisPoints" +
    " loop_cycles=$(ConvertTo-DuplexSummaryNumber $loopCycles)" +
    " graph_processed_cycles=$(ConvertTo-DuplexSummaryNumber $graphProcessedCycles)" +
    " captured_frames=$(ConvertTo-DuplexSummaryNumber $capturedFrames)" +
    " render_sample_rate=$(ConvertTo-DuplexSummaryNumber $renderSampleRate)" +
    " target_rendered_frames=$(ConvertTo-DuplexSummaryNumber $targetRenderedFrames)" +
    " minimum_rendered_frames=$(ConvertTo-DuplexSummaryNumber $minimumRenderedFrames)" +
    " rendered_frames=$(ConvertTo-DuplexSummaryNumber $renderedFrames)" +
    " render_fifo_underflow_frames=$(ConvertTo-DuplexSummaryNumber $renderFifoUnderflowFrames)" +
    " render_recovery_silence_frames=$(ConvertTo-DuplexSummaryNumber $renderRecoverySilenceFrames)" +
    " render_startup_silence_frames=$(ConvertTo-DuplexSummaryNumber $renderStartupSilenceFrames)" +
    " unlabeled_render_underflow_frames=$(ConvertTo-DuplexSummaryNumber $unlabeledRenderUnderflowFrames)" +
    " maximum_render_recovery_silence_frames=$(ConvertTo-DuplexSummaryNumber $observedMaximumRenderRecoverySilenceFrames)" +
    " maximum_render_recovery_silence_frames_threshold=$(ConvertTo-DuplexSummaryNumber $MaximumRenderRecoverySilenceFrames)" +
    " reason=`"$reason`"")

if ($passed) { exit 0 }
exit 1
