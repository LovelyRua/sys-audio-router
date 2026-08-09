param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath
)

$ErrorActionPreference = "Stop"

$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$resultPath = [string]$config.result_path
$temporaryResultPath = "$resultPath.tmp"
$switchPath = [string]$config.switch_path
$temporarySwitchPath = "$switchPath.tmp"
$startedUtc = [datetime]::UtcNow
$events = [System.Collections.Generic.List[object]]::new()
$restoreErrors = [System.Collections.Generic.List[string]]::new()
$failure = $null
$process = $null
$processExitCode = $null
$acceptanceExitCode = $null
$originalPlayback = $null
$originalRecording = $null
$targetPlayback = $null
$targetRecording = $null
$restorationAttempted = $false
$restorationSucceeded = $false

function Add-SwitchEvent {
  param(
    [string]$Phase,
    [string]$Direction,
    [string]$RequestedId,
    [string]$ObservedId,
    [bool]$Succeeded,
    [string]$ErrorMessage = ""
  )

  $events.Add([pscustomobject][ordered]@{
    utc = [datetime]::UtcNow.ToString('o')
    phase = $Phase
    direction = $Direction
    requested_id = $RequestedId
    observed_id = $ObservedId
    succeeded = $Succeeded
    error = $ErrorMessage
  })
}

function Get-DefaultEndpoint {
  param([ValidateSet("Playback", "Recording")][string]$Direction)

  if ($Direction -eq "Playback") {
    return Get-AudioDevice -Playback
  }
  return Get-AudioDevice -Recording
}

function Wait-DefaultEndpoint {
  param(
    [ValidateSet("Playback", "Recording")][string]$Direction,
    [string]$ExpectedId,
    [uint32]$TimeoutMs
  )

  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  do {
    $observed = Get-DefaultEndpoint -Direction $Direction
    if ([string]$observed.ID -eq $ExpectedId) {
      return $observed
    }
    Start-Sleep -Milliseconds 100
  } while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs)
  throw "Timed out after $TimeoutMs ms waiting for the default $Direction endpoint '$ExpectedId'."
}

function Set-DefaultEndpoint {
  param(
    [string]$Phase,
    [ValidateSet("Playback", "Recording")][string]$Direction,
    [string]$EndpointId,
    [uint32]$TimeoutMs
  )

  try {
    Set-AudioDevice -ID $EndpointId | Out-Null
    $observed = Wait-DefaultEndpoint -Direction $Direction `
        -ExpectedId $EndpointId -TimeoutMs $TimeoutMs
    Add-SwitchEvent -Phase $Phase -Direction $Direction `
        -RequestedId $EndpointId -ObservedId ([string]$observed.ID) -Succeeded $true
    return $observed
  } catch {
    $setFailure = $_.Exception.ToString()
    $observedId = ""
    try {
      $observedId = [string](Get-DefaultEndpoint -Direction $Direction).ID
    } catch {
      $observedId = "unavailable"
    }
    Add-SwitchEvent -Phase $Phase -Direction $Direction `
        -RequestedId $EndpointId -ObservedId $observedId -Succeeded $false `
        -ErrorMessage $setFailure
    throw $setFailure
  }
}

function Wait-ExperimentDelay {
  param(
    [Diagnostics.Process]$Process,
    [uint32]$DelayMs,
    [string]$Phase
  )

  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  while ($stopwatch.ElapsedMilliseconds -lt $DelayMs) {
    if ($Process.HasExited) {
      throw "Recovery measurement exited during $Phase with code $($Process.ExitCode)."
    }
    $remaining = $DelayMs - $stopwatch.ElapsedMilliseconds
    Start-Sleep -Milliseconds ([int][Math]::Min(100, [Math]::Max(1, $remaining)))
  }
}

function Find-EndpointById {
  param(
    [object[]]$Endpoints,
    [string]$EndpointId,
    [ValidateSet("Playback", "Recording")][string]$Direction
  )

  $matches = @($Endpoints | Where-Object {
    [string]$_.ID -eq $EndpointId -and [string]$_.Type -eq $Direction
  })
  if ($matches.Count -ne 1) {
    throw "Endpoint '$EndpointId' did not identify exactly one $Direction device."
  }
  return $matches[0]
}

