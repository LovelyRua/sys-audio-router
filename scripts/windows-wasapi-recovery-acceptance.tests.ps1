$ErrorActionPreference = "Stop"

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
}

function Invoke-AcceptanceCase {
  param(
    [string]$Name,
    [string[]]$Lines,
    [int]$ProcessExitCode = 0,
    [string[]]$ExtraArgument = @()
  )

  $logPath = Join-Path $temporaryDirectory "$Name.log"
  Set-Content -LiteralPath $logPath -Value $Lines -Encoding ASCII
  $arguments = @(
    "-NoProfile",
    "-File", (Join-Path $PSScriptRoot "windows-wasapi-recovery-acceptance.ps1"),
    "-InputPath", $logPath,
    "-ProcessExitCode", $ProcessExitCode
  ) + $ExtraArgument
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = @(& $powershellPath @arguments 2>&1 | ForEach-Object { [string]$_ })
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  return [pscustomobject]@{
    ExitCode = $LASTEXITCODE
    Text = [string]::Join("`n", $output)
  }
}

function Assert-Case {
  param(
    [string]$Name,
    [string[]]$Lines,
    [int]$ExpectedExitCode,
    [string]$ExpectedText,
    [int]$ProcessExitCode = 0,
    [string[]]$ExtraArgument = @()
  )

  $result = Invoke-AcceptanceCase -Name $Name -Lines $Lines `
      -ProcessExitCode $ProcessExitCode -ExtraArgument $ExtraArgument
  Assert-Equal $ExpectedExitCode $result.ExitCode `
      "Unexpected exit code for '$Name'. Output: $($result.Text)"
  Assert-Equal $true $result.Text.Contains($ExpectedText) "Unexpected summary for '$Name'."
  Assert-Equal $true $result.Text.Contains("wasapi_recovery_acceptance passed=$([int]($ExpectedExitCode -eq 0))") `
      "Missing machine-readable summary for '$Name'."
}

$tokens = $null
$parseErrors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot "windows-wasapi-recovery-acceptance.ps1"),
    [ref]$tokens,
    [ref]$parseErrors)
Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in acceptance script."

$powershellPath = Join-Path $PSHOME "powershell.exe"
if (!(Test-Path -LiteralPath $powershellPath -PathType Leaf)) {
  $powershellPath = (Get-Process -Id $PID).Path
}
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("sar-recovery-acceptance-" + [guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $temporaryDirectory)

$initial = 'wasapi_recovery_supervisor elapsed_ms=0 state=running running=1 attempt_count=0 recovery_episode_count=0 successful_recovery_count=0 failed_recovery_count=0 notification_reset_failure_count=0 maximum_recovery_duration_ms=0 maximum_render_recovery_silence_frames=0 error_count=0 active_capture_device_id="capture\\endpoint" active_render_device_id="render\"endpoint"'
$recovering = 'wasapi_recovery_supervisor elapsed_ms=500 state=opening running=0 attempt_count=2 recovery_episode_count=1 successful_recovery_count=0 failed_recovery_count=0 notification_reset_failure_count=0 maximum_recovery_duration_ms=0 maximum_render_recovery_silence_frames=64 error_count=1 active_capture_device_id="" active_render_device_id=""'
$healthy = 'wasapi_recovery_supervisor elapsed_ms=1200 state=running running=1 attempt_count=2 recovery_episode_count=1 successful_recovery_count=1 failed_recovery_count=0 notification_reset_failure_count=0 maximum_recovery_duration_ms=4999 maximum_render_recovery_silence_frames=64 error_count=0 active_capture_device_id="capture\\endpoint" active_render_device_id="render\"endpoint"'
$stopped = 'wasapi_recovery_supervisor elapsed_ms=1201 state=stopped running=0 attempt_count=0 recovery_episode_count=1 successful_recovery_count=1 failed_recovery_count=0 notification_reset_failure_count=0 maximum_recovery_duration_ms=4999 maximum_render_recovery_silence_frames=64 error_count=0 active_capture_device_id="capture\\endpoint" active_render_device_id="render\"endpoint"'
$noLastErrors = 'wasapi_recovery_last_errors count=0'

try {
  $fakeToolPath = Join-Path $temporaryDirectory "fake-recovery-measure.cmd"
  Set-Content -LiteralPath $fakeToolPath -Encoding ASCII -Value @(
    "@echo off",
    "echo $initial",
    "echo $recovering",
    "echo $healthy",
    "echo $stopped",
    "echo $noLastErrors",
    "exit /b 0"
  )
  $runArguments = @(
    "-NoProfile",
    "-File", (Join-Path $PSScriptRoot "windows-wasapi-recovery-acceptance.ps1"),
    "-ToolPath", $fakeToolPath
  )
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $runOutput = @(& $powershellPath @runArguments 2>&1 | ForEach-Object { [string]$_ })
    $runExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  $runText = [string]::Join("`n", $runOutput)
  Assert-Equal 0 $runExitCode "Direct tool invocation should pass."
  Assert-Equal $true $runText.Contains("passed=1 process_exit_code=0") `
      "Direct tool invocation did not capture the process exit code."

  Assert-Case -Name "pass" -Lines @($initial, $recovering, $healthy, $stopped, $noLastErrors) `
      -ExpectedExitCode 0 `
      -ExpectedText 'failed_recovery_count=0 error_count=0 last_error_count=0'
  $passResult = Invoke-AcceptanceCase -Name "pass-summary" `
      -Lines @($initial, $recovering, $healthy, $stopped, $noLastErrors)
  Assert-Equal $true $passResult.Text.Contains(
      'maximum_recovery_attempt_count=2 maximum_recovery_attempt_count_threshold=3') `
      "Machine summary did not report recovery attempt metrics."

  $noRecovery = $healthy -replace 'successful_recovery_count=1', 'successful_recovery_count=0'
  Assert-Case -Name "optional-recovery" -Lines @($initial, $noRecovery, $stopped, $noLastErrors) `
      -ExpectedExitCode 0 -ExpectedText 'require_successful_recovery=0' `
      -ExtraArgument @("-AllowNoSuccessfulRecovery")

  Assert-Case -Name "process-failure" -Lines @($healthy, $stopped, $noLastErrors) `
      -ProcessExitCode 7 -ExpectedExitCode 1 -ExpectedText 'reason="process_exit_code"'

  $faulted = $healthy -replace 'state=running running=1', 'state=faulted running=0'
  Assert-Case -Name "unhealthy" -Lines @($faulted, $stopped, $noLastErrors) -ExpectedExitCode 1 `
      -ExpectedText 'reason="final_not_healthy"'

  $emptyId = $healthy.Replace(
      'active_capture_device_id="capture\\endpoint"',
      'active_capture_device_id=""')
  Assert-Case -Name "empty-id" -Lines @($emptyId, $stopped, $noLastErrors) -ExpectedExitCode 1 `
      -ExpectedText 'reason="empty_active_capture_device_id"'

  Assert-Case -Name "recovery-required" -Lines @($noRecovery, $stopped, $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="successful_recovery_required"'

  $tooSlow = $healthy -replace 'maximum_recovery_duration_ms=4999', 'maximum_recovery_duration_ms=5001'
  Assert-Case -Name "too-slow" -Lines @($tooSlow, $stopped, $noLastErrors) -ExpectedExitCode 1 `
      -ExpectedText 'reason="maximum_recovery_duration_exceeded"'

  Assert-Case -Name "recovery-silence-limit" `
      -Lines @($initial, $recovering, $healthy, $stopped, $noLastErrors) `
      -ExpectedExitCode 0 `
      -ExpectedText 'maximum_render_recovery_silence_frames_threshold=64' `
      -ExtraArgument @("-MaximumRenderRecoverySilenceFrames", "64")
  $tooMuchRecoverySilence = $healthy -replace `
      'maximum_render_recovery_silence_frames=64', `
      'maximum_render_recovery_silence_frames=65'
  Assert-Case -Name "recovery-silence-limit-exceeded" `
      -Lines @($initial, $recovering, $tooMuchRecoverySilence, $stopped, $noLastErrors) `
      -ExpectedExitCode 1 `
      -ExpectedText 'reason="maximum_render_recovery_silence_frames_exceeded"' `
      -ExtraArgument @("-MaximumRenderRecoverySilenceFrames", "64")
  $legacyRecoverySilence = $healthy -replace `
      ' maximum_render_recovery_silence_frames=64', ''
  Assert-Case -Name "legacy-recovery-silence" `
      -Lines @($initial, $recovering, $legacyRecoverySilence, $stopped, $noLastErrors) `
      -ExpectedExitCode 0 `
      -ExpectedText 'maximum_render_recovery_silence_frames=missing'
  Assert-Case -Name "missing-recovery-silence-with-gate" `
      -Lines @($initial, $recovering, $legacyRecoverySilence, $stopped, $noLastErrors) `
      -ExpectedExitCode 1 `
      -ExpectedText 'reason="missing_maximum_render_recovery_silence_frames"' `
      -ExtraArgument @("-MaximumRenderRecoverySilenceFrames", "64")

  $resetFailure = $healthy -replace 'notification_reset_failure_count=0', 'notification_reset_failure_count=1'
  Assert-Case -Name "reset-failure" -Lines @($resetFailure, $stopped, $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="notification_reset_failure"'

  $failedRecovery = $stopped -replace 'failed_recovery_count=0', 'failed_recovery_count=1'
  Assert-Case -Name "failed-recovery" -Lines @($healthy, $failedRecovery, $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="failed_recovery_count_nonzero"'

  $finalErrors = $stopped -replace 'error_count=0', 'error_count=2'
  Assert-Case -Name "final-errors" -Lines @($healthy, $finalErrors, $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="error_count_nonzero"'

  Assert-Case -Name "last-errors" -Lines @($healthy, $stopped, 'wasapi_recovery_last_errors count=1') `
      -ExpectedExitCode 1 -ExpectedText 'reason="last_error_count_nonzero"'

  Assert-Case -Name "missing-last-errors" -Lines @($healthy, $stopped) `
      -ExpectedExitCode 1 -ExpectedText 'reason="missing_last_errors_summary"'

  $fourAttempts = $healthy -replace 'attempt_count=2', 'attempt_count=4'
  Assert-Case -Name "attempt-limit" -Lines @($initial, $fourAttempts, $stopped, $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="maximum_recovery_attempt_count_exceeded"'
  Assert-Case -Name "custom-attempt-limit" `
      -Lines @($initial, $fourAttempts, $stopped, $noLastErrors) `
      -ExpectedExitCode 0 -ExpectedText 'maximum_recovery_attempt_count_threshold=4' `
      -ExtraArgument @("-MaximumRecoveryAttemptCount", "4")

  $secondEpisode = $healthy -replace 'elapsed_ms=1200', 'elapsed_ms=1500' `
      -replace 'attempt_count=2', 'attempt_count=3' `
      -replace 'recovery_episode_count=1', 'recovery_episode_count=2' `
      -replace 'successful_recovery_count=1', 'successful_recovery_count=2'
  $secondStopped = $stopped -replace 'recovery_episode_count=1', 'recovery_episode_count=2' `
      -replace 'successful_recovery_count=1', 'successful_recovery_count=2'
  Assert-Case -Name "running-attempt-budget" `
      -Lines @($initial, $healthy, $secondEpisode, $secondStopped, $noLastErrors) `
      -ExpectedExitCode 0 -ExpectedText 'maximum_recovery_attempt_count=2'

  $missingMetric = $healthy -replace ' maximum_recovery_duration_ms=4999', ''
  Assert-Case -Name "missing-field" -Lines @($missingMetric, $stopped, $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="missing_maximum_recovery_duration_ms"'

  Assert-Case -Name "missing-summary" -Lines @("unrelated output", $noLastErrors) `
      -ExpectedExitCode 1 -ExpectedText 'reason="missing_supervisor_summary"'
} finally {
  Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
}

Write-Output "WASAPI recovery acceptance tests passed"
