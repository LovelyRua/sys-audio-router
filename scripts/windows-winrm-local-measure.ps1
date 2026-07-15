param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "local-measure",
  [string]$RepoRoot = "",
  [ValidateSet("render", "duplex", "loopback", "both", "all")]
  [string]$Mode = "render",
  [uint32]$DurationMs = 1000,
  [uint32]$TimeoutMs = 10,
  [uint32]$Iterations = 1,
  [string]$CaptureId = "",
  [string]$RenderId = "",
  [string]$EvidenceDirectory = "",
  [uint64]$MaximumRenderRecoverySilenceFrames = 0,
  [ValidateRange(0, 10000)]
  [uint32]$MinimumRenderedFrameCoverageBasisPoints = 9900,
  [string]$RequireHealthyText = "false",
  [string]$AllowUnavailableText = "false",
  [switch]$CleanupCompletedSlots,
  [switch]$CleanupDryRun,
  [string]$CleanupCompletedSlotsText = "",
  [string]$CleanupDryRunText = "",
  [ValidateRange(1, 3650)]
  [uint32]$RetentionDays = 14,
  [ValidateRange(1, 1000)]
  [uint32]$RetentionCount = 8,
  [ValidateRange(1, 100)]
  [uint32]$CleanupLimit = 2,
  [ValidateRange(24, 8760)]
  [uint32]$StaleActiveHours = 24
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
  $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
  $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}

$requireHealthy = $false
if ($RequireHealthyText -match '^(1|true|yes|y|on)$') {
  $requireHealthy = $true
} elseif ($RequireHealthyText -notmatch '^(0|false|no|n|off)$') {
  throw "RequireHealthyText must be true/false, yes/no, on/off, or 1/0."
}

$allowUnavailable = $false
if ($AllowUnavailableText -match '^(1|true|yes|y|on)$') {
  $allowUnavailable = $true
} elseif ($AllowUnavailableText -notmatch '^(0|false|no|n|off)$') {
  throw "AllowUnavailableText must be true/false, yes/no, on/off, or 1/0."
}
if ($Iterations -eq 0) {
  throw "Iterations must be at least one."
}
if ([string]::IsNullOrWhiteSpace($CaptureId) -ne
    [string]::IsNullOrWhiteSpace($RenderId)) {
  throw "CaptureId and RenderId must be supplied together."
}

$gitHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($gitHead)) {
  throw "Could not resolve the source commit for the evidence manifest."
}
if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
  $evidenceStamp = [datetime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
  $EvidenceDirectory = Join-Path $RepoRoot ".sar-evidence\$evidenceStamp-$safeSlot"
} elseif (![System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
  $EvidenceDirectory = Join-Path $RepoRoot $EvidenceDirectory
}
$EvidenceDirectory = [System.IO.Path]::GetFullPath($EvidenceDirectory)
$remoteEvidenceDirectory = "C:\Windows\Temp\sar-evidence-$safeSlot"

$archive = Join-Path $env:TEMP "sar-local-measure-$safeSlot.zip"
if (Test-Path $archive) {
  Remove-Item -LiteralPath $archive -Force
}

Write-Host "Creating local source archive from HEAD"
Write-Host "Repository: $RepoRoot"
Write-Host "Slot:       $safeSlot"
Write-Host "Archive:    $archive"

& git -C $RepoRoot archive --format=zip HEAD -o $archive
if ($LASTEXITCODE -ne 0) {
  throw "git archive failed with exit code $LASTEXITCODE."
}

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$session = $null

function ConvertTo-OptionalBoolean {
  param([string]$Value, [string]$Name)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $false
  }
  if ($Value -match '^(1|true|yes|y|on)$') {
    return $true
  }
  if ($Value -match '^(0|false|no|n|off)$') {
    return $false
  }
  throw "$Name must be true/false, yes/no, on/off, or 1/0."
}

$cleanupRequested = $CleanupCompletedSlots.IsPresent -or
    (ConvertTo-OptionalBoolean $CleanupCompletedSlotsText "CleanupCompletedSlotsText")
