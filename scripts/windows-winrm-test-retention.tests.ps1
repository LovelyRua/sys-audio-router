$ErrorActionPreference = "Stop"

function Assert-Equal {
  param($Expected, $Actual, [string]$Message)
  if ($Expected -ne $Actual) {
    throw "$Message Expected '$Expected', got '$Actual'."
  }
}

function Add-FinishedSlot {
  param(
    [string]$Root,
    [string]$Slot,
    [datetime]$FinishedUtc,
    [ValidateSet("success", "failure")][string]$Outcome = "success"
  )

  $path = Join-Path $Root "sys-audio-router-$Slot"
  New-Item -ItemType Directory -Path $path | Out-Null
  [ordered]@{
    slot = $Slot
    outcome = $Outcome
    finished_utc = $FinishedUtc.ToString('o')
  } | ConvertTo-Json -Compress |
      Set-Content -LiteralPath (Join-Path $path ".sar-slot-finished.json") -Encoding ASCII
  $path
}

function Add-ActiveToken {
  param([string]$Root, [string]$Slot, [datetime]$StartedUtc)

  $tokenDir = Join-Path $Root ".sar-slot-active\$Slot"
  New-Item -ItemType Directory -Path $tokenDir -Force | Out-Null
  $token = New-Item -ItemType File -Path (Join-Path $tokenDir "run.active") -Force
  $token.LastWriteTimeUtc = $StartedUtc
}

$scriptPath = Join-Path $PSScriptRoot "windows-winrm-test.ps1"
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$parseErrors)
Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in windows-winrm-test.ps1."

foreach ($functionName in @(
    "Invoke-WithSlotRetentionLock", "Get-FinishedSlotRetentionSelection",
    "Invoke-FinishedSlotRetention")) {
  $matches = @($ast.FindAll({
    param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $functionName
  }, $true))
  Assert-Equal 1 $matches.Count "Unexpected '$functionName' definition count."
  Set-Item -Path "function:script:$functionName" -Value $matches[0].Body.GetScriptBlock()
}

$scriptText = Get-Content -LiteralPath $scriptPath -Raw
Assert-Equal $true ($scriptText -match '(?s)catch\s*\{\s*\$slotOutcome = "failure"\s*throw\s*\}\s*finally') `
    "Run failures are not preserved through slot finalization."
Assert-Equal $true ($scriptText.IndexOf('Set-Content -LiteralPath $activeToken') -lt
    $scriptText.IndexOf('Invoke-FinishedSlotRetention -DryRun:$CleanupDryRun')) `
    "The current run must become active before retention starts."
Assert-Equal $true $scriptText.Contains('($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)') `
    "Retention does not reject reparse points."
Assert-Equal $true ($scriptText -match '(?s)if \(Test-Path -LiteralPath \$repoDir -PathType Container\)\s*\{\s*\[ordered\]@\{') `
    "Finalization can create an empty repository just to write a marker."
Assert-Equal $true $scriptText.Contains('Removing completed non-Git checkout') `
    "A completed partial checkout cannot recover on the next run."

$wrapperText = Get-Content -LiteralPath (Join-Path $PSScriptRoot "windows-winrm-test.cmd") -Raw
foreach ($setting in @(
    "SAR_SLOT_CLEANUP", "SAR_SLOT_CLEANUP_DRY_RUN", "SAR_SLOT_RETENTION_DAYS",
    "SAR_SLOT_RETENTION_COUNT", "SAR_SLOT_CLEANUP_LIMIT",
    "SAR_SLOT_STALE_ACTIVE_HOURS")) {
  Assert-Equal $true $wrapperText.Contains($setting) "Wrapper does not expose $setting."
}
Assert-Equal $true $wrapperText.Contains('if "%CLEANUP%"=="" set "CLEANUP=true"') `
    "Completed-slot cleanup is not enabled by default."