try {
  Import-Module AudioDeviceCmdlets -ErrorAction Stop
  $allEndpoints = @(Get-AudioDevice -List)
  $originalPlayback = Get-DefaultEndpoint -Direction Playback
  $originalRecording = Get-DefaultEndpoint -Direction Recording
  $targetPlayback = Find-EndpointById -Endpoints $allEndpoints `
      -EndpointId ([string]$config.target_playback_id) -Direction Playback
  $targetRecording = Find-EndpointById -Endpoints $allEndpoints `
      -EndpointId ([string]$config.target_recording_id) -Direction Recording
  if ([string]$originalPlayback.ID -eq [string]$targetPlayback.ID) {
    throw "Target Playback endpoint is already the default; A-to-B requires a different endpoint."
  }
  if ([string]$originalRecording.ID -eq [string]$targetRecording.ID) {
    throw "Target Recording endpoint is already the default; A-to-B requires a different endpoint."
  }

  $arguments = [string[]]$config.arguments
  $process = Start-Process -FilePath ([string]$config.executable_path) `
      -ArgumentList $arguments -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput ([string]$config.stdout_path) `
      -RedirectStandardError ([string]$config.stderr_path)
  Wait-ExperimentDelay -Process $process -DelayMs ([uint32]$config.switch_delay_ms) `
      -Phase "pre-switch delay"
  Set-DefaultEndpoint -Phase "switch_to_b" -Direction Playback `
      -EndpointId ([string]$targetPlayback.ID) `
      -TimeoutMs ([uint32]$config.endpoint_wait_timeout_ms) | Out-Null
  Set-DefaultEndpoint -Phase "switch_to_b" -Direction Recording `
      -EndpointId ([string]$targetRecording.ID) `
      -TimeoutMs ([uint32]$config.endpoint_wait_timeout_ms) | Out-Null
  Wait-ExperimentDelay -Process $process -DelayMs ([uint32]$config.target_hold_ms) `
      -Phase "target hold"
} catch {
  $failure = $_.Exception.ToString()
} finally {
  $restorationAttempted = $null -ne $originalPlayback -or $null -ne $originalRecording
  if ($null -ne $originalRecording) {
    try {
      Set-DefaultEndpoint -Phase "restore_to_a" -Direction Recording `
          -EndpointId ([string]$originalRecording.ID) `
          -TimeoutMs ([uint32]$config.endpoint_wait_timeout_ms) | Out-Null
    } catch {
      $restoreErrors.Add($_.Exception.ToString())
    }
  }
  if ($null -ne $originalPlayback) {
    try {
      Set-DefaultEndpoint -Phase "restore_to_a" -Direction Playback `
          -EndpointId ([string]$originalPlayback.ID) `
          -TimeoutMs ([uint32]$config.endpoint_wait_timeout_ms) | Out-Null
    } catch {
      $restoreErrors.Add($_.Exception.ToString())
    }
  }
  $restorationSucceeded = $restorationAttempted -and $restoreErrors.Count -eq 0
}

try {
  if ($null -ne $process) {
    $waitMilliseconds = [int][Math]::Min(
        [int]::MaxValue, [uint64]$config.process_wait_timeout_ms)
    if (!$process.WaitForExit($waitMilliseconds)) {
      try { $process.Kill() } finally { $process.WaitForExit() }
      throw "Recovery measurement exceeded its bounded wait of $waitMilliseconds ms."
    }
    $processExitCode = [int]$process.ExitCode
  }

  if ($null -ne $processExitCode -and
      (Test-Path -LiteralPath ([string]$config.stdout_path) -PathType Leaf)) {
    $powershell = (Get-Process -Id $PID).Path
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
      & $powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
          -File ([string]$config.acceptance_script_path) `
          -InputPath ([string]$config.stdout_path) `
          -ProcessExitCode $processExitCode `
          -MaximumRecoveryDurationMilliseconds `
              ([long]$config.maximum_recovery_duration_ms) `
          1> ([string]$config.acceptance_log_path) 2>&1
      $acceptanceExitCode = [int]$LASTEXITCODE
    } finally {
      $ErrorActionPreference = $previousErrorActionPreference
    }
  }
} catch {
  if ([string]::IsNullOrWhiteSpace($failure)) {
    $failure = $_.Exception.ToString()
  } else {
    $failure += "`n" + $_.Exception.ToString()
  }
} finally {
  if ($null -ne $process) {
    $process.Dispose()
  }

  $switchRecord = [ordered]@{
    schema_version = 1
    run_id = [string]$config.run_id
    slot = [string]$config.slot
    started_utc = $startedUtc.ToString('o')
    finished_utc = [datetime]::UtcNow.ToString('o')
    original_playback = if ($null -eq $originalPlayback) { $null } else {
      [ordered]@{ id = [string]$originalPlayback.ID; name = [string]$originalPlayback.Name }
    }
    original_recording = if ($null -eq $originalRecording) { $null } else {
      [ordered]@{ id = [string]$originalRecording.ID; name = [string]$originalRecording.Name }
    }
    target_playback = if ($null -eq $targetPlayback) { $null } else {
      [ordered]@{ id = [string]$targetPlayback.ID; name = [string]$targetPlayback.Name }
    }
    target_recording = if ($null -eq $targetRecording) { $null } else {
      [ordered]@{ id = [string]$targetRecording.ID; name = [string]$targetRecording.Name }
    }
    restoration_attempted = $restorationAttempted
    restoration_succeeded = $restorationSucceeded
    restoration_errors = @($restoreErrors)
    events = @($events)
  }
  $switchRecord | ConvertTo-Json -Depth 6 |
      Set-Content -LiteralPath $temporarySwitchPath -Encoding UTF8
  Move-Item -LiteralPath $temporarySwitchPath -Destination $switchPath -Force

  $passed = [string]::IsNullOrWhiteSpace($failure) -and
      $restorationSucceeded -and $processExitCode -eq 0 -and
      $acceptanceExitCode -eq 0
  $result = [ordered]@{
    schema_version = 1
    run_id = [string]$config.run_id
    slot = [string]$config.slot
    finished_utc = [datetime]::UtcNow.ToString('o')
    exit_code = if ($passed) { 0 } else { 1 }
    process_exit_code = $processExitCode
    acceptance_exit_code = $acceptanceExitCode
    restoration_succeeded = $restorationSucceeded
    failure = $failure
    stdout_path = [string]$config.stdout_path
    stderr_path = [string]$config.stderr_path
    switch_path = $switchPath
    acceptance_log_path = [string]$config.acceptance_log_path
  }
  $result | ConvertTo-Json -Depth 4 |
      Set-Content -LiteralPath $temporaryResultPath -Encoding UTF8
  Move-Item -LiteralPath $temporaryResultPath -Destination $resultPath -Force
}

exit $(if ($passed) { 0 } else { 1 })
