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

  [ValidateRange(0, [long]::MaxValue)]
  [long]$MaximumRecoveryDurationMilliseconds = 5000
)

$ErrorActionPreference = "Stop"
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
foreach ($line in $lines) {
  if ($line -match '^wasapi_recovery_supervisor(?:\s|$)') {
    $snapshots.Add((ConvertFrom-RecoveryKeyValueLine -Line $line))
  }
}

$failures = [System.Collections.Generic.List[string]]::new()
if ($toolExitCode -ne 0) {
  $failures.Add("process_exit_code")
}

$finalSnapshot = $null
if ($snapshots.Count -eq 0) {
  $failures.Add("missing_supervisor_summary")
} else {
  $finalIndex = $snapshots.Count - 1
  if ($snapshots[$finalIndex]["state"] -eq "stopped" -and $finalIndex -gt 0) {
    --$finalIndex
  }
  $finalSnapshot = $snapshots[$finalIndex]
}

$finalHealthy = $false
$activeCaptureDeviceId = ""
$activeRenderDeviceId = ""
$successfulRecoveryCount = $null
$maximumRecoveryDurationMs = $null
$notificationResetFailureCount = $null
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
    } elseif ([string]::IsNullOrWhiteSpace([string]$finalSnapshot[$field])) {
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
  if ($null -ne $notificationResetFailureCount -and
      $notificationResetFailureCount -ne 0) {
    $failures.Add("notification_reset_failure")
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
    " active_capture_device_id=$(ConvertTo-SummaryValue $activeCaptureDeviceId)" +
    " active_render_device_id=$(ConvertTo-SummaryValue $activeRenderDeviceId)" +
    " successful_recovery_count=$(& $numberOrMissing $successfulRecoveryCount)" +
    " require_successful_recovery=$(if ($requireSuccessfulRecovery) { 1 } else { 0 })" +
    " maximum_recovery_duration_ms=$(& $numberOrMissing $maximumRecoveryDurationMs)" +
    " maximum_recovery_duration_threshold_ms=$MaximumRecoveryDurationMilliseconds" +
    " notification_reset_failure_count=$(& $numberOrMissing $notificationResetFailureCount)" +
    " reason=$(ConvertTo-SummaryValue $reason)")

if ($passed) { exit 0 }
exit 1
