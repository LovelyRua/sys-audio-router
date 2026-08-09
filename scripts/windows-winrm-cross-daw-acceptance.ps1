param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [Parameter(Mandatory = $true)]
  [string]$RemoteBuildPath,
  [string]$FirstDawProcessName = "Cakewalk",
  [string]$SecondDawPath =
      "C:\Program Files\REAPER (x64)\reaper.exe",
  [string]$InteractiveUser = "",
  [string]$Slot = "cross-daw",
  [ValidateRange(1, 86400)]
  [uint32]$DurationSeconds = 30,
  [ValidateRange(1, 10000)]
  [uint32]$MinimumCallbackCoverageBasisPoints = 8000,
  [ValidateRange(1.0, 1000000.0)]
  [double]$MaximumCallbackMicroseconds = 10000.0,
  [ValidateRange(1, 300)]
  [uint32]$StartupTimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"
$safeSlot = $Slot -replace '[^A-Za-z0-9_.-]', '-'
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot must contain at least one safe character."
}

$credentialUserName = $UserName
if ($UserName -notmatch '\\' -and $UserName -notmatch '@') {
  $credentialUserName = "$HostName\$UserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$session = $null

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $result = Invoke-Command -Session $session -ArgumentList `
      $RemoteBuildPath, $FirstDawProcessName, $SecondDawPath, `
      $InteractiveUser, $safeSlot, $DurationSeconds, `
      $MinimumCallbackCoverageBasisPoints, $MaximumCallbackMicroseconds, `
      $StartupTimeoutSeconds -ScriptBlock {
    param(
      [string]$BuildPath,
      [string]$RequestedFirstDawProcessName,
      [string]$RequestedSecondDawPath,
      [string]$RequestedInteractiveUser,
      [string]$SafeSlot,
      [uint32]$RequestedDurationSeconds,
      [uint32]$MinimumCoverageBasisPoints,
      [double]$MaximumCallbackUs,
      [uint32]$TimeoutSeconds
    )

    $ErrorActionPreference = "Stop"
    $buildPathFull = [IO.Path]::GetFullPath($BuildPath.Trim().Trim('"'))
    $secondDawPathFull =
        [IO.Path]::GetFullPath($RequestedSecondDawPath.Trim().Trim('"'))
    $driverPath = Join-Path $buildPathFull "SystemAudioRouteVirtualASIO.dll"
    $controlPath = Join-Path $buildPathFull "sar_control_cli.exe"
    foreach ($path in @($driverPath, $controlPath, $secondDawPathFull)) {
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
          "asio_pushed_blocks", "asio_dropped_blocks",
          "asio_producer_underflows", "asio_producer_overflows",
          "asio_consumed_blocks",
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
        ProducerUnderflows = [uint64]$fields.asio_producer_underflows
        ProducerOverflows = [uint64]$fields.asio_producer_overflows
        ConsumedBlocks = [uint64]$fields.asio_consumed_blocks
        MixedBlocks = [uint64]$fields.asio_mixed_blocks
        ClippedSamples = [uint64]$fields.asio_clipped_samples
        NonFiniteSamples = [uint64]$fields.asio_non_finite_samples
        CallbackPeakMicroseconds = [double]::Parse(
            $fields.callback_peak_us,
            [Globalization.CultureInfo]::InvariantCulture)
      }
    }

    function Test-DriverLoaded([Diagnostics.Process]$Process) {
      try {
        @($Process.Modules | ForEach-Object { $_.FileName }) -contains $driverPath
      } catch {
        $false
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
      $matches = @($explorerOwners | Where-Object {
        $_ -eq $RequestedInteractiveUser -or
        $_ -like "*\$RequestedInteractiveUser"
      })
      if ($matches.Count -ne 1) {
        throw "Interactive user '$RequestedInteractiveUser' does not identify one Explorer session."
      }
      $interactiveUser = $matches[0]
    }

    $interactiveLeaf = ($interactiveUser -split '\\')[-1]
    if ($interactiveLeaf -ne $env:USERNAME) {
      throw "WinRM user '$env:USERNAME' must match interactive user '$interactiveLeaf'."
    }

    $firstDawProcesses = @(
      Get-Process -Name $RequestedFirstDawProcessName `
          -ErrorAction SilentlyContinue |
        Where-Object { Test-DriverLoaded $_ }
    )
    if ($firstDawProcesses.Count -ne 1) {
      throw "Expected exactly one '$RequestedFirstDawProcessName' process with the virtual ASIO driver loaded; found $($firstDawProcesses.Count)."
    }
    $firstDawProcess = $firstDawProcesses[0]

    $secondProcessName = [IO.Path]::GetFileNameWithoutExtension(
        $secondDawPathFull)
    if (@(Get-Process -Name $secondProcessName `
        -ErrorAction SilentlyContinue).Count -ne 0) {
      throw "'$secondProcessName' is already running; acceptance refuses to take it over."
    }
    $initialSingleClient = Get-DiagnosticsSnapshot
    if ($initialSingleClient.ActiveProducers -ne 1) {
      throw "First DAW must be the only active producer before launch; found $($initialSingleClient.ActiveProducers)."
    }

    $suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
    $secondDawTask = "SAR-$SafeSlot-second-daw-$suffix"
    $secondDawProcess = $null
    try {
      $principal = New-ScheduledTaskPrincipal -UserId $interactiveUser `
          -LogonType Interactive -RunLevel Limited
      $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
          -DontStopIfGoingOnBatteries
      $action = New-ScheduledTaskAction -Execute $secondDawPathFull
      Register-ScheduledTask -TaskName $secondDawTask -Action $action `
          -Principal $principal -Settings $settings | Out-Null
      Start-ScheduledTask -TaskName $secondDawTask

      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      while ([datetime]::UtcNow -lt $deadline -and
          $null -eq $secondDawProcess) {
        Start-Sleep -Milliseconds 250
        $secondDawProcess = Get-Process -Name $secondProcessName `
            -ErrorAction SilentlyContinue |
          Where-Object { Test-DriverLoaded $_ } |
          Select-Object -First 1
      }
      if ($null -eq $secondDawProcess) {
        throw "Second DAW did not load the virtual ASIO driver within $TimeoutSeconds seconds."
      }

      $deadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
      do {
        $initial = Get-DiagnosticsSnapshot
        if ($initial.ActiveProducers -eq 2) {
          break
        }
        Start-Sleep -Milliseconds 250
      } while ([datetime]::UtcNow -lt $deadline)
      if ($initial.ActiveProducers -ne 2) {
        throw "Both DAWs loaded the driver, but active producer count was $($initial.ActiveProducers)."
      }

      Start-Sleep -Seconds $RequestedDurationSeconds
      $final = Get-DiagnosticsSnapshot
      foreach ($process in @($firstDawProcess, $secondDawProcess)) {
        if ($null -eq (Get-Process -Id $process.Id `
            -ErrorAction SilentlyContinue)) {
          throw "DAW process $($process.Id) exited during acceptance."
        }
      }
      if ($final.ActiveProducers -ne 2) {
        throw "Final active producer count was $($final.ActiveProducers), expected 2."
      }

      $pushedDelta = $final.PushedBlocks - $initial.PushedBlocks
      $droppedDelta = $final.DroppedBlocks - $initial.DroppedBlocks
      $producerUnderflowDelta =
          $final.ProducerUnderflows - $initial.ProducerUnderflows
      $producerOverflowDelta =
          $final.ProducerOverflows - $initial.ProducerOverflows
      $consumedDelta = $final.ConsumedBlocks - $initial.ConsumedBlocks
      $mixedDelta = $final.MixedBlocks - $initial.MixedBlocks
      $processedDelta = $final.ProcessedBlocks - $initial.ProcessedBlocks
      $xrunDelta = $final.Xruns - $initial.Xruns
      $clippedDelta = $final.ClippedSamples - $initial.ClippedSamples
      $nonFiniteDelta = $final.NonFiniteSamples - $initial.NonFiniteSamples
      $nominalBlocksPerClient =
          [double]$RequestedDurationSeconds * 48000.0 / 128.0
      $coverage = [double]$MinimumCoverageBasisPoints / 10000.0
      $minimumProducerBlocks = [uint64][Math]::Floor(
          $nominalBlocksPerClient * 2.0 * $coverage)
      $minimumMixedBlocks = [uint64][Math]::Floor(
          $nominalBlocksPerClient * $coverage)

      if ($pushedDelta -lt $minimumProducerBlocks -or
          $consumedDelta -lt $minimumProducerBlocks -or
          $mixedDelta -lt $minimumMixedBlocks -or
          $processedDelta -lt $minimumMixedBlocks -or
          $droppedDelta -ne 0 -or $producerUnderflowDelta -ne 0 -or
          $producerOverflowDelta -ne 0 -or $xrunDelta -ne 0 -or
          $clippedDelta -ne 0 -or $nonFiniteDelta -ne 0 -or
          $final.CallbackPeakMicroseconds -gt $MaximumCallbackUs) {
        throw ((("Cross-DAW duration acceptance failed: duration={0} " +
            "pushed_delta={1}/{2} consumed_delta={3}/{2} " +
            "mixed_delta={4}/{5} processed_delta={6}/{5} " +
            "dropped_delta={7} producer_underflow_delta={8} " +
            "producer_overflow_delta={9} xrun_delta={10} clipped_delta={11} " +
            "non_finite_delta={12} callback_peak_us={13}/{14}. " +
            "Final diagnostics: {15}") -f
            $RequestedDurationSeconds, $pushedDelta, $minimumProducerBlocks,
            $consumedDelta, $mixedDelta, $minimumMixedBlocks, $processedDelta,
            $droppedDelta, $producerUnderflowDelta, $producerOverflowDelta,
            $xrunDelta, $clippedDelta, $nonFiniteDelta,
            $final.CallbackPeakMicroseconds, $MaximumCallbackUs, $final.Text))
      }

      [pscustomobject]@{
        DriverPath = $driverPath
        FirstDawName = $RequestedFirstDawProcessName
        FirstDawProcessId = $firstDawProcess.Id
        SecondDawName = $secondProcessName
        SecondDawProcessId = $secondDawProcess.Id
        SessionId = $secondDawProcess.SessionId
        DurationSeconds = $RequestedDurationSeconds
        ActiveProducers = $final.ActiveProducers
        PushedDelta = $pushedDelta
        MinimumProducerBlocks = $minimumProducerBlocks
        DroppedDelta = $droppedDelta
        ProducerUnderflowDelta = $producerUnderflowDelta
        ProducerOverflowDelta = $producerOverflowDelta
        ConsumedDelta = $consumedDelta
        MixedDelta = $mixedDelta
        MinimumMixedBlocks = $minimumMixedBlocks
        ProcessedDelta = $processedDelta
        XrunDelta = $xrunDelta
        CallbackPeakMicroseconds = $final.CallbackPeakMicroseconds
      }
    } finally {
      if ($null -ne $secondDawProcess) {
        Stop-Process -Id $secondDawProcess.Id -Force `
            -ErrorAction SilentlyContinue
        $disconnectDeadline = [datetime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
          Start-Sleep -Milliseconds 100
          $afterDisconnect = Get-DiagnosticsSnapshot
          if ($afterDisconnect.ActiveProducers -eq 1) {
            break
          }
        } while ([datetime]::UtcNow -lt $disconnectDeadline)
        if ($afterDisconnect.ActiveProducers -ne 1) {
          throw "Second DAW disconnected, but active producer count remained $($afterDisconnect.ActiveProducers); expected 1."
        }
      }
      Stop-ScheduledTask -TaskName $secondDawTask `
          -ErrorAction SilentlyContinue
      Unregister-ScheduledTask -TaskName $secondDawTask -Confirm:$false `
          -ErrorAction SilentlyContinue
    }
  }

  Write-Host ((("cross_daw_asio_acceptance status=passed session={0} " +
      "first_daw={1} first_pid={2} second_daw={3} second_pid={4} " +
      "active_producers={5} duration_seconds={6} pushed_delta={7} " +
      "minimum_pushed={8} dropped_delta={9} consumed_delta={10} " +
      "mixed_delta={11} minimum_mixed={12} processed_delta={13} " +
      "xrun_delta={14} callback_peak_us={15} driver=`"{16}`"") -f
      $result.SessionId, $result.FirstDawName, $result.FirstDawProcessId,
      $result.SecondDawName, $result.SecondDawProcessId,
      $result.ActiveProducers, $result.DurationSeconds, $result.PushedDelta,
      $result.MinimumProducerBlocks, $result.DroppedDelta,
      $result.ConsumedDelta, $result.MixedDelta, $result.MinimumMixedBlocks,
      $result.ProcessedDelta, $result.XrunDelta,
      $result.CallbackPeakMicroseconds, $result.DriverPath))
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
