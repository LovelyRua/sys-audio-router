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

  [switch]$AllowNoSuccessfulRecovery,

  [switch]$AllowEmptyCaptureDeviceId,

  [ValidateRange(0, [long]::MaxValue)]
  [long]$MaximumRecoveryAttemptCount = 3,

  [ValidateRange(0, [long]::MaxValue)]
  [long]$MaximumRecoveryDurationMilliseconds = 5000,

  [Nullable[long]]$MaximumRenderRecoverySilenceFrames = $null
)

$ErrorActionPreference = "Stop"
if ($null -ne $MaximumRenderRecoverySilenceFrames -and
    $MaximumRenderRecoverySilenceFrames -lt 0) {
  throw "MaximumRenderRecoverySilenceFrames must be non-negative."
}
$requireSuccessfulRecovery = !$AllowNoSuccessfulRecovery

function ConvertFrom-RecoveryKeyValueLine {
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

function ConvertTo-RecoveryUnsignedInteger {
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

function ConvertTo-SummaryValue {
  param([string]$Value)
  return '"' + ($Value -replace '([\\"])', '\$1') + '"'
}

$lines = @()
$toolExitCode = $ProcessExitCode
if ($PSCmdlet.ParameterSetName -eq "Run") {
  if (!(Test-Path -LiteralPath $ToolPath -PathType Leaf)) {
    Write-Error "Recovery measurement tool was not found: $ToolPath"
  }
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $lines = @(& $ToolPath @ToolArgument 2>&1 | ForEach-Object { [string]$_ })
    $toolExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
} else {
  if (!(Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    Write-Error "Recovery measurement log was not found: $InputPath"
  }
  $lines = @(Get-Content -LiteralPath $InputPath | ForEach-Object { [string]$_ })
}

$snapshots = [System.Collections.Generic.List[hashtable]]::new()
$lastErrorSummaries = [System.Collections.Generic.List[hashtable]]::new()
foreach ($line in $lines) {
  if ($line -match '^wasapi_recovery_supervisor(?:\s|$)') {
    $snapshots.Add((ConvertFrom-RecoveryKeyValueLine -Line $line))
  } elseif ($line -match '^wasapi_recovery_last_errors(?:\s|$)') {
    $lastErrorSummaries.Add((ConvertFrom-RecoveryKeyValueLine -Line $line))
  }
}

$failures = [System.Collections.Generic.List[string]]::new()
if ($toolExitCode -ne 0) {
  $failures.Add("process_exit_code")
}

$finalSnapshot = $null
$terminalSnapshot = $null
if ($snapshots.Count -eq 0) {
  $failures.Add("missing_supervisor_summary")
} else {
  $finalIndex = $snapshots.Count - 1
  $terminalSnapshot = $snapshots[$finalIndex]
  if ($snapshots[$finalIndex]["state"] -eq "stopped" -and $finalIndex -gt 0) {
    --$finalIndex
  }
  $finalSnapshot = $snapshots[$finalIndex]
}

$finalHealthy = $false
$activeCaptureDeviceId = ""
$activeRenderDeviceId = ""
$successfulRecoveryCount = $null
$failedRecoveryCount = $null
$maximumRecoveryDurationMs = $null
$observedMaximumRenderRecoverySilenceFrames = $null
$notificationResetFailureCount = $null
$finalErrorCount = $null
$lastErrorCount = $null
$maximumEpisodeAttemptCount = $null
if ($null -ne $finalSnapshot) {
  if (!$finalSnapshot.ContainsKey("state")) {
    $failures.Add("missing_state")
  } elseif (!$finalSnapshot.ContainsKey("running")) {
    $failures.Add("missing_running")
  } else {
    $finalHealthy = $finalSnapshot["state"] -eq "running" -and
                    $finalSnapshot["running"] -eq "1"
    if (!$finalHealthy) {
      $failures.Add("final_not_healthy")
    }
  }

  foreach ($field in @("active_capture_device_id", "active_render_device_id")) {
    if (!$finalSnapshot.ContainsKey($field)) {
      $failures.Add("missing_$field")
    } elseif ([string]::IsNullOrWhiteSpace([string]$finalSnapshot[$field]) -and
        !($AllowEmptyCaptureDeviceId -and
          $field -eq "active_capture_device_id")) {
      $failures.Add("empty_$field")
    }
  }
  if ($finalSnapshot.ContainsKey("active_capture_device_id")) {
    $activeCaptureDeviceId = [string]$finalSnapshot["active_capture_device_id"]
  }
  if ($finalSnapshot.ContainsKey("active_render_device_id")) {
    $activeRenderDeviceId = [string]$finalSnapshot["active_render_device_id"]
  }

  $successfulRecoveryCount = ConvertTo-RecoveryUnsignedInteger `
      -Values $finalSnapshot -Name "successful_recovery_count" -Failures $failures
  $maximumRecoveryDurationMs = ConvertTo-RecoveryUnsignedInteger `
      -Values $finalSnapshot -Name "maximum_recovery_duration_ms" -Failures $failures
  if ($null -ne $MaximumRenderRecoverySilenceFrames) {
    $observedMaximumRenderRecoverySilenceFrames = ConvertTo-RecoveryUnsignedInteger `
        -Values $finalSnapshot -Name "maximum_render_recovery_silence_frames" `
        -Failures $failures
  } elseif ($finalSnapshot.ContainsKey("maximum_render_recovery_silence_frames")) {
    $optionalMetricFailures = [System.Collections.Generic.List[string]]::new()
    $observedMaximumRenderRecoverySilenceFrames = ConvertTo-RecoveryUnsignedInteger `
        -Values $finalSnapshot -Name "maximum_render_recovery_silence_frames" `
        -Failures $optionalMetricFailures
  }
  $notificationResetFailureCount = ConvertTo-RecoveryUnsignedInteger `
      -Values $finalSnapshot -Name "notification_reset_failure_count" -Failures $failures
  if ($null -ne $successfulRecoveryCount -and $requireSuccessfulRecovery -and
      $successfulRecoveryCount -lt 1) {
    $failures.Add("successful_recovery_required")
  }
  if ($null -ne $maximumRecoveryDurationMs -and
      $maximumRecoveryDurationMs -gt $MaximumRecoveryDurationMilliseconds) {
    $failures.Add("maximum_recovery_duration_exceeded")
  }
  if ($null -ne $MaximumRenderRecoverySilenceFrames -and
      $null -ne $observedMaximumRenderRecoverySilenceFrames -and
      $observedMaximumRenderRecoverySilenceFrames -gt
          $MaximumRenderRecoverySilenceFrames) {
    $failures.Add("maximum_render_recovery_silence_frames_exceeded")
  }
  if ($null -ne $notificationResetFailureCount -and
      $notificationResetFailureCount -ne 0) {
    $failures.Add("notification_reset_failure")
  }
}

if ($null -ne $terminalSnapshot) {
  $failedRecoveryCount = ConvertTo-RecoveryUnsignedInteger `
      -Values $terminalSnapshot -Name "failed_recovery_count" -Failures $failures
  $finalErrorCount = ConvertTo-RecoveryUnsignedInteger `
      -Values $terminalSnapshot -Name "error_count" -Failures $failures
  if ($null -ne $failedRecoveryCount -and $failedRecoveryCount -ne 0) {
    $failures.Add("failed_recovery_count_nonzero")
  }
  if ($null -ne $finalErrorCount -and $finalErrorCount -ne 0) {
    $failures.Add("error_count_nonzero")
  }
}

if ($lastErrorSummaries.Count -eq 0) {
  $failures.Add("missing_last_errors_summary")
} else {
  $lastErrorCount = ConvertTo-RecoveryUnsignedInteger `
      -Values $lastErrorSummaries[$lastErrorSummaries.Count - 1] `
      -Name "count" -Failures $failures
  if ($null -ne $lastErrorCount -and $lastErrorCount -ne 0) {
    $failures.Add("last_error_count_nonzero")
  }
}

if ($snapshots.Count -gt 0) {
  [long]$previousAttemptCount = 0
  [long]$trackedEpisodeCount = 0
  [long]$episodeAttemptBaseline = 0
  [long]$observedMaximum = 0
  $attemptMetricsValid = $true

  foreach ($snapshot in $snapshots) {
    $metricFailures = [System.Collections.Generic.List[string]]::new()
    $attemptCount = ConvertTo-RecoveryUnsignedInteger `
        -Values $snapshot -Name "attempt_count" -Failures $metricFailures
    $episodeCount = ConvertTo-RecoveryUnsignedInteger `
        -Values $snapshot -Name "recovery_episode_count" -Failures $metricFailures
    foreach ($metricFailure in $metricFailures) {
      $failures.Add($metricFailure)
    }
    if ($null -eq $attemptCount -or $null -eq $episodeCount) {
      $attemptMetricsValid = $false
      continue
    }
    if ($episodeCount -lt $trackedEpisodeCount) {
      $failures.Add("recovery_episode_count_decreased")
      $attemptMetricsValid = $false
      continue
    }

    if ($episodeCount -gt $trackedEpisodeCount) {
      # A successful running snapshot retains its consumed attempt budget. A
      # later episode therefore starts at the previous snapshot's budget unless
      # the stability window reset is visible as a lower attempt count.
      $episodeAttemptBaseline = if ($attemptCount -ge $previousAttemptCount) {
        $previousAttemptCount
      } else {
        0
      }
      $trackedEpisodeCount = $episodeCount
    } elseif ($attemptCount -lt $previousAttemptCount) {
      $episodeAttemptBaseline = 0
    }

    if ($episodeCount -gt 0) {
      $episodeAttemptCount = $attemptCount - $episodeAttemptBaseline
      if ($episodeAttemptCount -lt 0) {
        $episodeAttemptCount = $attemptCount
      }
      $observedMaximum = [Math]::Max($observedMaximum, $episodeAttemptCount)
    }
    $previousAttemptCount = $attemptCount
  }

  if ($attemptMetricsValid) {
    $maximumEpisodeAttemptCount = $observedMaximum
    if ($maximumEpisodeAttemptCount -gt $MaximumRecoveryAttemptCount) {
      $failures.Add("maximum_recovery_attempt_count_exceeded")
    }
  }
}

$passed = $failures.Count -eq 0
$reason = if ($passed) { "none" } else { [string]::Join(",", $failures) }
$numberOrMissing = {
  param($Value)
  if ($null -eq $Value) { return "missing" }
  return [string]$Value
}

Write-Output (
    "wasapi_recovery_acceptance" +
    " passed=$(if ($passed) { 1 } else { 0 })" +
    " process_exit_code=$toolExitCode" +
    " final_healthy=$(if ($finalHealthy) { 1 } else { 0 })" +
    " allow_empty_capture_device_id=$(if ($AllowEmptyCaptureDeviceId) { 1 } else { 0 })" +
    " active_capture_device_id=$(ConvertTo-SummaryValue $activeCaptureDeviceId)" +
    " active_render_device_id=$(ConvertTo-SummaryValue $activeRenderDeviceId)" +
    " successful_recovery_count=$(& $numberOrMissing $successfulRecoveryCount)" +
    " failed_recovery_count=$(& $numberOrMissing $failedRecoveryCount)" +
    " error_count=$(& $numberOrMissing $finalErrorCount)" +
    " last_error_count=$(& $numberOrMissing $lastErrorCount)" +
    " require_successful_recovery=$(if ($requireSuccessfulRecovery) { 1 } else { 0 })" +
    " maximum_recovery_attempt_count=$(& $numberOrMissing $maximumEpisodeAttemptCount)" +
    " maximum_recovery_attempt_count_threshold=$MaximumRecoveryAttemptCount" +
    " maximum_recovery_duration_ms=$(& $numberOrMissing $maximumRecoveryDurationMs)" +
    " maximum_recovery_duration_threshold_ms=$MaximumRecoveryDurationMilliseconds" +
    " maximum_render_recovery_silence_frames=$(& $numberOrMissing $observedMaximumRenderRecoverySilenceFrames)" +
    " maximum_render_recovery_silence_frames_threshold=$(& $numberOrMissing $MaximumRenderRecoverySilenceFrames)" +
    " notification_reset_failure_count=$(& $numberOrMissing $notificationResetFailureCount)" +
    " reason=$(ConvertTo-SummaryValue $reason)")

if ($passed) { exit 0 }
exit 1
