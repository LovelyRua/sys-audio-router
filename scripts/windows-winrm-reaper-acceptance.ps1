param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [Parameter(Mandatory = $true)]
  [string]$RemoteBuildPath,
  [Parameter(Mandatory = $true)]
  [string]$RenderDeviceId,
  [string]$ReaperPath = "C:\Program Files\REAPER (x64)\reaper.exe",
  [string]$InteractiveUser = "",
  [string]$Slot = "reaper",
  [ValidateRange(1, 8)]
  [uint32]$ClientCount = 1,
  [ValidateRange(1, 86400)]
  [uint32]$DurationSeconds = 3,
  [ValidateRange(1, 10000)]
  [uint32]$MinimumCallbackCoverageBasisPoints = 8000,
  [ValidateRange(1, 1000000)]
  [double]$MaximumCallbackMicroseconds = 10000,
  [ValidateRange(5, 120)]
  [uint32]$StartupTimeoutSeconds = 30,
  [string]$EvidenceDirectory = "",
  [switch]$RecoverUntrackedProcesses
)

$ErrorActionPreference = "Stop"

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}

$runId = [guid]::NewGuid().ToString("N").Substring(0, 12)
$runTimestamp = [datetime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
  $EvidenceDirectory = Join-Path (Join-Path $PSScriptRoot "..\.sar-evidence") `
      "reaper-$safeSlot-$runTimestamp-$runId"
}
$evidencePath = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidencePath -Force | Out-Null

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$session = $null
$remoteEvidencePath = $null
$preflight = $null

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $remotePaths = Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $runId -ScriptBlock {
    param([string]$SafeSlot, [string]$RunId)
    $root = Join-Path $env:LOCALAPPDATA "SystemAudioRoute\acceptance\reaper"
    $evidence = Join-Path (Join-Path $root "runs") $RunId
    $state = Join-Path (Join-Path $root "state") "$SafeSlot.json"
    New-Item -ItemType Directory -Path $evidence -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path $state -Parent) `
        -Force | Out-Null
    [pscustomobject]@{ Evidence = $evidence; State = $state }
  }
  $remoteEvidencePath = [string]$remotePaths.Evidence
  $remoteStatePath = [string]$remotePaths.State

  $preflight = Invoke-Command -Session $session -ArgumentList `
      $RemoteBuildPath, $ReaperPath, $InteractiveUser, $safeSlot, $runId, `
      $remoteEvidencePath, $remoteStatePath, `
      [bool]$RecoverUntrackedProcesses -ScriptBlock {
    param(
      [string]$BuildPath,
      [string]$RequestedReaperPath,
      [string]$RequestedInteractiveUser,
      [string]$SafeSlot,
      [string]$RunId,
      [string]$RemoteEvidence,
      [string]$StatePath,
      [bool]$RecoverUntracked
    )

    $ErrorActionPreference = "Stop"
    function Get-ProcessEvidence {
      param([Diagnostics.Process]$Process)
      $path = ""
      $started = ""
      try { $path = $Process.Path } catch {}
      try { $started = $Process.StartTime.ToUniversalTime().ToString("o") } catch {}
      [pscustomobject]@{
        pid = [int]$Process.Id
        name = [string]$Process.ProcessName
        path = [string]$path
        started_utc = [string]$started
      }
    }

    function Stop-OwnedProcess {
      param($Entry)
      $process = Get-Process -Id ([int]$Entry.pid) -ErrorAction SilentlyContinue
      if ($null -eq $process) { return $true }
      $actualPath = ""
      try { $actualPath = $process.Path } catch { return $false }
      if (![string]::IsNullOrWhiteSpace([string]$Entry.path) -and
          ![string]::Equals($actualPath, [string]$Entry.path,
              [StringComparison]::OrdinalIgnoreCase)) {
        return $false
      }
      if (![string]::IsNullOrWhiteSpace([string]$Entry.started_utc)) {
        $actualStarted = ""
        try { $actualStarted = $process.StartTime.ToUniversalTime().ToString("o") }
        catch { return $false }
        if ($actualStarted -ne [string]$Entry.started_utc) { return $false }
      }
      Stop-Process -Id $process.Id -Force
      try { Wait-Process -Id $process.Id -Timeout 10 -ErrorAction Stop } catch {
        if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue) {
          return $false
        }
      }
      return $true
    }

    $buildPathFull = [IO.Path]::GetFullPath($BuildPath.Trim().Trim('"'))
    $enginePath = Join-Path $buildPathFull "sar_engine_service.exe"
    $reaperPathFull = [IO.Path]::GetFullPath(
        $RequestedReaperPath.Trim().Trim('"'))
    $recoveredProcesses = @()
    $recoveredTasks = @()
    $recoveryWarnings = @()

    if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
      try {
        $staleState = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        foreach ($entry in @($staleState.processes)) {
          if (Stop-OwnedProcess $entry) {
            $recoveredProcesses += [int]$entry.pid
          } else {
            $recoveryWarnings += "Could not prove ownership of PID $($entry.pid)."
          }
        }
        foreach ($taskName in @($staleState.tasks)) {
          if ([string]$taskName -like "SAR-$SafeSlot-*") {
            Stop-ScheduledTask -TaskName ([string]$taskName) `
                -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName ([string]$taskName) `
                -Confirm:$false -ErrorAction SilentlyContinue
            $recoveredTasks += [string]$taskName
          }
        }
        Remove-Item -LiteralPath $StatePath -Force -ErrorAction SilentlyContinue
      } catch {
        $recoveryWarnings += "Stale state could not be recovered: $($_.Exception.Message)"
      }
    }

    Get-ScheduledTask -TaskName "SAR-$SafeSlot-*" -ErrorAction SilentlyContinue |
      ForEach-Object {
        Stop-ScheduledTask -TaskName $_.TaskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $_.TaskName -Confirm:$false `
            -ErrorAction SilentlyContinue
        $recoveredTasks += $_.TaskName
      }

    $conflicts = @(Get-Process reaper, sar_engine_service `
        -ErrorAction SilentlyContinue | Where-Object {
      $processPath = ""
      try { $processPath = $_.Path } catch {}
      [string]::Equals($processPath, $enginePath,
          [StringComparison]::OrdinalIgnoreCase) -or
        [string]::Equals($processPath, $reaperPathFull,
          [StringComparison]::OrdinalIgnoreCase)
    })
    if ($RecoverUntracked) {
      foreach ($process in $conflicts) {
        $recoveredProcesses += $process.Id
        Stop-Process -Id $process.Id -Force
      }
      $conflicts = @()
    }

    $explorerOwners = @(
      Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
        ForEach-Object {
          $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwner
          if ($owner.ReturnValue -eq 0) {
            if ([string]::IsNullOrWhiteSpace($owner.Domain)) { $owner.User }
            else { "$($owner.Domain)\$($owner.User)" }
          }
        } | Sort-Object -Unique
    )
    $interactiveUser = ""
    $blockCode = ""
    $action = ""
    if ([string]::IsNullOrWhiteSpace($RequestedInteractiveUser)) {
      if ($explorerOwners.Count -eq 1) { $interactiveUser = $explorerOwners[0] }
      else {
        $blockCode = "interactive_user_missing"
        $action = "Log in to the Windows desktop as the WinRM user, leave explorer.exe running, then rerun the identical command."
      }
    } else {
      $matches = @($explorerOwners | Where-Object {
        $_ -eq $RequestedInteractiveUser -or $_ -like "*\$RequestedInteractiveUser"
      })
      if ($matches.Count -eq 1) { $interactiveUser = $matches[0] }
      else {
        $blockCode = "interactive_user_not_found"
        $action = "Log in as '$RequestedInteractiveUser', then rerun with the same -InteractiveUser value."
      }
    }
    if ([string]::IsNullOrEmpty($blockCode) -and $conflicts.Count -ne 0) {
      $blockCode = "untracked_processes"
      $action = "Close the listed processes or rerun with -RecoverUntrackedProcesses after confirming they are disposable."
    }
    if ([string]::IsNullOrEmpty($blockCode) -and
        (($interactiveUser -split '\\')[-1] -ne $env:USERNAME)) {
      $blockCode = "interactive_user_mismatch"
      $action = "Run WinRM as the same Windows user that owns the interactive desktop."
    }

    $record = [ordered]@{
      schema_version = 1
      status = if ([string]::IsNullOrEmpty($blockCode)) { "ready" } else { "blocked" }
      stage = "preflight"
      run_id = $RunId
      slot = $SafeSlot
      checked_utc = [datetime]::UtcNow.ToString("o")
      interactive_user = $interactiveUser
      explorer_owners = @($explorerOwners)
      block_code = $blockCode
      action = $action
      conflicts = @($conflicts | ForEach-Object { Get-ProcessEvidence $_ })
      recovered_process_ids = @($recoveredProcesses | Sort-Object -Unique)
      recovered_tasks = @($recoveredTasks | Sort-Object -Unique)
      recovery_warnings = @($recoveryWarnings)
      build_path = $buildPathFull
      reaper_path = $reaperPathFull
    }
    $record | ConvertTo-Json -Depth 6 | Set-Content `
        -LiteralPath (Join-Path $RemoteEvidence "preflight.json") -Encoding UTF8
    [pscustomobject]$record
  }
  $preflight | ConvertTo-Json -Depth 6 | Set-Content `
      -LiteralPath (Join-Path $evidencePath "preflight.json") -Encoding UTF8
  Write-Host (("reaper_asio_preflight status={0} block_code={1} " +
      "run_id={2} evidence=`"{3}`"") -f $preflight.status,
      $(if ([string]::IsNullOrEmpty($preflight.block_code)) {
        "none"
      } else { $preflight.block_code }), $runId, $evidencePath)
  if ($preflight.status -ne "ready") {
    throw "REAPER acceptance blocked [$($preflight.block_code)]: $($preflight.action) Evidence: '$evidencePath'."
  }

  $result = Invoke-Command -Session $session -ArgumentList `
      $RemoteBuildPath, $RenderDeviceId, $ReaperPath, $InteractiveUser, `
      $safeSlot, $ClientCount, $DurationSeconds, `
      $MinimumCallbackCoverageBasisPoints, $MaximumCallbackMicroseconds, `
      $StartupTimeoutSeconds, $runId, $remoteEvidencePath, `
      $remoteStatePath -ScriptBlock {
    param(
      [string]$BuildPath,
      [string]$PinnedRenderId,
      [string]$RequestedReaperPath,
      [string]$RequestedInteractiveUser,
      [string]$SafeSlot,
      [uint32]$RequestedClientCount,
      [uint32]$RequestedDurationSeconds,
      [uint32]$MinimumCoverageBasisPoints,
      [double]$MaximumCallbackUs,
      [uint32]$TimeoutSeconds,
      [string]$RunId,
      [string]$RemoteEvidence,
      [string]$StatePath
    )

    $ErrorActionPreference = "Stop"
    [ordered]@{
      schema_version = 1
      run_id = $RunId
      slot = $SafeSlot
      started_utc = [datetime]::UtcNow.ToString("o")
      build_path = $BuildPath
      render_device_id = $PinnedRenderId
      reaper_path = $RequestedReaperPath
      client_count = $RequestedClientCount
      duration_seconds = $RequestedDurationSeconds
      minimum_coverage_basis_points = $MinimumCoverageBasisPoints
      maximum_callback_microseconds = $MaximumCallbackUs
    } | ConvertTo-Json -Depth 5 | Set-Content `
        -LiteralPath (Join-Path $RemoteEvidence "manifest.json") -Encoding UTF8

    function Save-RunState {
      param([string[]]$Tasks, [Diagnostics.Process[]]$Processes)
      $processRecords = @($Processes | Where-Object { $null -ne $_ } |
        ForEach-Object {
          $processPath = ""
          try { $processPath = $_.Path } catch {}
          [ordered]@{
            pid = [int]$_.Id
            name = [string]$_.ProcessName
            path = [string]$processPath
            started_utc = try {
              $_.StartTime.ToUniversalTime().ToString("o")
            } catch { "" }
          }
        })
      [ordered]@{
        schema_version = 1
        run_id = $RunId
        slot = $SafeSlot
        updated_utc = [datetime]::UtcNow.ToString("o")
        tasks = @($Tasks)
        processes = $processRecords
      } | ConvertTo-Json -Depth 5 | Set-Content `
          -LiteralPath $StatePath -Encoding UTF8
    }

    $buildPathArgument = $BuildPath.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($buildPathArgument)) {
      throw "Remote build path is empty."
    }
    $buildPathFull = [IO.Path]::GetFullPath($buildPathArgument)
    $reaperPathFull = [IO.Path]::GetFullPath(
        $RequestedReaperPath.Trim().Trim('"'))
    $driverPath = Join-Path $buildPathFull "SystemAudioRouteVirtualASIO.dll"
    $registerPath = Join-Path $buildPathFull "sar_virtual_asio_register.exe"
    $enginePath = Join-Path $buildPathFull "sar_engine_service.exe"
    $controlPath = Join-Path $buildPathFull "sar_control_cli.exe"
    foreach ($path in @(
        $driverPath, $registerPath, $enginePath, $controlPath,
        $reaperPathFull)) {
      if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required acceptance executable was not found at '$path'."
      }
    }

    function Get-DiagnosticsSnapshot {
      $text = (& $controlPath diagnostics 2>&1 | Out-String).Trim()
      if ($LASTEXITCODE -ne 0) {
        throw "Diagnostics query failed: $text"
      }
      $fields = @{}
      foreach ($match in [regex]::Matches($text, '([a-z_]+)=([^\s]+)')) {
        $fields[$match.Groups[1].Value] = $match.Groups[2].Value
      }
      foreach ($name in @(
          "processed_blocks", "xruns", "asio_active_producers",
          "asio_pushed_blocks", "asio_dropped_blocks", "asio_consumed_blocks",
          "asio_mixed_blocks", "asio_clipped_samples",
          "asio_non_finite_samples", "callback_peak_us")) {
        if (!$fields.ContainsKey($name)) {
          throw "Diagnostics response is missing '$name': $text"
        }
      }
      [pscustomobject]@{
        Text = $text
        ProcessedBlocks = [uint64]$fields.processed_blocks
        Xruns = [uint64]$fields.xruns
        ActiveProducers = [uint64]$fields.asio_active_producers
        PushedBlocks = [uint64]$fields.asio_pushed_blocks
        DroppedBlocks = [uint64]$fields.asio_dropped_blocks
        ConsumedBlocks = [uint64]$fields.asio_consumed_blocks
        MixedBlocks = [uint64]$fields.asio_mixed_blocks
        ClippedSamples = [uint64]$fields.asio_clipped_samples
        NonFiniteSamples = [uint64]$fields.asio_non_finite_samples
        CallbackPeakMicroseconds = [double]::Parse(
            $fields.callback_peak_us,
            [Globalization.CultureInfo]::InvariantCulture)
      }
    }

    function Get-ReaperProcesses {
      @(Get-Process reaper -ErrorAction SilentlyContinue | Where-Object {
        $candidatePath = ""
        try { $candidatePath = $_.Path } catch {}
        [string]::Equals($candidatePath, $reaperPathFull,
            [StringComparison]::OrdinalIgnoreCase)
      } | Sort-Object Id)
    }

    function Get-TaskLaunchSummary {
      param([string]$TaskName)
      $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
      $info = Get-ScheduledTaskInfo -TaskName $TaskName `
          -ErrorAction SilentlyContinue
      "task=$TaskName state=$($task.State) last_result=$($info.LastTaskResult) last_run=$($info.LastRunTime.ToUniversalTime().ToString('o'))"
    }

    $explorerOwners = @(
      Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
        ForEach-Object {
          $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwner
          if ($owner.ReturnValue -eq 0) {
            if ([string]::IsNullOrWhiteSpace($owner.Domain)) {
              $owner.User
            } else {
              "$($owner.Domain)\$($owner.User)"
            }
          }
        } |
        Sort-Object -Unique
    )
    if ([string]::IsNullOrWhiteSpace($RequestedInteractiveUser)) {
      if ($explorerOwners.Count -ne 1) {
        throw "Expected one interactive Explorer user; found $($explorerOwners.Count)."
      }
      $interactiveUser = $explorerOwners[0]
    } else {
      $interactiveUser = $RequestedInteractiveUser
      $matches = @($explorerOwners | Where-Object {
        $_ -eq $interactiveUser -or $_ -like "*\$interactiveUser"
      })
      if ($matches.Count -ne 1) {
        throw "Interactive user '$interactiveUser' does not identify one Explorer session."
      }
      $interactiveUser = $matches[0]
    }

    $interactiveLeaf = ($interactiveUser -split '\\')[-1]
    if ($interactiveLeaf -ne $env:USERNAME) {
      throw "WinRM user '$env:USERNAME' must match interactive user '$interactiveLeaf' for per-user ASIO registration."
    }
    $unexpectedProcesses = @(Get-Process reaper, sar_engine_service `
        -ErrorAction SilentlyContinue | Where-Object {
      $candidatePath = ""
      try { $candidatePath = $_.Path } catch {}
      [string]::Equals($candidatePath, $enginePath,
          [StringComparison]::OrdinalIgnoreCase) -or
        [string]::Equals($candidatePath, $reaperPathFull,
          [StringComparison]::OrdinalIgnoreCase)
    })
    if ($unexpectedProcesses.Count -ne 0) {
      throw "REAPER or sar_engine_service is already running; acceptance refuses to take over an existing session."
    }

    $reaperIni = Join-Path $env:APPDATA "REAPER\reaper.ini"
    if (!(Test-Path -LiteralPath $reaperIni -PathType Leaf)) {
      throw "REAPER configuration was not found at '$reaperIni'."
    }
    $reaperConfig = Get-Content -LiteralPath $reaperIni -Raw
    foreach ($requiredSetting in @(
        '(?m)^mode=3\s*$',
        '(?m)^asio_driver_name="System Audio Route Virtual ASIO"\s*$',
        '(?m)^asio_srate=48000\s*$',
        '(?m)^asio_srate_use=1\s*$',
        '(?m)^asio_bsize=128\s*$',
        '(?m)^asio_bsize_use=1\s*$')) {
      if ($reaperConfig -notmatch $requiredSetting) {
        throw "REAPER is not configured for the expected Virtual ASIO 48 kHz/128-frame contract."
      }
    }

    $stage = "register_driver"
    & $registerPath --register $driverPath --user --x64
    if ($LASTEXITCODE -ne 0) {
      throw "Virtual ASIO registration failed with exit code $LASTEXITCODE."
    }
    & $registerPath --verify $driverPath --user --x64
    if ($LASTEXITCODE -ne 0) {
      throw "Virtual ASIO registration verification failed with exit code $LASTEXITCODE."
    }

    $suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
    $engineTask = "SAR-$SafeSlot-engine-$suffix"
    $reaperTasks = @()
    $engineProcess = $null
    $reaperProcesses = @()
    try {
      Save-RunState @() @()
      $principal = New-ScheduledTaskPrincipal -UserId $interactiveUser `
          -LogonType Interactive -RunLevel Limited
      $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
          -DontStopIfGoingOnBatteries
      $engineArguments = "--wasapi-render --render-id `"$PinnedRenderId`""
      $engineAction = New-ScheduledTaskAction -Execute $enginePath `
          -Argument $engineArguments
      Register-ScheduledTask -TaskName $engineTask -Action $engineAction `
          -Principal $principal -Settings $settings | Out-Null
      Save-RunState @($engineTask) @()
      $stage = "launch_engine"
      Start-ScheduledTask -TaskName $engineTask

      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      while ([datetime]::UtcNow -lt $deadline -and $null -eq $engineProcess) {
        Start-Sleep -Milliseconds 250
        $engineProcess = Get-Process sar_engine_service -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -eq $enginePath } |
            Select-Object -First 1
      }
      if ($null -eq $engineProcess) {
        throw "Engine did not start within $TimeoutSeconds seconds. $(Get-TaskLaunchSummary $engineTask)"
      }
      Save-RunState @($engineTask) @($engineProcess)

      for ($clientIndex = 1; $clientIndex -le $RequestedClientCount; ++$clientIndex) {
        $reaperTask = "SAR-$SafeSlot-reaper-$clientIndex-$suffix"
        $reaperTasks += $reaperTask
        if ($RequestedClientCount -gt 1) {
          $reaperAction = New-ScheduledTaskAction -Execute $reaperPathFull `
              -Argument "-newinst"
        } else {
          $reaperAction = New-ScheduledTaskAction -Execute $reaperPathFull
        }
        Register-ScheduledTask -TaskName $reaperTask -Action $reaperAction `
            -Principal $principal -Settings $settings | Out-Null
        Save-RunState (@($reaperTasks) + @($engineTask)) @($engineProcess)
        $stage = "launch_reaper_$clientIndex"
        Start-ScheduledTask -TaskName $reaperTask
        $clientDeadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
          Start-Sleep -Milliseconds 250
          $reaperProcesses = @(Get-ReaperProcesses)
          Save-RunState (@($reaperTasks) + @($engineTask)) `
              (@($engineProcess) + @($reaperProcesses))
        } while ([datetime]::UtcNow -lt $clientDeadline -and
            $reaperProcesses.Count -lt $clientIndex)
        if ($reaperProcesses.Count -lt $clientIndex) {
          throw "REAPER client $clientIndex did not start within $TimeoutSeconds seconds. $(Get-TaskLaunchSummary $reaperTask)"
        }
      }

      $stage = "wait_driver_load"
      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      $loadedProcesses = @()
      while ([datetime]::UtcNow -lt $deadline -and
          $loadedProcesses.Count -lt $RequestedClientCount) {
        Start-Sleep -Milliseconds 250
        $reaperProcesses = @(Get-ReaperProcesses)
        Save-RunState (@($reaperTasks) + @($engineTask)) `
            (@($engineProcess) + @($reaperProcesses))
        $loadedProcesses = @($reaperProcesses | Where-Object {
          try {
            @($_.Modules | Where-Object {
              [string]::Equals($_.FileName, $driverPath,
                  [StringComparison]::OrdinalIgnoreCase)
            }).Count -eq 1
          } catch {
            $false
          }
        })
      }
      if ($reaperProcesses.Count -lt $RequestedClientCount) {
        throw "Expected $RequestedClientCount REAPER processes; found $($reaperProcesses.Count)."
      }
      if ($loadedProcesses.Count -lt $RequestedClientCount) {
        throw "Only $($loadedProcesses.Count) of $RequestedClientCount REAPER processes loaded SystemAudioRouteVirtualASIO.dll."
      }
      $reaperProcesses = @($loadedProcesses | Select-Object -First $RequestedClientCount)
      Save-RunState (@($reaperTasks) + @($engineTask)) `
          (@($engineProcess) + @($reaperProcesses))

      $stage = "wait_active_producers"
      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      do {
        $initial = Get-DiagnosticsSnapshot
        if ($initial.ActiveProducers -eq $RequestedClientCount) {
          break
        }
        Start-Sleep -Milliseconds 100
      } while ([datetime]::UtcNow -lt $deadline)
      if ($initial.ActiveProducers -ne $RequestedClientCount) {
        throw "Initial active producer count was $($initial.ActiveProducers), expected $RequestedClientCount."
      }
      $initial.Text | Set-Content `
          -LiteralPath (Join-Path $RemoteEvidence "diagnostics-initial.txt") `
          -Encoding UTF8
      $initial | ConvertTo-Json -Depth 5 | Set-Content `
          -LiteralPath (Join-Path $RemoteEvidence "diagnostics-initial.json") `
          -Encoding UTF8
      $stage = "measure"
      Start-Sleep -Seconds $RequestedDurationSeconds
      $final = Get-DiagnosticsSnapshot
      $final.Text | Set-Content `
          -LiteralPath (Join-Path $RemoteEvidence "diagnostics-final.txt") `
          -Encoding UTF8
      $final | ConvertTo-Json -Depth 5 | Set-Content `
          -LiteralPath (Join-Path $RemoteEvidence "diagnostics-final.json") `
          -Encoding UTF8
      foreach ($process in $reaperProcesses) {
        if ($null -eq (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
          throw "REAPER process $($process.Id) exited during acceptance."
        }
      }
      if ($final.ActiveProducers -ne $RequestedClientCount) {
        throw "Final active producer count was $($final.ActiveProducers), expected $RequestedClientCount."
      }

      $pushedDelta = $final.PushedBlocks - $initial.PushedBlocks
      $droppedDelta = $final.DroppedBlocks - $initial.DroppedBlocks
      $consumedDelta = $final.ConsumedBlocks - $initial.ConsumedBlocks
      $mixedDelta = $final.MixedBlocks - $initial.MixedBlocks
      $processedDelta = $final.ProcessedBlocks - $initial.ProcessedBlocks
      $xrunDelta = $final.Xruns - $initial.Xruns
      $clippedDelta = $final.ClippedSamples - $initial.ClippedSamples
      $nonFiniteDelta = $final.NonFiniteSamples - $initial.NonFiniteSamples
      $nominalBlocksPerClient = [double]$RequestedDurationSeconds * 48000.0 / 128.0
      $coverage = [double]$MinimumCoverageBasisPoints / 10000.0
      $minimumProducerBlocks = [uint64][Math]::Floor(
          $nominalBlocksPerClient * $RequestedClientCount * $coverage)
      $minimumMixedBlocks = [uint64][Math]::Floor(
          $nominalBlocksPerClient * $coverage)

      $stage = "validate_measurement"
      if ($pushedDelta -lt $minimumProducerBlocks -or
          $consumedDelta -lt $minimumProducerBlocks -or
          $mixedDelta -lt $minimumMixedBlocks -or
          $processedDelta -lt $minimumMixedBlocks -or
          $droppedDelta -ne 0 -or $xrunDelta -ne 0 -or
          $clippedDelta -ne 0 -or $nonFiniteDelta -ne 0 -or
          $final.CallbackPeakMicroseconds -gt $MaximumCallbackUs) {
        throw ((("REAPER ASIO duration acceptance failed: duration={0} " +
            "pushed_delta={1}/{2} consumed_delta={3}/{2} mixed_delta={4}/{5} " +
            "processed_delta={6}/{5} dropped_delta={7} xrun_delta={8} " +
            "clipped_delta={9} non_finite_delta={10} callback_peak_us={11}/{12}. " +
            "Final diagnostics: {13}") -f
            $RequestedDurationSeconds, $pushedDelta, $minimumProducerBlocks,
            $consumedDelta, $mixedDelta, $minimumMixedBlocks, $processedDelta,
            $droppedDelta, $xrunDelta, $clippedDelta, $nonFiniteDelta,
            $final.CallbackPeakMicroseconds, $MaximumCallbackUs, $final.Text))
      }

      $reaperProcessIds = [string]::Join(",", @($reaperProcesses.Id))
      $disconnectSequence = @($reaperProcesses.Count)
      $stage = "disconnect_clients"
      for ($processIndex = 0;
          $processIndex -lt $reaperProcesses.Count;
          ++$processIndex) {
        Stop-Process -Id $reaperProcesses[$processIndex].Id -Force
        $expectedProducers = $reaperProcesses.Count - $processIndex - 1
        $disconnectDeadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
          Start-Sleep -Milliseconds 100
          $afterDisconnect = Get-DiagnosticsSnapshot
          if ($afterDisconnect.ActiveProducers -eq $expectedProducers) {
            break
          }
        } while ([datetime]::UtcNow -lt $disconnectDeadline)
        if ($afterDisconnect.ActiveProducers -ne $expectedProducers) {
          throw "REAPER process disconnected, but active producer count remained $($afterDisconnect.ActiveProducers); expected $expectedProducers."
        }
        $disconnectSequence += $expectedProducers
      }
      $reaperProcesses = @()

      $acceptanceResult = [pscustomobject]@{
        SchemaVersion = 1
        Status = "passed"
        RunId = $RunId
        Slot = $SafeSlot
        CompletedUtc = [datetime]::UtcNow.ToString("o")
        DriverPath = $driverPath
        EngineProcessId = $engineProcess.Id
        ReaperProcessIds = $reaperProcessIds
        SessionId = $loadedProcesses[0].SessionId
        ClientCount = $RequestedClientCount
        DurationSeconds = $RequestedDurationSeconds
        ActiveProducers = $final.ActiveProducers
        PushedDelta = $pushedDelta
        MinimumProducerBlocks = $minimumProducerBlocks
        DroppedDelta = $droppedDelta
        ConsumedDelta = $consumedDelta
        MixedDelta = $mixedDelta
        MinimumMixedBlocks = $minimumMixedBlocks
        ProcessedDelta = $processedDelta
        XrunDelta = $xrunDelta
        CallbackPeakMicroseconds = $final.CallbackPeakMicroseconds
        DisconnectSequence = [string]::Join("->", $disconnectSequence)
        Diagnostics = $final.Text
      }
      $acceptanceResult | ConvertTo-Json -Depth 6 | Set-Content `
          -LiteralPath (Join-Path $RemoteEvidence "result.json") -Encoding UTF8
      $acceptanceResult
    } catch {
      [ordered]@{
        schema_version = 1
        status = "failed"
        stage = $stage
        run_id = $RunId
        slot = $SafeSlot
        failed_utc = [datetime]::UtcNow.ToString("o")
        error = $_.Exception.Message
        error_type = $_.Exception.GetType().FullName
        script_stack = $_.ScriptStackTrace
      } | ConvertTo-Json -Depth 6 | Set-Content `
          -LiteralPath (Join-Path $RemoteEvidence "result.json") -Encoding UTF8
      throw
    } finally {
      foreach ($reaperProcess in $reaperProcesses) {
        Stop-Process -Id $reaperProcess.Id -Force -ErrorAction SilentlyContinue
      }
      if ($null -ne $engineProcess) {
        Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
      }
      foreach ($taskName in @($reaperTasks) + @($engineTask)) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false `
            -ErrorAction SilentlyContinue
      }
      $cleanupComplete = $true
      if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
        try {
          $cleanupState = Get-Content -LiteralPath $StatePath -Raw |
              ConvertFrom-Json
          foreach ($entry in @($cleanupState.processes)) {
            if (Get-Process -Id ([int]$entry.pid) -ErrorAction SilentlyContinue) {
              $cleanupComplete = $false
            }
          }
          foreach ($taskName in @($cleanupState.tasks)) {
            if (Get-ScheduledTask -TaskName ([string]$taskName) `
                -ErrorAction SilentlyContinue) {
              $cleanupComplete = $false
            }
          }
        } catch {
          $cleanupComplete = $false
        }
      }
      if ($cleanupComplete) {
        Remove-Item -LiteralPath $StatePath -Force -ErrorAction SilentlyContinue
      }
    }
  }

  Write-Host $result.Diagnostics
  Write-Host ((("reaper_asio_acceptance status=passed run_id={0} session={1} " +
      "engine_pid={2} reaper_pids={3} clients={4} active_producers={5} " +
      "duration_seconds={6} pushed_delta={7} minimum_pushed={8} " +
      "dropped_delta={9} consumed_delta={10} mixed_delta={11} " +
      "minimum_mixed={12} processed_delta={13} xrun_delta={14} " +
      "callback_peak_us={15} disconnect_sequence={16} driver=`"{17}`" " +
      "evidence=`"{18}`"") -f
      $runId, $result.SessionId, $result.EngineProcessId, $result.ReaperProcessIds,
      $result.ClientCount, $result.ActiveProducers, $result.DurationSeconds,
      $result.PushedDelta, $result.MinimumProducerBlocks, $result.DroppedDelta,
      $result.ConsumedDelta, $result.MixedDelta, $result.MinimumMixedBlocks,
      $result.ProcessedDelta, $result.XrunDelta,
      $result.CallbackPeakMicroseconds, $result.DisconnectSequence,
      $result.DriverPath, $evidencePath))
} catch {
  $localStatus = if ($null -ne $preflight -and
      $preflight.status -eq "blocked") { "blocked" } else { "failed" }
  [ordered]@{
    schema_version = 1
    status = $localStatus
    run_id = $runId
    slot = $safeSlot
    failed_utc = [datetime]::UtcNow.ToString("o")
    error = $_.Exception.Message
    error_type = $_.Exception.GetType().FullName
  } | ConvertTo-Json -Depth 5 | Set-Content `
      -LiteralPath (Join-Path $evidencePath "local-failure.json") -Encoding UTF8
  Write-Error "REAPER acceptance failed. Evidence: '$evidencePath'. $($_.Exception.Message)"
} finally {
  if ($null -ne $session) {
    if (![string]::IsNullOrWhiteSpace($remoteEvidencePath)) {
      try {
        Copy-Item -FromSession $session `
            -Path (Join-Path $remoteEvidencePath "*") `
            -Destination $evidencePath -Recurse -Force -ErrorAction Stop
      } catch {
        $_.Exception.Message | Set-Content `
            -LiteralPath (Join-Path $evidencePath "evidence-copy-error.txt") `
            -Encoding UTF8
      }
    }
    Remove-PSSession $session
  }
}