$nowUtc = [datetime]::UtcNow
$testRoot = Join-Path $env:TEMP "sar-winrm-retention-$([guid]::NewGuid().ToString('N'))"
try {
  New-Item -ItemType Directory -Path $testRoot | Out-Null
  $currentPath = Add-FinishedSlot $testRoot "current" $nowUtc.AddDays(-90)
  $oldPath = Add-FinishedSlot $testRoot "old" $nowUtc.AddDays(-60) "failure"
  $excessPath = Add-FinishedSlot $testRoot "excess" $nowUtc.AddDays(-20)
  Add-FinishedSlot $testRoot "recent-a" $nowUtc.AddDays(-5) | Out-Null
  Add-FinishedSlot $testRoot "recent-b" $nowUtc.AddDays(-10) | Out-Null
  $activePath = Add-FinishedSlot $testRoot "active" $nowUtc.AddDays(-80)
  Add-ActiveToken $testRoot "active" $nowUtc.AddHours(-1)

  $staleBusyPath = Add-FinishedSlot $testRoot "stale-busy" $nowUtc.AddDays(-80)
  Add-ActiveToken $testRoot "stale-busy" $nowUtc.AddHours(-48)
  $orphanPath = Add-FinishedSlot $testRoot "orphan" $nowUtc.AddDays(-45)
  Add-ActiveToken $testRoot "orphan" $nowUtc.AddHours(-48)

  $unmarkedPath = Join-Path $testRoot "sys-audio-router-unmarked"
  New-Item -ItemType Directory -Path $unmarkedPath | Out-Null
  $malformedPath = Join-Path $testRoot "sys-audio-router-malformed"
  New-Item -ItemType Directory -Path $malformedPath | Out-Null
  Set-Content -LiteralPath (Join-Path $malformedPath ".sar-slot-finished.json") `
      -Value '{"outcome":"unknown","finished_utc":"invalid"}' -Encoding ASCII
  New-Item -ItemType Directory -Path (Join-Path $testRoot "sys-audio-router") | Out-Null

  $matchingProcesses = [string[]]@((Get-Item -LiteralPath $staleBusyPath).FullName)
  $escapedBusyPath = [regex]::Escape(
      [IO.Path]::GetFullPath($staleBusyPath).TrimEnd([char[]]@('\', '/')))
  $busyPathPattern = "(?i)(?<![A-Za-z0-9_.-])$escapedBusyPath(?=`$|[\s`"'\\/])"
  Assert-Equal $true ($matchingProcesses[0] -match $busyPathPattern) `
      "The matching-process fixture '$($matchingProcesses[0])' does not satisfy '$busyPathPattern'."
  $selection = @(Get-FinishedSlotRetentionSelection `
      -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
      -RetentionDays 30 -RetentionCount 2 -CleanupLimit 10 -StaleActiveHours 24 `
      -ProcessInspectionSucceeded $true -ProcessCommandLines $matchingProcesses)

  Assert-Equal "sys-audio-router-old,sys-audio-router-orphan,sys-audio-router-excess" `
      ([string]::Join(",", $selection.Name)) "Retention selection was unsafe or incomplete."
  Assert-Equal $true ($selection.Name -contains "sys-audio-router-old") `
      "A completed failed slot was not eligible."
  Assert-Equal $false ($selection.Name -contains "sys-audio-router-current") `
      "The current slot became eligible."
  Assert-Equal $false ($selection.Name -contains "sys-audio-router-active") `
      "A slot with a fresh active token became eligible."
  Assert-Equal $false ($selection.Name -contains "sys-audio-router-stale-busy") `
      "A stale token with a matching process became eligible."
  Assert-Equal $false ($selection.Name -contains "sys-audio-router-unmarked") `
      "An unmarked directory became eligible."
  Assert-Equal $false ($selection.Name -contains "sys-audio-router-malformed") `
      "A malformed completion marker became eligible."

  $inspectionFailed = @(Get-FinishedSlotRetentionSelection `
      -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
      -RetentionDays 30 -RetentionCount 100 -CleanupLimit 10 -StaleActiveHours 24 `
      -ProcessInspectionSucceeded $false -ProcessCommandLines ([string[]]@()))
  Assert-Equal $false ($inspectionFailed.Name -contains "sys-audio-router-orphan") `
      "A stale token expired when process inspection failed."

  $limited = @(Get-FinishedSlotRetentionSelection `
      -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
      -RetentionDays 30 -RetentionCount 2 -CleanupLimit 1 -StaleActiveHours 24 `
      -ProcessInspectionSucceeded $true -ProcessCommandLines $matchingProcesses)
  Assert-Equal 1 $limited.Count "CleanupLimit was not enforced."
  Assert-Equal "sys-audio-router-old" $limited[0].Name `
      "CleanupLimit did not prefer the oldest eligible slot."

  (Get-Item -LiteralPath (Join-Path $testRoot ".sar-slot-active\stale-busy\run.active")).LastWriteTimeUtc =
      [datetime]::UtcNow
  $slotRoot = $testRoot
  $repoDir = $currentPath
  $RetentionDays = [uint32]30
  $RetentionCount = [uint32]2
  $CleanupLimit = [uint32]1
  $StaleActiveHours = [uint32]24
  Invoke-FinishedSlotRetention -DryRun
  Assert-Equal $true (Test-Path -LiteralPath $oldPath) "Dry-run removed a slot."
  Invoke-FinishedSlotRetention
  Assert-Equal $false (Test-Path -LiteralPath $oldPath) `
      "Live retention did not remove the oldest completed slot."
  Assert-Equal $true (Test-Path -LiteralPath $excessPath) `
      "Live retention exceeded CleanupLimit."
  Assert-Equal $true (Test-Path -LiteralPath $activePath) `
      "Live retention removed an active slot."
  Assert-Equal $true (Test-Path -LiteralPath $currentPath) `
      "Live retention removed the current slot."
  Assert-Equal $true (Test-Path -LiteralPath $unmarkedPath) `
      "Live retention removed an unmarked directory."
  Assert-Equal $true (Test-Path -LiteralPath $malformedPath) `
      "Live retention removed a directory with a malformed marker."
} finally {
  if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
  }
}

Write-Output "WinRM downloaded-source slot retention tests passed"
