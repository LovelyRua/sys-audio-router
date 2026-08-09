param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "engineer-a",
  [ValidateRange(9000, 4294967295)]
  [uint32]$DurationMs = 25000,
  [ValidateRange(1000, 60000)]
  [uint32]$RestartDelayMs = 3000,
  [ValidateRange(1, 1000)]
  [uint32]$PollMs = 100,
  [ValidateRange(1, 1000)]
  [uint32]$StreamTimeoutMs = 10,
  [ValidateRange(0, [long]::MaxValue)]
  [long]$MaximumRecoveryDurationMilliseconds = 5000,
  [string]$RemoteExecutablePath = "",
  [string]$EvidenceDirectory = ""
)

$ErrorActionPreference = "Stop"

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot) -or $safeSlot.Length -gt 48) {
  throw "Slot must contain 1-48 safe path characters."
}
$minimumDuration = [uint64]$RestartDelayMs +
    [uint64]$MaximumRecoveryDurationMilliseconds + 1000
if ([uint64]$DurationMs -lt $minimumDuration) {
  throw "DurationMs must cover RestartDelayMs, the recovery threshold, and one final second."
}

$runId = [guid]::NewGuid().ToString("N")
$timestamp = [datetime]::UtcNow.ToString("yyyyMMdd-HHmmss")
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
  $EvidenceDirectory = Join-Path (Join-Path $PSScriptRoot "..\.sar-evidence") `
      "audio-service-recovery-$safeSlot-$timestamp-$runId"
}
$evidencePath = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidencePath -Force | Out-Null
$stdoutPath = Join-Path $evidencePath "stdout.log"
$stderrPath = Join-Path $evidencePath "stderr.log"
$acceptancePath = Join-Path $evidencePath "acceptance.log"
$resultPath = Join-Path $evidencePath "result.json"

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$session = $null
$remoteResult = $null
$acceptanceExitCode = $null
$failure = $null

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $remoteResult = Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $runId, $DurationMs, $RestartDelayMs, $PollMs, `
      $StreamTimeoutMs, $RemoteExecutablePath -ScriptBlock {
    param(
      [string]$SafeSlot,
      [string]$RunId,
      [uint32]$DurationMs,
      [uint32]$RestartDelayMs,
      [uint32]$PollMs,
      [uint32]$StreamTimeoutMs,
      [string]$RequestedExecutablePath
    )

    $ErrorActionPreference = "Stop"
    $root = Join-Path $env:LOCALAPPDATA `
        "SystemAudioRoute\acceptance\audio-service-recovery"
    $runDirectory = Join-Path $root "$SafeSlot\$RunId"
    $lockDirectory = Join-Path $root ".locks"
    New-Item -ItemType Directory -Path $runDirectory, $lockDirectory -Force |
        Out-Null
    $lockPath = Join-Path $lockDirectory "$SafeSlot.lock"
    $lockStream = $null
    $process = $null
    try {
      try {
        $lockStream = [IO.File]::Open(
            $lockPath, [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
      } catch [IO.IOException] {
        throw "Audio-service recovery slot '$SafeSlot' is already active."
      }

      $executablePath = $RequestedExecutablePath
      if ([string]::IsNullOrWhiteSpace($executablePath)) {
        if ($SafeSlot -eq "default") {
          $executablePath = Join-Path $env:USERPROFILE `
              "src\sys-audio-router\build\Debug\sar_measure_wasapi_recovery.exe"
        } else {
          $executablePath = Join-Path $env:USERPROFILE `
              "src\sys-audio-router-$SafeSlot\build-$SafeSlot\Debug\sar_measure_wasapi_recovery.exe"
        }
      }
      $executablePath = [IO.Path]::GetFullPath($executablePath)
      if (!(Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Recovery measurement tool was not found: $executablePath"
      }

      $stdout = Join-Path $runDirectory "stdout.log"
      $stderr = Join-Path $runDirectory "stderr.log"
      $arguments = @(
        "--render-only", "--duration-ms", [string]$DurationMs,
        "--poll-ms", [string]$PollMs,
        "--timeout-ms", [string]$StreamTimeoutMs
      )
      $process = Start-Process -FilePath $executablePath `
          -ArgumentList $arguments -RedirectStandardOutput $stdout `
          -RedirectStandardError $stderr -PassThru -WindowStyle Hidden

      $delay = [Diagnostics.Stopwatch]::StartNew()
      while ($delay.ElapsedMilliseconds -lt $RestartDelayMs) {
        if ($process.HasExited) {
          throw "Recovery measurement exited before the service restart."
        }
        Start-Sleep -Milliseconds 100
      }

      Restart-Service -Name Audiosrv -Force -ErrorAction Stop
      $audioService = Get-Service -Name Audiosrv
      $audioService.WaitForStatus(
          [ServiceProcess.ServiceControllerStatus]::Running,
          [timespan]::FromSeconds(30))

      $waitMs = [int][Math]::Min(
          [int]::MaxValue, [uint64]$DurationMs + 60000)
      if (!$process.WaitForExit($waitMs)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Recovery measurement exceeded its bounded wait."
      }
      $process.Refresh()
      [pscustomobject]@{
        run_id = $RunId
        executable_path = $executablePath
        remote_directory = $runDirectory
        process_exit_code = [int]$process.ExitCode
        audio_service_status = [string](Get-Service -Name Audiosrv).Status
        stdout = @(Get-Content -LiteralPath $stdout)
        stderr = @(Get-Content -LiteralPath $stderr)
      }
    } finally {
      if ($null -ne $process) {
        if (!$process.HasExited) {
          $process.Kill()
          $process.WaitForExit()
        }
        $process.Dispose()
      }
      if ($null -ne $lockStream) {
        $lockStream.Dispose()
      }
    }
  }

  Set-Content -LiteralPath $stdoutPath -Value @($remoteResult.stdout) `
      -Encoding UTF8
  Set-Content -LiteralPath $stderrPath -Value @($remoteResult.stderr) `
      -Encoding UTF8

  $powershellPath = (Get-Process -Id $PID).Path
  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    & $powershellPath -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $PSScriptRoot "windows-wasapi-recovery-acceptance.ps1") `
        -InputPath $stdoutPath `
        -ProcessExitCode ([int]$remoteResult.process_exit_code) `
        -AllowEmptyCaptureDeviceId `
        -MaximumRecoveryDurationMilliseconds `
            $MaximumRecoveryDurationMilliseconds `
        1> $acceptancePath 2>&1
    $acceptanceExitCode = [int]$LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }

  if ([string]$remoteResult.audio_service_status -ne "Running") {
    throw "Windows Audio did not return to Running."
  }
  if ($acceptanceExitCode -ne 0) {
    throw "WASAPI recovery acceptance rejected the measurement."
  }
} catch {
  $failure = $_.Exception.ToString()
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
  [ordered]@{
    schema_version = 1
    run_id = $runId
    slot = $safeSlot
    host = $HostName
    finished_utc = [datetime]::UtcNow.ToString('o')
    passed = [string]::IsNullOrWhiteSpace($failure)
    process_exit_code = if ($null -eq $remoteResult) { $null } else {
      $remoteResult.process_exit_code
    }
    acceptance_exit_code = $acceptanceExitCode
    audio_service_status = if ($null -eq $remoteResult) { $null } else {
      $remoteResult.audio_service_status
    }
    failure = $failure
    stdout_path = $stdoutPath
    stderr_path = $stderrPath
    acceptance_path = $acceptancePath
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding UTF8
}

if (![string]::IsNullOrWhiteSpace($failure)) {
  Write-Error "Audio-service recovery failed. Evidence: '$evidencePath'. $failure"
}
$acceptanceSummary = Get-Content -LiteralPath $acceptancePath -Raw
Write-Output $acceptanceSummary.Trim()
Write-Output (("wasapi_audio_service_recovery passed=1 run_id={0} slot={1} " +
    "service_status={2} evidence={3}") -f $runId, $safeSlot,
    $remoteResult.audio_service_status, $evidencePath)
