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
      $safeSlot, $ClientCount, $StartupTimeoutSeconds -ScriptBlock {
    param(
      [string]$BuildPath,
      [string]$PinnedRenderId,
      [string]$RequestedReaperPath,
      [string]$RequestedInteractiveUser,
      [string]$SafeSlot,
      [uint32]$RequestedClientCount,
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

      Start-Sleep -Seconds 3
      $diagnostics = (& $controlPath diagnostics 2>&1 | Out-String).Trim()
      if ($LASTEXITCODE -ne 0) {
        throw "Diagnostics query failed: $diagnostics"
      }
      $fields = @{}
      foreach ($match in [regex]::Matches($diagnostics, '([a-z_]+)=([^\s]+)')) {
        $fields[$match.Groups[1].Value] = $match.Groups[2].Value
      }
      foreach ($name in @(
          "asio_active_producers", "asio_pushed_blocks", "asio_dropped_blocks",
          "asio_consumed_blocks", "asio_mixed_blocks")) {
        if (!$fields.ContainsKey($name)) {
          throw "Diagnostics response is missing '$name': $diagnostics"
        }
      }
      $activeProducers = [uint64]$fields.asio_active_producers
      $pushedBlocks = [uint64]$fields.asio_pushed_blocks
      $droppedBlocks = [uint64]$fields.asio_dropped_blocks
      $consumedBlocks = [uint64]$fields.asio_consumed_blocks
      $mixedBlocks = [uint64]$fields.asio_mixed_blocks
      if ($activeProducers -ne $RequestedClientCount -or $pushedBlocks -eq 0 -or
          $consumedBlocks -eq 0 -or $mixedBlocks -eq 0 -or
          $droppedBlocks -ne 0) {
        throw "REAPER ASIO diagnostics failed acceptance: $diagnostics"
      }

      [pscustomobject]@{
        DriverPath = $driverPath
        EngineProcessId = $engineProcess.Id
        ReaperProcessIds = [string]::Join(",", @($reaperProcesses.Id))
        SessionId = $reaperProcesses[0].SessionId
        ClientCount = $RequestedClientCount
        ActiveProducers = $activeProducers
        PushedBlocks = $pushedBlocks
        DroppedBlocks = $droppedBlocks
        ConsumedBlocks = $consumedBlocks
        MixedBlocks = $mixedBlocks
        Diagnostics = $diagnostics
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
      "pushed={5} dropped={6} consumed={7} mixed={8} driver=`"{9}`"") -f
      $result.SessionId, $result.EngineProcessId, $result.ReaperProcessIds,
      $result.ClientCount, $result.ActiveProducers, $result.PushedBlocks,
      $result.DroppedBlocks, $result.ConsumedBlocks, $result.MixedBlocks,
      $result.DriverPath))
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
