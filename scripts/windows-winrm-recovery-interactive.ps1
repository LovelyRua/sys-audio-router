param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "recovery-interactive",
  [ValidateRange(1, 4294967295)]
  [uint32]$DurationMs = 30000,
  [string]$CaptureId = "",
  [string]$RenderId = "",
  [string]$RemoteExecutablePath = "",
  [string]$InteractiveUser = "",
  [ValidateRange(0, 86400)]
  [uint32]$WaitTimeoutSeconds = 0
)

$ErrorActionPreference = "Stop"

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}
if ($safeSlot.Length -gt 48) {
  throw "Slot must be at most 48 characters after sanitization."
}
if ($WaitTimeoutSeconds -eq 0) {
  $WaitTimeoutSeconds = [uint32][Math]::Min(
      86400, [Math]::Max(60, [Math]::Ceiling($DurationMs / 1000.0) + 120))
}

$runnerPath = Join-Path $PSScriptRoot "windows-wasapi-recovery-interactive-runner.ps1"
$runnerSource = Get-Content -LiteralPath $runnerPath -Raw
$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$session = $null
$remoteResult = $null

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $remoteResult = Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $DurationMs, $CaptureId, $RenderId, $RemoteExecutablePath, `
      $InteractiveUser, $WaitTimeoutSeconds, $runnerSource -ScriptBlock {
    param(
      [string]$SafeSlot,
      [uint32]$DurationMs,
      [string]$CaptureId,
      [string]$RenderId,
      [string]$RemoteExecutablePath,
      [string]$RequestedInteractiveUser,
      [uint32]$WaitTimeoutSeconds,
      [string]$RunnerSource
    )

    $ErrorActionPreference = "Stop"

    function Get-ShortHash {
      param([Parameter(Mandatory = $true)][string]$Value)

      $sha256 = [Security.Cryptography.SHA256]::Create()
      try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($sha256.ComputeHash($bytes)) `
            -replace '-', '').Substring(0, 12).ToLowerInvariant()
      } finally {
        $sha256.Dispose()
      }
    }

    function Get-InteractiveExplorerUsers {
      $users = foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='explorer.exe'")) {
        $owner = Invoke-CimMethod -InputObject $process -MethodName GetOwner
        if ($owner.ReturnValue -eq 0 -and ![string]::IsNullOrWhiteSpace($owner.User)) {
          [pscustomobject]@{
            UserName = "$($owner.Domain)\$($owner.User)"
            SessionId = [uint32]$process.SessionId
          }
        }
      }
      @($users | Sort-Object UserName, SessionId -Unique)
    }

    function Quote-TaskArgument {
      param([Parameter(Mandatory = $true)][string]$Value)
      return '"' + ($Value -replace '"', '\"') + '"'
    }

    $interactiveSessions = @(Get-InteractiveExplorerUsers)
    if ([string]::IsNullOrWhiteSpace($RequestedInteractiveUser)) {
      $interactiveUsers = @($interactiveSessions.UserName | Sort-Object -Unique)
      if ($interactiveUsers.Count -eq 0) {
        throw "No logged-in interactive Explorer session was found."
      }
      if ($interactiveUsers.Count -ne 1) {
        throw "Multiple interactive users were found ($($interactiveUsers -join ', ')); specify -InteractiveUser."
      }
      $interactiveUser = $interactiveUsers[0]
    } else {
      $requested = $RequestedInteractiveUser
      if ($requested -notmatch '[\\@]') {
        $matchingUsers = @($interactiveSessions.UserName | Where-Object {
          ($_ -split '\\')[-1] -ieq $requested
        } | Sort-Object -Unique)
      } else {
        $matchingUsers = @($interactiveSessions.UserName | Where-Object {
          $_ -ieq $requested
        } | Sort-Object -Unique)
      }
      if ($matchingUsers.Count -ne 1) {
        throw "Interactive user '$requested' does not identify exactly one Explorer session."
      }
      $interactiveUser = $matchingUsers[0]
    }

    $account = [Security.Principal.NTAccount]::new($interactiveUser)
    $sid = $account.Translate([Security.Principal.SecurityIdentifier]).Value
    $profile = Get-CimInstance Win32_UserProfile -Filter "SID='$sid'" |
        Select-Object -First 1
    if ($null -eq $profile -or [string]::IsNullOrWhiteSpace($profile.LocalPath)) {
      throw "The local profile path for '$interactiveUser' could not be resolved."
    }

    if ([string]::IsNullOrWhiteSpace($RemoteExecutablePath)) {
      $RemoteExecutablePath = Join-Path $env:USERPROFILE `
          "src\sys-audio-router-$SafeSlot\build-$SafeSlot\sar_measure_wasapi_recovery.exe"
    }
    $RemoteExecutablePath = [IO.Path]::GetFullPath($RemoteExecutablePath)
    if (!(Test-Path -LiteralPath $RemoteExecutablePath -PathType Leaf)) {
      throw "Recovery executable was not found at '$RemoteExecutablePath'."
    }

    $initiator = "$env:USERDOMAIN\$env:USERNAME"
    $initiatorKey = (($initiator -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_'))
    $initiatorKey = "$initiatorKey-$(Get-ShortHash $initiator)"
    $runId = [guid]::NewGuid().ToString('N')
    $outputDirectory = Join-Path ([string]$profile.LocalPath) `
        "AppData\Local\SystemAudioRoute\interactive-recovery\$initiatorKey\$SafeSlot\$runId"
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

    $runnerRemotePath = Join-Path $outputDirectory "runner.ps1"
    $configPath = Join-Path $outputDirectory "config.json"
    $stdoutPath = Join-Path $outputDirectory "stdout.log"
    $stderrPath = Join-Path $outputDirectory "stderr.log"
    $resultPath = Join-Path $outputDirectory "result.json"
    $RunnerSource | Set-Content -LiteralPath $runnerRemotePath -Encoding UTF8

    $arguments = @("--duration-ms", "$DurationMs")
    if (![string]::IsNullOrWhiteSpace($CaptureId)) {
      $arguments += @("--capture-id", $CaptureId)
    }
    if (![string]::IsNullOrWhiteSpace($RenderId)) {
      $arguments += @("--render-id", $RenderId)
    }
    $config = [ordered]@{
      executable_path = $RemoteExecutablePath
      arguments = $arguments
      stdout_path = $stdoutPath
      stderr_path = $stderrPath
      result_path = $resultPath
    }
    $config | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $configPath -Encoding UTF8

    $taskName = "SAR-Recovery-$(Get-ShortHash $sid)-$SafeSlot-$runId"
    $taskRegistered = $false
    try {
      $powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
      $taskArguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
          (Quote-TaskArgument $runnerRemotePath) + " -ConfigPath " +
          (Quote-TaskArgument $configPath)
      $action = New-ScheduledTaskAction -Execute $powershell -Argument $taskArguments
      $principal = New-ScheduledTaskPrincipal -UserId $interactiveUser `
          -LogonType Interactive -RunLevel Limited
      $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
          -DontStopIfGoingOnBatteries `
          -ExecutionTimeLimit ([TimeSpan]::FromSeconds($WaitTimeoutSeconds))
      Register-ScheduledTask -TaskName $taskName -Action $action `
          -Principal $principal -Settings $settings -Force | Out-Null
      $taskRegistered = $true
      Start-ScheduledTask -TaskName $taskName

      $deadline = [datetime]::UtcNow.AddSeconds($WaitTimeoutSeconds)
      while (!(Test-Path -LiteralPath $resultPath -PathType Leaf)) {
        if ([datetime]::UtcNow -ge $deadline) {
          throw "Timed out after $WaitTimeoutSeconds seconds waiting for interactive task " +
              "'$taskName'. Output directory: '$outputDirectory'."
        }
        $task = Get-ScheduledTask -TaskName $taskName
        $taskInfo = Get-ScheduledTaskInfo -TaskName $taskName
        if ($task.State -eq "Ready" -and $taskInfo.LastRunTime.Year -gt 2000) {
          throw "Interactive task '$taskName' exited before writing result.json " +
              "(Task Scheduler result $($taskInfo.LastTaskResult)). " +
              "Output directory: '$outputDirectory'."
        }
        Start-Sleep -Milliseconds 250
      }

      $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
      [pscustomobject]@{
        InteractiveUser = $interactiveUser
        TaskName = $taskName
        OutputDirectory = $outputDirectory
        Stdout = if (Test-Path -LiteralPath $stdoutPath) {
          Get-Content -LiteralPath $stdoutPath -Raw
        } else { "" }
        Stderr = if (Test-Path -LiteralPath $stderrPath) {
          Get-Content -LiteralPath $stderrPath -Raw
        } else { "" }
        ExitCode = [int]$result.exit_code
        LaunchError = [string]$result.launch_error
      }
    } finally {
      if ($taskRegistered) {
        try {
          Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        } finally {
          Unregister-ScheduledTask -TaskName $taskName -Confirm:$false `
              -ErrorAction SilentlyContinue
        }
      }
    }
  }
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}

Write-Host "Interactive user: $($remoteResult.InteractiveUser)"
Write-Host "Task:             $($remoteResult.TaskName) (removed)"
Write-Host "Remote output:    $($remoteResult.OutputDirectory)"
if (![string]::IsNullOrEmpty($remoteResult.Stdout)) {
  Write-Host "--- stdout ---"
  Write-Host $remoteResult.Stdout.TrimEnd()
}
if (![string]::IsNullOrEmpty($remoteResult.Stderr)) {
  Write-Host "--- stderr ---"
  Write-Host $remoteResult.Stderr.TrimEnd()
}
Write-Host "Exit code:        $($remoteResult.ExitCode)"
if (![string]::IsNullOrWhiteSpace($remoteResult.LaunchError)) {
  Write-Error $remoteResult.LaunchError
}
exit $remoteResult.ExitCode
