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
  [uint32]$StartupTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$session = $null

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $result = Invoke-Command -Session $session -ArgumentList `
      $RemoteBuildPath, $RenderDeviceId, $ReaperPath, $InteractiveUser, `
      $safeSlot, $ClientCount, $DurationSeconds, `
      $MinimumCallbackCoverageBasisPoints, $MaximumCallbackMicroseconds, `
      $StartupTimeoutSeconds -ScriptBlock {
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
      [uint32]$TimeoutSeconds
    )

    $ErrorActionPreference = "Stop"
    $buildPathArgument = $BuildPath.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($buildPathArgument)) {
      throw "Remote build path is empty."
    }
    $buildPathFull = [IO.Path]::GetFullPath($buildPathArgument)
    $driverPath = Join-Path $buildPathFull "SystemAudioRouteVirtualASIO.dll"
    $registerPath = Join-Path $buildPathFull "sar_virtual_asio_register.exe"
    $enginePath = Join-Path $buildPathFull "sar_engine_service.exe"
    $controlPath = Join-Path $buildPathFull "sar_control_cli.exe"
    foreach ($path in @(
        $driverPath, $registerPath, $enginePath, $controlPath,
        $RequestedReaperPath)) {
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
    if (@(Get-Process reaper, sar_engine_service -ErrorAction SilentlyContinue).Count -ne 0) {
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
      $principal = New-ScheduledTaskPrincipal -UserId $interactiveUser `
          -LogonType Interactive -RunLevel Limited
      $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
          -DontStopIfGoingOnBatteries
      $engineArguments = "--wasapi-render --render-id `"$PinnedRenderId`""
      $engineAction = New-ScheduledTaskAction -Execute $enginePath `
          -Argument $engineArguments
      Register-ScheduledTask -TaskName $engineTask -Action $engineAction `
          -Principal $principal -Settings $settings | Out-Null
      Start-ScheduledTask -TaskName $engineTask

      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      while ([datetime]::UtcNow -lt $deadline -and $null -eq $engineProcess) {
        Start-Sleep -Milliseconds 250
        $engineProcess = Get-Process sar_engine_service -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -eq $enginePath } |
            Select-Object -First 1
      }
      if ($null -eq $engineProcess) {
        throw "Engine did not start within $TimeoutSeconds seconds."
      }

      for ($clientIndex = 1; $clientIndex -le $RequestedClientCount; ++$clientIndex) {
        $reaperTask = "SAR-$SafeSlot-reaper-$clientIndex-$suffix"
        $reaperTasks += $reaperTask
        if ($RequestedClientCount -gt 1) {
          $reaperAction = New-ScheduledTaskAction -Execute $RequestedReaperPath `
              -Argument "-newinst"
        } else {
          $reaperAction = New-ScheduledTaskAction -Execute $RequestedReaperPath
        }
        Register-ScheduledTask -TaskName $reaperTask -Action $reaperAction `
            -Principal $principal -Settings $settings | Out-Null
        Start-ScheduledTask -TaskName $reaperTask
        if ($clientIndex -lt $RequestedClientCount) {
          Start-Sleep -Milliseconds 500
        }
      }

      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      $loadedProcesses = @()
      while ([datetime]::UtcNow -lt $deadline -and
          $loadedProcesses.Count -lt $RequestedClientCount) {
        Start-Sleep -Milliseconds 250
        $reaperProcesses = @(Get-Process reaper -ErrorAction SilentlyContinue)
        $loadedProcesses = @($reaperProcesses | Where-Object {
          try {
            @($_.Modules | ForEach-Object {
              $_.ModuleName
            }) -contains "SystemAudioRouteVirtualASIO.dll"
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
      Start-Sleep -Seconds $RequestedDurationSeconds
      $final = Get-DiagnosticsSnapshot
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

      [pscustomobject]@{
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
    }
  }

  Write-Host $result.Diagnostics
  Write-Host ((("reaper_asio_acceptance status=passed session={0} " +
      "engine_pid={1} reaper_pids={2} clients={3} active_producers={4} " +
      "duration_seconds={5} pushed_delta={6} minimum_pushed={7} " +
      "dropped_delta={8} consumed_delta={9} mixed_delta={10} " +
      "minimum_mixed={11} processed_delta={12} xrun_delta={13} " +
      "callback_peak_us={14} disconnect_sequence={15} driver=`"{16}`"") -f
      $result.SessionId, $result.EngineProcessId, $result.ReaperProcessIds,
      $result.ClientCount, $result.ActiveProducers, $result.DurationSeconds,
      $result.PushedDelta, $result.MinimumProducerBlocks, $result.DroppedDelta,
      $result.ConsumedDelta, $result.MixedDelta, $result.MinimumMixedBlocks,
      $result.ProcessedDelta, $result.XrunDelta,
      $result.CallbackPeakMicroseconds, $result.DisconnectSequence,
      $result.DriverPath))
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