$cleanupDryRunRequested = $CleanupDryRun.IsPresent -or
    (ConvertTo-OptionalBoolean $CleanupDryRunText "CleanupDryRunText")
$cleanupEnabled = $cleanupRequested -or $cleanupDryRunRequested

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $remoteArchive = "C:\Windows\Temp\sar-local-measure-$safeSlot.zip"
  Write-Host "Uploading archive to $HostName"
  Copy-Item -LiteralPath $archive -Destination $remoteArchive -ToSession $session -Force

  Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $remoteArchive, $Mode, $DurationMs, $TimeoutMs, $requireHealthy, `
      $allowUnavailable, $Iterations, $cleanupEnabled, $cleanupDryRunRequested, `
      $RetentionDays, $RetentionCount, $CleanupLimit, $StaleActiveHours, `
      $CaptureId, $RenderId, $remoteEvidenceDirectory, $gitHead `
      , $MaximumRenderRecoverySilenceFrames `
      , $MinimumRenderedFrameCoverageBasisPoints `
      -ScriptBlock {
    param(
      [string]$SafeSlot,
      [string]$RemoteArchive,
      [string]$Mode,
      [uint32]$DurationMs,
      [uint32]$TimeoutMs,
      [bool]$RequireHealthy,
      [bool]$AllowUnavailable,
      [uint32]$Iterations,
      [bool]$CleanupEnabled,
      [bool]$CleanupDryRun,
      [uint32]$RetentionDays,
      [uint32]$RetentionCount,
      [uint32]$CleanupLimit,
      [uint32]$StaleActiveHours,
      [string]$CaptureId,
      [string]$RenderId,
      [string]$EvidenceDirectory,
      [string]$GitHead
      ,[uint64]$MaximumRenderRecoverySilenceFrames
      ,[uint32]$MinimumRenderedFrameCoverageBasisPoints
    )

    $ErrorActionPreference = "Stop"
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    $slotRoot = Join-Path $env:USERPROFILE "src"
    $slotKey = "local-measure-$SafeSlot"
    $repoDir = Join-Path $slotRoot "sys-audio-router-$slotKey"
    $buildDir = "build-$slotKey"
    $cmdFile = "C:\Windows\Temp\sar-local-measure-$SafeSlot.cmd"
    $activeTokenDir = Join-Path $slotRoot ".sar-slot-active\$slotKey"
    $activeToken = Join-Path $activeTokenDir "$([guid]::NewGuid().ToString('N')).active"
    $finishedMarker = Join-Path $repoDir ".sar-slot-finished.json"
    $slotOutcome = "failure"
    $slotStarted = $false
    $evidenceManifest = $null
    $measureArgs = @("--duration-ms", "$DurationMs", "--timeout-ms", "$TimeoutMs")
    if ($RequireHealthy) {
      $measureArgs += "--require-healthy"
    }

    function Invoke-WithSlotRetentionLock {
      param([Parameter(Mandatory = $true)][scriptblock]$Action)

      $lockPath = Join-Path $slotRoot ".sar-slot-retention.lock"
      $lockStream = $null
      for ($attempt = 0; $attempt -lt 50 -and $null -eq $lockStream; ++$attempt) {
        try {
          $lockStream = [IO.File]::Open(
              $lockPath, [IO.FileMode]::OpenOrCreate,
              [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        } catch [IO.IOException] {
          Start-Sleep -Milliseconds 100
        }
      }
      if ($null -eq $lockStream) {
        throw "Timed out waiting for the remote slot-retention lock."
      }
      try {
        & $Action
      } finally {
        $lockStream.Dispose()
      }
    }

    function Get-FinishedSlotRetentionSelection {
      param(
        [Parameter(Mandatory = $true)]
        [string]$SlotRoot,
        [Parameter(Mandatory = $true)]
        [string]$CurrentRepoDir,
        [Parameter(Mandatory = $true)]
        [datetime]$NowUtc,
        [Parameter(Mandatory = $true)]
        [uint32]$RetentionDays,
        [Parameter(Mandatory = $true)]
        [uint32]$RetentionCount,
        [Parameter(Mandatory = $true)]
        [uint32]$CleanupLimit,
        [Parameter(Mandatory = $true)]
        [uint32]$StaleActiveHours,
        [bool]$ProcessInspectionSucceeded = $false,
        [AllowNull()]
        [string[]]$ProcessCommandLines = $null
      )

      $rootPath = [IO.Path]::GetFullPath($SlotRoot).TrimEnd([char[]]@('\', '/'))
      $currentPath = [IO.Path]::GetFullPath($CurrentRepoDir).TrimEnd([char[]]@('\', '/'))
      $cutoffUtc = $NowUtc.ToUniversalTime().AddDays(-[double]$RetentionDays)
      $eligible = @()

      foreach ($directory in @(Get-ChildItem -LiteralPath $rootPath -Directory -Force)) {
        if ($directory.Name -notmatch '^sys-audio-router-[A-Za-z0-9](?:[A-Za-z0-9_.-]*[A-Za-z0-9])?$') {
          continue
        }

        $candidatePath = [IO.Path]::GetFullPath($directory.FullName).TrimEnd([char[]]@('\', '/'))
        $expectedPath = [IO.Path]::GetFullPath(
            (Join-Path $rootPath $directory.Name)).TrimEnd([char[]]@('\', '/'))
        $parentPath = [IO.Path]::GetFullPath($directory.Parent.FullName).TrimEnd([char[]]@('\', '/'))
        if (![string]::Equals($candidatePath, $expectedPath, [StringComparison]::OrdinalIgnoreCase) -or
            ![string]::Equals($parentPath, $rootPath, [StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($candidatePath, $currentPath, [StringComparison]::OrdinalIgnoreCase) -or
            (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
          continue
        }

        $slotName = $directory.Name.Substring('sys-audio-router-'.Length)
        $activeDir = Join-Path $rootPath ".sar-slot-active\$slotName"
        $activeTokens = @()
        if (Test-Path -LiteralPath $activeDir) {
          $activeTokens = @(Get-ChildItem -LiteralPath $activeDir -Filter "*.active" -File -Force)
        }
        if ($activeTokens.Count -ne 0) {
          $staleCutoffUtc = $NowUtc.ToUniversalTime().AddHours(-[double]$StaleActiveHours)
          $hasFreshToken = @($activeTokens | Where-Object {
            $_.LastWriteTimeUtc -ge $staleCutoffUtc
          }).Count -ne 0
          if ($hasFreshToken -or !$ProcessInspectionSucceeded) {
            continue
          }

          $escapedPath = [regex]::Escape($candidatePath)
          $exactPathPattern = "(?i)(?<![A-Za-z0-9_.-])$escapedPath(?=`$|[\s`"'\\/])"
          $hasMatchingProcess = @($ProcessCommandLines | Where-Object {
            ![string]::IsNullOrWhiteSpace($_) -and $_ -match $exactPathPattern
          }).Count -ne 0
          if ($hasMatchingProcess) {
            continue
          }
        }

        $markerPath = Join-Path $candidatePath ".sar-slot-finished.json"
        if (!(Test-Path -LiteralPath $markerPath -PathType Leaf)) {
          continue
        }
        try {
          $finishedRecord = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
          if ($finishedRecord.outcome -notmatch '^(success|failure)$') {
            continue
          }
          $finishedUtc = [datetimeoffset]::Parse(
              [string]$finishedRecord.finished_utc,
              [Globalization.CultureInfo]::InvariantCulture,
              [Globalization.DateTimeStyles]::RoundtripKind).UtcDateTime
        } catch {
          continue
        }

        $eligible += [pscustomobject]@{
          Name = $directory.Name
          Path = $candidatePath
          FinishedUtc = $finishedUtc
        }
      }

      $ordered = @($eligible | Sort-Object FinishedUtc -Descending)
      $selected = @()
      for ($index = 0; $index -lt $ordered.Count; ++$index) {
        $reasons = @()
        if ($ordered[$index].FinishedUtc -lt $cutoffUtc) {
          $reasons += "age"
        }
        if ($index -ge $RetentionCount) {
          $reasons += "count"
        }
        if ($reasons.Count -ne 0) {
          $selected += [pscustomobject]@{
            Name = $ordered[$index].Name
            Path = $ordered[$index].Path
            FinishedUtc = $ordered[$index].FinishedUtc
            Reason = [string]::Join(",", $reasons)
          }
        }
      }

      @($selected | Sort-Object FinishedUtc | Select-Object -First $CleanupLimit)
    }

    function Invoke-FinishedSlotRetention {
      param([switch]$DryRun)

      Invoke-WithSlotRetentionLock {
        $processInspectionSucceeded = $false
        $processCommandLines = @()
        try {
          $processCommandLines = @(Get-CimInstance Win32_Process -ErrorAction Stop |
              ForEach-Object { $_.CommandLine })
          $processInspectionSucceeded = $true
        } catch {
          Write-Warning "Process inspection failed; slots with active tokens will be retained."
        }
        $selection = @(Get-FinishedSlotRetentionSelection `
            -SlotRoot $slotRoot -CurrentRepoDir $repoDir -NowUtc ([datetime]::UtcNow) `
            -RetentionDays $RetentionDays -RetentionCount $RetentionCount `
            -CleanupLimit $CleanupLimit -StaleActiveHours $StaleActiveHours `
            -ProcessInspectionSucceeded $processInspectionSucceeded `
            -ProcessCommandLines $processCommandLines)
        foreach ($candidate in $selection) {
          $action = if ($DryRun) { "Would remove" } else { "Removing" }
          Write-Host "$action finished slot '$($candidate.Name)' (reason=$($candidate.Reason), finished=$($candidate.FinishedUtc.ToString('o')))."
          if (!$DryRun) {
            Remove-Item -LiteralPath $candidate.Path -Recurse -Force
          }
        }
        if ($selection.Count -eq 0) {
          Write-Host "No finished remote slots matched the retention policy."
        }
      }
    }

    try {
      Write-Host "Repository: $repoDir"
      Write-Host "Build dir:  $buildDir"
      Write-Host "Archive:    $RemoteArchive"
      Write-Host "Mode:       $Mode"
      Write-Host "Allow unavailable endpoint: $AllowUnavailable"
      Write-Host "Iterations: $Iterations"

      New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
      Invoke-WithSlotRetentionLock {
        New-Item -ItemType Directory -Path $activeTokenDir -Force | Out-Null
        $activeRecord = [ordered]@{
          started_utc = [datetime]::UtcNow.ToString('o')
          process_id = $PID
          repo_path = $repoDir
        }
        $activeRecord | ConvertTo-Json -Compress |
            Set-Content -LiteralPath $activeToken -Encoding ASCII
      }
      $slotStarted = $true
      if (Test-Path -LiteralPath $EvidenceDirectory) {
        Remove-Item -LiteralPath $EvidenceDirectory -Recurse -Force
      }
      New-Item -ItemType Directory -Path $EvidenceDirectory | Out-Null
      $evidenceManifest = [ordered]@{
        schema_version = 1
        started_utc = [datetime]::UtcNow.ToString('o')
        host = $env:COMPUTERNAME
        slot = $SafeSlot
        git_head = $GitHead
        mode = $Mode
        duration_ms = $DurationMs
        timeout_ms = $TimeoutMs
        iterations = $Iterations
        capture_id = $CaptureId
        render_id = $RenderId
        maximum_render_recovery_silence_frames = $MaximumRenderRecoverySilenceFrames
        minimum_rendered_frame_coverage_basis_points = $MinimumRenderedFrameCoverageBasisPoints
        outcome = "running"
      }
      $evidenceManifest | ConvertTo-Json -Depth 4 |
          Set-Content -LiteralPath (Join-Path $EvidenceDirectory "manifest.json") -Encoding UTF8
      if ($CleanupEnabled) {
        Invoke-FinishedSlotRetention -DryRun:$CleanupDryRun
      }
      if (Test-Path -LiteralPath $repoDir) {
        Remove-Item -LiteralPath $repoDir -Recurse -Force
      }
      New-Item -ItemType Directory -Path $repoDir | Out-Null
      tar.exe -xf $RemoteArchive -C $repoDir
      if ($LASTEXITCODE -ne 0) {
        throw "tar extraction failed with exit code $LASTEXITCODE."
      }

      $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
      if (!(Test-Path $vswhere)) {
        throw "vswhere.exe was not found."
      }
      $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
      if ([string]::IsNullOrWhiteSpace($vsInstall)) {
        throw "Visual Studio C++ tools were not found."
      }
      $vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
      if (!(Test-Path $vsDevCmd)) {
        throw "VsDevCmd.bat was not found."
      }

      $targets = @()
      $executables = @{}
      if ($Mode -eq "render" -or $Mode -eq "both" -or $Mode -eq "all") {
        $targets += "sar_measure_wasapi_render_loop"
        $executables.render = Join-Path $repoDir "$buildDir\sar_measure_wasapi_render_loop.exe"
      }
      if ($Mode -eq "duplex" -or $Mode -eq "both" -or $Mode -eq "all") {
        $targets += "sar_measure_wasapi_duplex_loop"
        $executables.duplex = Join-Path $repoDir "$buildDir\sar_measure_wasapi_duplex_loop.exe"
      }
      if ($Mode -eq "loopback" -or $Mode -eq "all") {
        $targets += "sar_measure_wasapi_loopback_loop"
        $executables.loopback = Join-Path $repoDir "$buildDir\sar_measure_wasapi_loopback_loop.exe"
      }
      if ($targets.Count -eq 0) {
        throw "No measurement targets selected for mode '$Mode'."
      }

      $targetArgs = [string]::Join(" ", $targets)
      $lines = @(
        "@echo off",
        "setlocal EnableExtensions",
        "call `"$vsDevCmd`" -arch=x64 -host_arch=x64",
        "if errorlevel 1 exit /b 1",
        "set `"PATH=$vsInstall\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vsInstall\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%`"",
        "cd /d `"$repoDir`"",
        "where ninja >nul 2>nul",
        "if errorlevel 1 (cmake -S . -B `"$buildDir`") else (cmake -S . -B `"$buildDir`" -G Ninja)",
        "if errorlevel 1 exit /b 1",
        "cmake --build `"$buildDir`" --target $targetArgs",
        "if errorlevel 1 exit /b 1"
      )
      $lines += "exit /b 0"

      Set-Content -LiteralPath $cmdFile -Value $lines -Encoding ASCII
      cmd.exe /c "`"$cmdFile`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Local WASAPI measurement failed with exit code $LASTEXITCODE."
      }

      Import-Module (Join-Path $repoDir "scripts\windows-wasapi-soak-runner.psm1") -Force
      $soakOutput = @(Invoke-WasapiSoak -Mode $Mode -Iterations $Iterations `
          -MaximumRenderRecoverySilenceFrames $MaximumRenderRecoverySilenceFrames `
          -MinimumRenderedFrameCoverageBasisPoints $MinimumRenderedFrameCoverageBasisPoints `
          -RunMeasurement {
        param($modeName, $iteration)
        $previousErrorActionPreference = $ErrorActionPreference
        $measurementExitCode = 1
        $ErrorActionPreference = "Continue"
        try {
          $attemptArgs = @($measureArgs)
          if ($modeName -eq "duplex" -and
              ![string]::IsNullOrWhiteSpace($CaptureId)) {
            $attemptArgs += @("--capture-id", $CaptureId, "--render-id", $RenderId)
          }
          $attemptBase = "attempt-{0}-{1:D3}" -f $modeName, $iteration
          $commandText = '"{0}" {1}' -f $executables[$modeName],
              ([string]::Join(' ', ($attemptArgs | ForEach-Object {
                if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
              })))
          Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$attemptBase.command.txt") `
              -Value $commandText -Encoding UTF8
          $measurementOutput = @(& $executables[$modeName] @attemptArgs 2>&1 |
              ForEach-Object { [string]$_ })
          $measurementExitCode = $LASTEXITCODE
          Set-Content -LiteralPath (Join-Path $EvidenceDirectory "$attemptBase.combined.log") `
              -Value $measurementOutput -Encoding UTF8
          $measurementOutput | Write-Host
        } finally {
          $ErrorActionPreference = $previousErrorActionPreference
        }
        return $measurementExitCode
      })
      $soakOutput[0..($soakOutput.Count - 2)] | Write-Output
      $soakOutput[0..($soakOutput.Count - 2)] |
          Set-Content -LiteralPath (Join-Path $EvidenceDirectory "soak-summary.log") -Encoding UTF8
      $soakResult = $soakOutput[-1]
      if (!$AllowUnavailable -and $soakResult.FailureCount -ne 0) {
        throw "Local WASAPI measurement failed in $($soakResult.FailureCount) of $($soakResult.Attempts) attempts."
      }
      $slotOutcome = "success"
    } catch {
      $slotOutcome = "failure"
      throw
    } finally {
      if ($slotStarted) {
        try {
          if ($null -ne $evidenceManifest) {
            $evidenceManifest.outcome = $slotOutcome
            $evidenceManifest.finished_utc = [datetime]::UtcNow.ToString('o')
            $evidenceManifest | ConvertTo-Json -Depth 4 |
                Set-Content -LiteralPath (Join-Path $EvidenceDirectory "manifest.json") -Encoding UTF8
          }
          New-Item -ItemType Directory -Path $repoDir -Force | Out-Null
          $finishedRecord = [ordered]@{
            outcome = $slotOutcome
            finished_utc = [datetime]::UtcNow.ToString('o')
          }
          $finishedRecord | ConvertTo-Json -Compress |
              Set-Content -LiteralPath $finishedMarker -Encoding ASCII
        } catch {
          Write-Warning "Could not write the slot-finished marker: $($_.Exception.Message)"
        }
      }
      try {
        if (Test-Path -LiteralPath $activeToken) {
          Remove-Item -LiteralPath $activeToken -Force
        }
      } catch {
        Write-Warning "Could not remove the slot active token: $($_.Exception.Message)"
      }
      if ($slotStarted -and $CleanupEnabled) {
        try {
          Invoke-FinishedSlotRetention -DryRun:$CleanupDryRun
        } catch {
          Write-Warning "Finished-slot retention was skipped: $($_.Exception.Message)"
        }
      }
      foreach ($temporaryPath in @($RemoteArchive, $cmdFile)) {
        try {
          if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
          }
        } catch {
          Write-Warning "Could not remove temporary file '$temporaryPath': $($_.Exception.Message)"
        }
      }
    }
  }
} finally {
  try {
    if ($null -ne $session) {
      if (Invoke-Command -Session $session -ArgumentList $remoteEvidenceDirectory `
          -ScriptBlock { param($Path) Test-Path -LiteralPath $Path }) {
        New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
        Copy-Item -Path "$remoteEvidenceDirectory\*" -Destination $EvidenceDirectory `
            -FromSession $session -Recurse -Force
        Write-Host "Evidence:   $EvidenceDirectory"
        Invoke-Command -Session $session -ArgumentList $remoteEvidenceDirectory `
            -ScriptBlock { param($Path) Remove-Item -LiteralPath $Path -Recurse -Force }
      }
      Remove-PSSession $session
    }
  } catch {
    Write-Warning "Could not remove the WinRM session: $($_.Exception.Message)"
  }
  try {
    if (Test-Path -LiteralPath $archive) {
      Remove-Item -LiteralPath $archive -Force
    }
  } catch {
    Write-Warning "Could not remove local archive '$archive': $($_.Exception.Message)"
  }
}
