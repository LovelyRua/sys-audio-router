[CmdletBinding()]
param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [Parameter(Mandatory = $true)]
  [string]$RemoteBuildPath,
  [string]$CaptureDeviceIdA = "",
  [string]$CaptureDeviceIdB = "",
  [string]$RenderDeviceId = "",
  [string]$Configuration = "",
  [string]$InteractiveUser = "",
  [string]$Slot = "multi-endpoint",
  [ValidateRange(1, 120)]
  [int]$ReadyTimeoutSeconds = 20,
  [ValidateRange(100, 60000)]
  [int]$DiagnosticsDelayMilliseconds = 1500,
  [ValidateRange(100, 30000)]
  [int]$PreflightTimeoutMilliseconds = 2500,
  [string]$EvidenceDirectory = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0
Import-Module (Join-Path $PSScriptRoot "windows-winrm-preflight.psm1") -Force

function Write-JsonFile {
  param([object]$Value, [string]$Path)
  $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Write-BlockedPreflight {
  param([object]$Diagnosis, [string]$EvidencePath)
  Write-JsonFile $Diagnosis (Join-Path $EvidencePath "winrm-preflight.json")
  Write-Output ("multi_endpoint_winrm_preflight status=blocked" +
      " block_code=$($Diagnosis.block_code) host=$($Diagnosis.host) port=$($Diagnosis.port)" +
      " tcp_reachable=$(if ($Diagnosis.tcp_reachable) { 1 } else { 0 })" +
      " evidence=$EvidencePath action=$($Diagnosis.action)")
}

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}
$runId = [Guid]::NewGuid().ToString("N").Substring(0, 12)
$timestamp = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
  $EvidenceDirectory = Join-Path (Join-Path $PSScriptRoot "..\.sar-evidence") `
      "multi-endpoint-$safeSlot-$timestamp-$runId"
}
$evidencePath = [IO.Path]::GetFullPath($EvidenceDirectory)
$null = New-Item -ItemType Directory -Path $evidencePath -Force

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') { $credentialUserName = ".\$credentialUserName" }
$credential = [pscredential]::new($credentialUserName,
    (ConvertTo-SecureString $Password -AsPlainText -Force))
$session = $null
$remoteEvidencePath = ""
$remoteScriptPath = ""
$passed = $false
$failedStep = "winrm-preflight"
$failureReason = "none"
$remoteExitCode = -1

[ordered]@{
  schema_version = 1; run_id = $runId; slot = $safeSlot
  started_utc = [DateTime]::UtcNow.ToString("o"); host = $HostName; port = 5985
  user = $UserName; remote_build_path = $RemoteBuildPath; configuration = $Configuration
  requested_capture_a = $CaptureDeviceIdA; requested_capture_b = $CaptureDeviceIdB
  requested_render = $RenderDeviceId
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidencePath "wrapper-manifest.json") -Encoding UTF8

$tcpReachable = Test-SarTcpPort -HostName $HostName -Port 5985 `
    -TimeoutMilliseconds $PreflightTimeoutMilliseconds
try {
  $null = Test-WSMan -ComputerName $HostName -Authentication Default `
      -Credential $credential -ErrorAction Stop
  [ordered]@{
    status = "ready"; stage = "winrm-preflight"; host = $HostName; port = 5985
    tcp_reachable = $tcpReachable; checked_utc = [DateTime]::UtcNow.ToString("o")
  } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidencePath "winrm-preflight.json") -Encoding UTF8
} catch {
  $diagnosis = Get-SarWinRmFailureDiagnosis -ErrorRecord $_ -HostName $HostName `
      -Port 5985 -TcpReachable $tcpReachable
  Write-BlockedPreflight $diagnosis $evidencePath
  exit 1
}

