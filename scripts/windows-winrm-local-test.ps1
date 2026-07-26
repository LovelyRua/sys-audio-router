param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "local",
  [string]$RepoRoot = "",
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
$slotHasher = [Security.Cryptography.SHA256]::Create()
try {
  $slotHash = [BitConverter]::ToString(
      $slotHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($safeSlot))) `
      -replace '-', ''
  $slotPathId = $slotHash.Substring(0, 12).ToLowerInvariant()
} finally {
  $slotHasher.Dispose()
}

$archive = Join-Path $env:TEMP "sar-local-$slotPathId.zip"
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
  $remoteArchive = "C:\Windows\Temp\sar-local-$slotPathId.zip"
  Write-Host "Uploading archive to $HostName"
  Copy-Item -LiteralPath $archive -Destination $remoteArchive -ToSession $session -Force

  Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $slotPathId, $remoteArchive, $cleanupEnabled, $cleanupDryRunRequested, `
      $RetentionDays, $RetentionCount, $CleanupLimit, $StaleActiveHours `
      -ScriptBlock {
    param(
      [string]$SafeSlot,
      [string]$PathSlot,
      [string]$RemoteArchive,
      [bool]$CleanupEnabled,
      [bool]$CleanupDryRun,
      [uint32]$RetentionDays,
      [uint32]$RetentionCount,
      [uint32]$CleanupLimit,
      [uint32]$StaleActiveHours
    )

    $ErrorActionPreference = "Stop"
    $slotRoot = Join-Path $env:USERPROFILE "src"
    $slotKey = "lt-$PathSlot"
    $repoDir = Join-Path $slotRoot "sys-audio-router-$slotKey"
    $buildDir = "b-$PathSlot"
    $cmdFile = "C:\Windows\Temp\sar-local-build-$PathSlot.cmd"
    $activeTokenDir = Join-Path $slotRoot ".sar-slot-active\$slotKey"
    $activeToken = Join-Path $activeTokenDir "$([guid]::NewGuid().ToString('N')).active"
    $finishedMarker = Join-Path $repoDir ".sar-slot-finished.json"
    $slotOutcome = "failure"
    $slotStarted = $false

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

      New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
      Invoke-WithSlotRetentionLock {
        New-Item -ItemType Directory -Path $activeTokenDir -Force | Out-Null
        $activeRecord = [ordered]@{
          slot = $SafeSlot
          started_utc = [datetime]::UtcNow.ToString('o')
          process_id = $PID
          repo_path = $repoDir
        }
        $activeRecord | ConvertTo-Json -Compress |
            Set-Content -LiteralPath $activeToken -Encoding ASCII
      }
      $slotStarted = $true
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

      $lines = @(
        "@echo off",
        "setlocal EnableExtensions",
        "call `"$vsDevCmd`" -arch=x64 -host_arch=x64",
        "if errorlevel 1 exit /b 1",
        "set `"PATH=C:\Tools\cmake-4.4.0-windows-x86_64\bin;$vsInstall\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vsInstall\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%`"",
        "cd /d `"$repoDir`"",
        "where ninja >nul 2>nul",
        "if errorlevel 1 (cmake -S . -B `"$buildDir`") else (cmake -S . -B `"$buildDir`" -G Ninja)",
        "if errorlevel 1 exit /b 1",
        "cmake --build `"$buildDir`"",
        "if errorlevel 1 exit /b 1",
        "ctest --test-dir `"$buildDir`" --output-on-failure",
        "exit /b %errorlevel%"
      )
      Set-Content -LiteralPath $cmdFile -Value $lines -Encoding ASCII
      cmd.exe /c "`"$cmdFile`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Local archive build failed with exit code $LASTEXITCODE."
      }
      $slotOutcome = "success"
    } catch {
      $slotOutcome = "failure"
      throw
    } finally {
      if ($slotStarted) {
        try {
          New-Item -ItemType Directory -Path $repoDir -Force | Out-Null
          $finishedRecord = [ordered]@{
            slot = $SafeSlot
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
