param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "",
  [string]$CleanupCompletedSlotsText = "true",
  [string]$CleanupDryRunText = "false",
  [ValidateRange(1, 3650)]
  [uint32]$RetentionDays = 14,
  [ValidateRange(1, 1000)]
  [uint32]$RetentionCount = 4,
  [ValidateRange(1, 100)]
  [uint32]$CleanupLimit = 4,
  [ValidateRange(24, 8760)]
  [uint32]$StaleActiveHours = 24
)

$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$expectedCommit = (& git -C $repositoryRoot rev-parse HEAD 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or $expectedCommit -notmatch '^[0-9a-fA-F]{40}$') {
  throw "Could not resolve the local repository HEAD for remote validation."
}
$localChanges = @(& git -C $repositoryRoot status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0) {
  throw "Could not inspect the local repository worktree before remote validation."
}
if ($localChanges.Count -ne 0) {
  throw "Remote validation clones GitHub and cannot test local changes. Commit and push the worktree first."
}

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

$cleanupEnabled = ConvertTo-OptionalBoolean $CleanupCompletedSlotsText "CleanupCompletedSlotsText"
$cleanupDryRun = ConvertTo-OptionalBoolean $CleanupDryRunText "CleanupDryRunText"
if ($cleanupDryRun) {
  $cleanupEnabled = $true
}

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$safeSlot = ""
if (-not [string]::IsNullOrWhiteSpace($Slot)) {
  $safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
  if ([string]::IsNullOrWhiteSpace($safeSlot)) {
    throw "Slot '$Slot' does not contain any valid path characters."
  }
}

