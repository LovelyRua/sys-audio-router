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
  Assert-Equal $ExpectedExitCode $result.ExitCode "Unexpected exit code for '$Name'."
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

$healthy = 'wasapi_recovery_supervisor elapsed_ms=1200 state=running running=1 successful_recovery_count=1 notification_reset_failure_count=0 maximum_recovery_duration_ms=4999 active_capture_device_id="capture\\endpoint" active_render_device_id="render\"endpoint"'
$stopped = 'wasapi_recovery_supervisor elapsed_ms=1201 state=stopped running=0 successful_recovery_count=1 notification_reset_failure_count=0 maximum_recovery_duration_ms=4999 active_capture_device_id="capture\\endpoint" active_render_device_id="render\"endpoint"'

try {
  $fakeToolPath = Join-Path $temporaryDirectory "fake-recovery-measure.cmd"
  Set-Content -LiteralPath $fakeToolPath -Encoding ASCII -Value @(
    "@echo off",
    "echo $healthy",
    "echo $stopped",
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

  Assert-Case -Name "pass" -Lines @($healthy, $stopped) -ExpectedExitCode 0 `
      -ExpectedText 'active_capture_device_id="capture\\endpoint" active_render_device_id="render\"endpoint"'

  $noRecovery = $healthy -replace 'successful_recovery_count=1', 'successful_recovery_count=0'
  Assert-Case -Name "optional-recovery" -Lines @($noRecovery, $stopped) `
      -ExpectedExitCode 0 -ExpectedText 'require_successful_recovery=0' `
      -ExtraArgument @("-AllowNoSuccessfulRecovery")

  Assert-Case -Name "process-failure" -Lines @($healthy, $stopped) `
      -ProcessExitCode 7 -ExpectedExitCode 1 -ExpectedText 'reason="process_exit_code"'

  $faulted = $healthy -replace 'state=running running=1', 'state=faulted running=0'
  Assert-Case -Name "unhealthy" -Lines @($faulted, $stopped) -ExpectedExitCode 1 `
      -ExpectedText 'reason="final_not_healthy"'

  $emptyId = $healthy.Replace(
      'active_capture_device_id="capture\\endpoint"',
      'active_capture_device_id=""')
  Assert-Case -Name "empty-id" -Lines @($emptyId, $stopped) -ExpectedExitCode 1 `
      -ExpectedText 'reason="empty_active_capture_device_id"'

  Assert-Case -Name "recovery-required" -Lines @($noRecovery, $stopped) `
      -ExpectedExitCode 1 -ExpectedText 'reason="successful_recovery_required"'

  $tooSlow = $healthy -replace 'maximum_recovery_duration_ms=4999', 'maximum_recovery_duration_ms=5001'
  Assert-Case -Name "too-slow" -Lines @($tooSlow, $stopped) -ExpectedExitCode 1 `
      -ExpectedText 'reason="maximum_recovery_duration_exceeded"'

  $resetFailure = $healthy -replace 'notification_reset_failure_count=0', 'notification_reset_failure_count=1'
  Assert-Case -Name "reset-failure" -Lines @($resetFailure, $stopped) `
      -ExpectedExitCode 1 -ExpectedText 'reason="notification_reset_failure"'

  $missingMetric = $healthy -replace ' maximum_recovery_duration_ms=4999', ''
  Assert-Case -Name "missing-field" -Lines @($missingMetric, $stopped) `
      -ExpectedExitCode 1 -ExpectedText 'reason="missing_maximum_recovery_duration_ms"'

  Assert-Case -Name "missing-summary" -Lines @("unrelated output") -ExpectedExitCode 1 `
      -ExpectedText 'reason="missing_supervisor_summary"'
} finally {
  Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
}

Write-Output "WASAPI recovery acceptance tests passed"
