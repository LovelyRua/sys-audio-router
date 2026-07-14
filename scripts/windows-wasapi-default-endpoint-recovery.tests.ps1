$ErrorActionPreference = "Stop"

function Assert-True {
  param([bool]$Condition, [string]$Message)
  if (!$Condition) { throw $Message }
}

function Get-ScriptAst {
  param([string]$Path)

  $tokens = $null
  $parseErrors = $null
  $ast = [System.Management.Automation.Language.Parser]::ParseFile(
      $Path, [ref]$tokens, [ref]$parseErrors)
  Assert-True ($parseErrors.Count -eq 0) `
      "Unexpected parser errors in '$Path': $($parseErrors.Message -join '; ')"
  return $ast
}

$launcherPath = Join-Path $PSScriptRoot "windows-winrm-default-endpoint-recovery.ps1"
$runnerPath = Join-Path $PSScriptRoot "windows-wasapi-default-endpoint-recovery-runner.ps1"
$wrapperPath = Join-Path $PSScriptRoot "windows-winrm-default-endpoint-recovery.cmd"
$launcherAst = Get-ScriptAst -Path $launcherPath
$runnerAst = Get-ScriptAst -Path $runnerPath
$launcherText = Get-Content -LiteralPath $launcherPath -Raw
$runnerText = Get-Content -LiteralPath $runnerPath -Raw
$wrapperText = Get-Content -LiteralPath $wrapperPath -Raw

$runnerCommands = @($runnerAst.FindAll({
  param($node)
  $node -is [System.Management.Automation.Language.CommandAst]
}, $true) | ForEach-Object { $_.GetCommandName() })

foreach ($command in @(
    "Import-Module", "Get-AudioDevice", "Set-AudioDevice", "Start-Process")) {
  Assert-True ($runnerCommands -contains $command) `
      "Runner is missing required command '$command'."
}
Assert-True ($runnerText -match '(?s)finally\s*\{.*restore_to_a') `
    "Endpoint restoration is not protected by a finally block."
Assert-True ($runnerText -match 'WaitForExit\(\$waitMilliseconds\)') `
    "Measurement process wait is not bounded."
Assert-True ($runnerText -match 'result\.json|result_path') `
    "Runner does not publish result metadata."
Assert-True ($launcherText -match '\[guid\]::NewGuid\(\)') `
    "Launcher does not isolate runs with a GUID."
Assert-True ($launcherText -match 'default-endpoint-recovery\\\$initiatorKey\\\$SafeSlot\\\$runId') `
    "Launcher output path is not isolated by initiator, slot, and run GUID."
Assert-True ($launcherText -match '-LogonType Interactive') `
    "Launcher does not run in the selected interactive user session."
Assert-True ($launcherText -notmatch 'arguments\s*=.*--capture-id') `
    "Launcher pins capture instead of measuring follow-default recovery."
Assert-True ($launcherText -notmatch 'arguments\s*=.*--render-id') `
    "Launcher pins render instead of measuring follow-default recovery."
Assert-True ($wrapperText -match 'SAR_TEST_PASSWORD') `
    "Wrapper does not support an ephemeral password environment variable."
Assert-True ($launcherText -notmatch 'password\s*=\s*\$Password') `
    "Launcher appears to persist the plaintext password."

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) `
    ("sar-default-recovery-test-" + [guid]::NewGuid().ToString("N"))
$moduleDirectory = Join-Path $temporaryDirectory "AudioDeviceCmdlets"
$outputDirectory = Join-Path $temporaryDirectory "output"
New-Item -ItemType Directory -Path $moduleDirectory, $outputDirectory | Out-Null
try {
  Set-Content -LiteralPath (Join-Path $moduleDirectory "AudioDeviceCmdlets.psm1") `
      -Encoding ASCII -Value @'