$session = $null
try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential

  Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $cleanupEnabled, $cleanupDryRun, $RetentionDays, $RetentionCount, `
      $CleanupLimit, $StaleActiveHours, $expectedCommit -ScriptBlock {
    param(
      [string]$SafeSlot,
      [bool]$CleanupEnabled,
      [bool]$CleanupDryRun,
      [uint32]$RetentionDays,
      [uint32]$RetentionCount,
      [uint32]$CleanupLimit,
      [uint32]$StaleActiveHours,
      [string]$ExpectedCommit
    )

    $ErrorActionPreference = "Stop"
    $slotName = $SafeSlot
    if ([string]::IsNullOrWhiteSpace($slotName)) {
      $slotName = "default"
    }
    $slotRoot = Join-Path $env:USERPROFILE "src"
    $lockRoot = Join-Path $slotRoot ".sar-slot-locks"
    $lockPath = Join-Path $lockRoot "$slotName.lock"
    if ([string]::IsNullOrWhiteSpace($SafeSlot)) {
      $bootstrap = "C:\Windows\Temp\sar-bootstrap.cmd"
      $repoDir = Join-Path $slotRoot "sys-audio-router"
      $buildDir = "build"
    } else {
      $bootstrap = "C:\Windows\Temp\sar-bootstrap-$SafeSlot.cmd"
      $repoDir = Join-Path $slotRoot "sys-audio-router-$SafeSlot"
      $buildDir = "build-$SafeSlot"
    }
    $activeTokenDir = Join-Path $slotRoot ".sar-slot-active\$slotName"
    $activeToken = Join-Path $activeTokenDir "$([guid]::NewGuid().ToString('N')).active"
    $finishedMarker = Join-Path $repoDir ".sar-slot-finished.json"
    $slotOutcome = "failure"
    $slotStarted = $false

    function Invoke-WithSlotRetentionLock {
      param([Parameter(Mandatory = $true)][scriptblock]$Action)

      $retentionLockPath = Join-Path $slotRoot ".sar-slot-retention.lock"
      $lockStream = $null
      for ($attempt = 0; $attempt -lt 50 -and $null -eq $lockStream; ++$attempt) {
        try {
          $lockStream = [IO.File]::Open(
              $retentionLockPath, [IO.FileMode]::OpenOrCreate,
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
        [Parameter(Mandatory = $true)][string]$SlotRoot,
        [Parameter(Mandatory = $true)][string]$CurrentRepoDir,
        [Parameter(Mandatory = $true)][datetime]$NowUtc,
        [Parameter(Mandatory = $true)][uint32]$RetentionDays,
        [Parameter(Mandatory = $true)][uint32]$RetentionCount,
        [Parameter(Mandatory = $true)][uint32]$CleanupLimit,
        [Parameter(Mandatory = $true)][uint32]$StaleActiveHours,
        [bool]$ProcessInspectionSucceeded = $false,
        [AllowNull()][string[]]$ProcessCommandLines = $null
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

        $candidateSlot = $directory.Name.Substring('sys-audio-router-'.Length)
        $activeDir = Join-Path $rootPath ".sar-slot-active\$candidateSlot"
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

    New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $lockRoot -Force | Out-Null
    $slotLock = $null
    try {
      try {
        $slotLock = [System.IO.File]::Open(
            $lockPath,
            [System.IO.FileMode]::OpenOrCreate,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
      } catch [System.IO.IOException] {
        throw "Test slot '$slotName' is already active on $env:COMPUTERNAME. Use a unique slot or wait for the current run to finish."
      }

      Invoke-WithSlotRetentionLock {
        New-Item -ItemType Directory -Path $activeTokenDir -Force | Out-Null
        [ordered]@{
          slot = $slotName
          started_utc = [datetime]::UtcNow.ToString('o')
          process_id = $PID
          repo_path = $repoDir
        } | ConvertTo-Json -Compress |
            Set-Content -LiteralPath $activeToken -Encoding ASCII
      }
      $slotStarted = $true
      if ($CleanupEnabled) {
        Invoke-FinishedSlotRetention -DryRun:$CleanupDryRun
      }

      if ((Test-Path -LiteralPath $repoDir -PathType Container) -and
          !(Test-Path -LiteralPath (Join-Path $repoDir ".git")) -and
          (Test-Path -LiteralPath $finishedMarker -PathType Leaf)) {
        $replaceIncompleteRepo = $false
        try {
          $previousRecord = Get-Content -LiteralPath $finishedMarker -Raw | ConvertFrom-Json
          $replaceIncompleteRepo = $previousRecord.outcome -match '^(success|failure)$'
        } catch {
          $replaceIncompleteRepo = $false
        }
        if ($replaceIncompleteRepo) {
          Write-Host "Removing completed non-Git checkout for slot '$slotName' before download."
          Remove-Item -LiteralPath $repoDir -Recurse -Force
        }
      }

      $url = "https://raw.githubusercontent.com/LovelyRua/sys-audio-router/main/scripts/windows-test-bootstrap.cmd"
      curl.exe --silent --show-error -L $url -o $bootstrap
      if ($LASTEXITCODE -ne 0) {
        throw "Failed to download bootstrap with exit code $LASTEXITCODE."
      }

      if ([string]::IsNullOrWhiteSpace($SafeSlot)) {
        cmd.exe /c "`"$bootstrap`" `"`" `"`" `"$ExpectedCommit`" 2>&1"
      } else {
        cmd.exe /c "`"$bootstrap`" `"$repoDir`" `"$buildDir`" `"$ExpectedCommit`" 2>&1"
      }
      if ($LASTEXITCODE -ne 0) {
        throw "Bootstrap failed with exit code $LASTEXITCODE."
      }
      $slotOutcome = "success"
    } catch {
      $slotOutcome = "failure"
      throw
    } finally {
      if ($slotStarted) {
        try {
          if (Test-Path -LiteralPath $repoDir -PathType Container) {
            [ordered]@{
              slot = $slotName
              outcome = $slotOutcome
              finished_utc = [datetime]::UtcNow.ToString('o')
            } | ConvertTo-Json -Compress |
                Set-Content -LiteralPath $finishedMarker -Encoding ASCII
          }
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
      if ($null -ne $slotLock) {
        $slotLock.Dispose()
      }
      try {
        if (Test-Path -LiteralPath $bootstrap) {
          Remove-Item -LiteralPath $bootstrap -Force
        }
      } catch {
        Write-Warning "Could not remove bootstrap '$bootstrap': $($_.Exception.Message)"
      }
    }
  }
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