try {
  $failedStep = "new-pssession"
  try {
    $session = New-PSSession -ComputerName $HostName -Credential $credential -ErrorAction Stop
  } catch {
    $diagnosis = Get-SarWinRmFailureDiagnosis -ErrorRecord $_ -HostName $HostName `
        -Port 5985 -TcpReachable $tcpReachable
    Write-BlockedPreflight $diagnosis $evidencePath
    throw "New-PSSession failed after Test-WSMan: $($diagnosis.summary)"
  }

  $failedStep = "remote-layout"
  $remotePaths = Invoke-Command -Session $session -ArgumentList $safeSlot, $runId `
      -ScriptBlock {
    param([string]$SafeSlot, [string]$RunId)
    $root = Join-Path $env:LOCALAPPDATA "SystemAudioRoute\acceptance\multi-endpoint"
    $run = Join-Path (Join-Path $root "runs") $RunId
    $scripts = Join-Path $root "scripts"
    $locks = Join-Path $root "locks"
    $null = New-Item -ItemType Directory -Path $run, $scripts, $locks -Force
    $owners = @(Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
      ForEach-Object {
        $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwner
        if ($owner.ReturnValue -eq 0) {
          if ([string]::IsNullOrWhiteSpace($owner.Domain)) { $owner.User }
          else { "$($owner.Domain)\$($owner.User)" }
        }
      } | Sort-Object -Unique)
    [pscustomobject]@{
      Evidence = $run
      Script = Join-Path $scripts "windows-multi-endpoint-acceptance-$RunId.ps1"
      Lock = Join-Path $locks "$SafeSlot.lock"
      ExplorerOwners = $owners
      WinRmUser = $env:USERNAME
    }
  }
  $remoteEvidencePath = [string]$remotePaths.Evidence
  $remoteScriptPath = [string]$remotePaths.Script
  $matchingOwners = if ([string]::IsNullOrWhiteSpace($InteractiveUser)) {
    @($remotePaths.ExplorerOwners)
  } else {
    @($remotePaths.ExplorerOwners | Where-Object {
      $_ -eq $InteractiveUser -or $_ -like "*\$InteractiveUser"
    })
  }
  if ($matchingOwners.Count -ne 1) {
    $ownersText = [string]::Join(",", @($remotePaths.ExplorerOwners))
    throw "interactive_user_missing_or_ambiguous: expected one logged-in explorer owner, found '$ownersText'; log in to the audio desktop or pass -InteractiveUser"
  }
  $resolvedInteractiveUser = [string]$matchingOwners[0]
  if (($resolvedInteractiveUser -split '\\')[-1] -ne [string]$remotePaths.WinRmUser) {
    throw "interactive_user_mismatch: WinRM user '$($remotePaths.WinRmUser)' does not own the selected desktop '$resolvedInteractiveUser'; reconnect WinRM as that desktop user"
  }
  [ordered]@{
    status = "ready"; interactive_user = $resolvedInteractiveUser
    winrm_user = [string]$remotePaths.WinRmUser
    explorer_owners = @($remotePaths.ExplorerOwners)
    checked_utc = [DateTime]::UtcNow.ToString("o")
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath `
      (Join-Path $evidencePath "interactive-session.json") -Encoding UTF8
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "windows-multi-endpoint-acceptance.ps1") `
      -Destination $remoteScriptPath -ToSession $session -Force

  $failedStep = "remote-acceptance"
  $remoteResult = Invoke-Command -Session $session -ArgumentList `
      $remoteScriptPath, $remoteEvidencePath, [string]$remotePaths.Lock,
      $RemoteBuildPath, $Configuration, $CaptureDeviceIdA, $CaptureDeviceIdB,
      $RenderDeviceId, $ReadyTimeoutSeconds, $DiagnosticsDelayMilliseconds,
      $resolvedInteractiveUser, $safeSlot, $runId `
      -ScriptBlock {
    param(
      [string]$ScriptPath, [string]$EvidencePath, [string]$LockPath,
      [string]$BuildPath, [string]$BuildConfiguration,
      [string]$CaptureA, [string]$CaptureB, [string]$Render,
      [int]$ReadyTimeout, [int]$DiagnosticsDelay,
      [string]$InteractiveUser, [string]$SafeSlot, [string]$RunId
    )
    $lock = $null
    $taskName = "SAR-$SafeSlot-multi-endpoint-$RunId"
    try {
      try {
        $lock = [IO.File]::Open($LockPath, [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
      } catch {
        return [pscustomobject]@{
          ExitCode = 73
          Lines = @("multi_endpoint_acceptance passed=0 failed_step=slot-lock reason=slot_is_already_running")
        }
      }
      $arguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ScriptPath,
        "-BuildPath", $BuildPath, "-OutputDirectory", $EvidencePath,
        "-ReadyTimeoutSeconds", [string]$ReadyTimeout,
        "-DiagnosticsDelayMilliseconds", [string]$DiagnosticsDelay
      )
      if (![string]::IsNullOrWhiteSpace($BuildConfiguration)) { $arguments += @("-Configuration", $BuildConfiguration) }
      if (![string]::IsNullOrWhiteSpace($CaptureA)) { $arguments += @("-CaptureDeviceIdA", $CaptureA) }
      if (![string]::IsNullOrWhiteSpace($CaptureB)) { $arguments += @("-CaptureDeviceIdB", $CaptureB) }
      if (![string]::IsNullOrWhiteSpace($Render)) { $arguments += @("-RenderDeviceId", $Render) }
      $quotedArguments = @($arguments | ForEach-Object {
        '"' + ([string]$_).Replace('"', '\"') + '"'
      })
      $principal = New-ScheduledTaskPrincipal -UserId $InteractiveUser `
          -LogonType Interactive -RunLevel Limited
      $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
          -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
      $action = New-ScheduledTaskAction -Execute "powershell.exe" `
          -Argument ([string]::Join(" ", $quotedArguments))
      Register-ScheduledTask -TaskName $taskName -Action $action `
          -Principal $principal -Settings $settings | Out-Null
      Start-ScheduledTask -TaskName $taskName
      $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(60, $ReadyTimeout * 3 + 30))
      $taskStarted = $false
      do {
        Start-Sleep -Milliseconds 250
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
        $taskInfo = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction Stop
        $taskStarted = $taskInfo.LastRunTime.Year -gt 2000
      } while ((!$taskStarted -or $task.State -eq "Running") -and
          [DateTime]::UtcNow -lt $deadline)
      if (!$taskStarted -or $task.State -eq "Running") {
        throw "interactive acceptance task did not complete before timeout"
      }
      $exitCode = [int64]$taskInfo.LastTaskResult
      $resultPath = Join-Path $EvidencePath "result.json"
      if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        $lines = @("multi_endpoint_acceptance passed=$(if ($result.passed) { 1 } else { 0 }) failed_step=$($result.failed_step) processed_delta=$($result.processed_block_delta) reason=$($result.reason)")
        if ($result.passed) { $exitCode = 0 } elseif ($exitCode -eq 0) { $exitCode = 1 }
      } else {
        $lines = @("multi_endpoint_acceptance passed=0 failed_step=interactive-task reason=result_json_missing task_result=$exitCode")
        if ($exitCode -eq 0) { $exitCode = 1 }
      }
      [pscustomobject]@{ ExitCode = $exitCode; Lines = $lines }
    } finally {
      Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
      Unregister-ScheduledTask -TaskName $taskName -Confirm:$false `
          -ErrorAction SilentlyContinue
      if ($null -ne $lock) { $lock.Dispose() }
    }
  }
  $remoteExitCode = [int]$remoteResult.ExitCode
  @($remoteResult.Lines) | Set-Content -LiteralPath (Join-Path $evidencePath "remote-console.log") -Encoding UTF8
  if ($remoteExitCode -ne 0) {
    throw "remote acceptance exited with code $remoteExitCode"
  }
  if (@($remoteResult.Lines | Where-Object { $_ -match '^multi_endpoint_acceptance passed=1(?:\s|$)' }).Count -ne 1) {
    throw "remote acceptance did not emit its stable pass record"
  }
  $passed = $true
} catch {
  $failureReason = $_.Exception.Message
} finally {
  if ($null -ne $session) {
    if (![string]::IsNullOrWhiteSpace($remoteEvidencePath)) {
      try {
        Copy-Item -FromSession $session -Path (Join-Path $remoteEvidencePath "*") `
            -Destination $evidencePath -Recurse -Force -ErrorAction Stop
      } catch {
        if ($failureReason -eq "none") { $failureReason = "evidence_copy_failed: $($_.Exception.Message)" }
        $passed = $false
      }
    }
    if (![string]::IsNullOrWhiteSpace($remoteScriptPath)) {
      Invoke-Command -Session $session -ArgumentList $remoteScriptPath -ScriptBlock {
        param([string]$Path)
        Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
      } -ErrorAction SilentlyContinue | Out-Null
    }
    Remove-PSSession $session -ErrorAction SilentlyContinue
  }
}

[ordered]@{
  schema_version = 1; passed = $passed
  failed_step = if ($passed) { "none" } else { $failedStep }
  reason = $failureReason; remote_exit_code = $remoteExitCode
  evidence_directory = $evidencePath; completed_utc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidencePath "wrapper-result.json") -Encoding UTF8
Write-Output ("multi_endpoint_winrm_acceptance passed=$(if ($passed) { 1 } else { 0 })" +
    " failed_step=$(if ($passed) { 'none' } else { $failedStep }) remote_exit_code=$remoteExitCode" +
    " evidence=$evidencePath reason=$failureReason")
if ($passed) { exit 0 }
exit 1