$script:playbackId = "playback-a"
$script:recordingId = "recording-a"
$script:endpoints = @(
  [pscustomobject]@{ Type = "Playback"; Name = "Playback A"; ID = "playback-a" },
  [pscustomobject]@{ Type = "Playback"; Name = "Playback B"; ID = "playback-b" },
  [pscustomobject]@{ Type = "Recording"; Name = "Recording A"; ID = "recording-a" },
  [pscustomobject]@{ Type = "Recording"; Name = "Recording B"; ID = "recording-b" }
)
function Get-AudioDevice {
  param([switch]$List, [switch]$Playback, [switch]$Recording)
  if ($List) { return $script:endpoints }
  $id = if ($Playback) { $script:playbackId } else { $script:recordingId }
  return $script:endpoints | Where-Object { $_.ID -eq $id }
}
function Set-AudioDevice {
  param([string]$ID)
  $endpoint = $script:endpoints | Where-Object { $_.ID -eq $ID }
  if ($null -eq $endpoint) { throw "Unknown fake endpoint '$ID'." }
  if ($endpoint.Type -eq "Playback") { $script:playbackId = $ID }
  else { $script:recordingId = $ID }
  return $endpoint
}
Export-ModuleMember -Function Get-AudioDevice, Set-AudioDevice
'@

  $fakeToolPath = Join-Path $temporaryDirectory "fake-recovery-measure.cmd"
  Set-Content -LiteralPath $fakeToolPath -Encoding ASCII -Value @(
    "@echo off",
    "%SystemRoot%\System32\ping.exe 127.0.0.1 -n 2 >nul",
    'echo wasapi_recovery_supervisor state=running running=1 attempt_count=2 recovery_episode_count=2 successful_recovery_count=2 failed_recovery_count=0 error_count=0 notification_reset_failure_count=0 maximum_recovery_duration_ms=250 active_capture_device_id="recording-a" active_render_device_id="playback-a"',
    'echo wasapi_recovery_supervisor state=stopped running=0 attempt_count=0 recovery_episode_count=2 successful_recovery_count=2 failed_recovery_count=0 error_count=0 notification_reset_failure_count=0 maximum_recovery_duration_ms=250 active_capture_device_id="" active_render_device_id=""',
    'echo wasapi_recovery_last_errors count=0',
    "exit /b 0"
  )
  $configPath = Join-Path $temporaryDirectory "config.json"
  $config = [ordered]@{
    schema_version = 1
    run_id = "test-run"
    slot = "test-slot"
    executable_path = $fakeToolPath
    arguments = @("--duration-ms", "1000")
    target_playback_id = "playback-b"
    target_recording_id = "recording-b"
    switch_delay_ms = 100
    target_hold_ms = 100
    endpoint_wait_timeout_ms = 1000
    process_wait_timeout_ms = 5000
    maximum_recovery_duration_ms = 5000
    stdout_path = Join-Path $outputDirectory "stdout.log"
    stderr_path = Join-Path $outputDirectory "stderr.log"
    switch_path = Join-Path $outputDirectory "switch.json"
    acceptance_script_path = Join-Path $PSScriptRoot "windows-wasapi-recovery-acceptance.ps1"
    acceptance_log_path = Join-Path $outputDirectory "acceptance.log"
    result_path = Join-Path $outputDirectory "result.json"
  }
  $config | ConvertTo-Json -Depth 4 |
      Set-Content -LiteralPath $configPath -Encoding UTF8

  $powershellPath = Join-Path $PSHOME "powershell.exe"
  if (!(Test-Path -LiteralPath $powershellPath -PathType Leaf)) {
    $powershellPath = (Get-Process -Id $PID).Path
  }
  $oldModulePath = $env:PSModulePath
  $env:PSModulePath = "$temporaryDirectory;$oldModulePath"
  try {
    $pathKeys = @([Environment]::GetEnvironmentVariables().Keys | Where-Object {
      [string]$_ -ieq "Path"
    })
    if ($pathKeys.Count -gt 1 -and $pathKeys -contains "PATH") {
      [Environment]::SetEnvironmentVariable("PATH", $null, "Process")
    }
    & $powershellPath -NoProfile -ExecutionPolicy Bypass -File $runnerPath `
        -ConfigPath $configPath
    $runnerExitCode = $LASTEXITCODE
  } finally {
    $env:PSModulePath = $oldModulePath
  }

  $switchRecord = Get-Content -LiteralPath $config.switch_path -Raw |
      ConvertFrom-Json
  $resultRecord = Get-Content -LiteralPath $config.result_path -Raw |
      ConvertFrom-Json
  $failureDetail = $resultRecord | ConvertTo-Json -Compress
  Assert-True ($runnerExitCode -eq 0) `
      "Fake A-to-B-to-A experiment failed with exit code $runnerExitCode. $failureDetail"
  Assert-True ([bool]$switchRecord.restoration_succeeded) `
      "Fake experiment did not restore both original endpoints."
  Assert-True ($switchRecord.events.Count -eq 4) `
      "Fake experiment did not record both B switches and both A restorations."
  Assert-True ($switchRecord.events[2].requested_id -eq "recording-a") `
      "Recording A restoration was not recorded."
  Assert-True ($switchRecord.events[3].requested_id -eq "playback-a") `
      "Playback A restoration was not recorded."
  Assert-True ($resultRecord.process_exit_code -eq 0 -and
      $resultRecord.acceptance_exit_code -eq 0 -and $resultRecord.exit_code -eq 0) `
      "Fake experiment result metadata did not pass process and acceptance gates."
} finally {
  Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
}

Write-Output "Default-endpoint recovery script self-check passed"
