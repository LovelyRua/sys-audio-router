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
    [string]$Name,
    [datetime]$FinishedUtc,
    [ValidateSet("success", "failure")]
    [string]$Outcome = "success"
  )

  $directory = New-Item -ItemType Directory -Path (Join-Path $Root $Name)
  @{ outcome = $Outcome; finished_utc = $FinishedUtc.ToString('o') } |
      ConvertTo-Json -Compress |
      Set-Content -LiteralPath (Join-Path $directory.FullName ".sar-slot-finished.json") -Encoding ASCII
  $marker = Get-Item -LiteralPath (Join-Path $directory.FullName ".sar-slot-finished.json")
  $marker.LastWriteTimeUtc = $FinishedUtc
  $directory.FullName
}

function Add-ActiveToken {
  param([string]$Root, [string]$SlotName, [datetime]$StartedUtc)

  $tokenDirectory = Join-Path $Root ".sar-slot-active\$SlotName"
  New-Item -ItemType Directory -Path $tokenDirectory -Force | Out-Null
  $token = New-Item -ItemType File -Path (Join-Path $tokenDirectory "run.active") -Force
  $token.LastWriteTimeUtc = $StartedUtc
}

function Import-RetentionFunctions {
  param([string]$ScriptPath)

  $tokens = $null
  $parseErrors = $null
  $ast = [System.Management.Automation.Language.Parser]::ParseFile(
      $ScriptPath, [ref]$tokens, [ref]$parseErrors)
  Assert-Equal 0 $parseErrors.Count "Unexpected parser errors in '$ScriptPath'."
  $scriptText = Get-Content -LiteralPath $ScriptPath -Raw
  Assert-Equal $true ($scriptText -match '(?s)catch\s*\{\s*\$slotOutcome = "failure"\s*throw\s*\}\s*finally') `
      "'$ScriptPath' does not preserve the original run failure through finalization."

  foreach ($functionName in @(
      "Invoke-WithSlotRetentionLock", "Get-FinishedSlotRetentionSelection",
      "Invoke-FinishedSlotRetention")) {
    $matches = @($ast.FindAll({
      param($node)
      $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
          $node.Name -eq $functionName
    }, $true))
    Assert-Equal 1 $matches.Count "Unexpected '$functionName' definition count in '$ScriptPath'."
    Set-Item -Path "function:script:$functionName" -Value $matches[0].Body.GetScriptBlock()
  }
}

$nowUtc = [datetime]::SpecifyKind([datetime]"2026-07-14T00:00:00", "Utc")
$scripts = @("windows-winrm-local-test.ps1", "windows-winrm-local-measure.ps1")

foreach ($scriptName in $scripts) {
  $testRoot = Join-Path $env:TEMP "sar-retention-$([guid]::NewGuid().ToString('N'))"
  try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    Import-RetentionFunctions -ScriptPath (Join-Path $PSScriptRoot $scriptName)

    $oldPath = Add-FinishedSlot $testRoot "sys-audio-router-old" $nowUtc.AddDays(-60) "failure"
    $excessPath = Add-FinishedSlot $testRoot "sys-audio-router-excess" $nowUtc.AddDays(-20)
    Add-FinishedSlot $testRoot "sys-audio-router-recent-a" $nowUtc.AddDays(-5) | Out-Null
    Add-FinishedSlot $testRoot "sys-audio-router-recent-b" $nowUtc.AddDays(-10) | Out-Null
    $activePath = Add-FinishedSlot $testRoot "sys-audio-router-active" $nowUtc.AddDays(-90) "failure"
    $currentPath = Add-FinishedSlot $testRoot "sys-audio-router-current" $nowUtc.AddDays(-90) "failure"
    Add-FinishedSlot $testRoot "not-sys-audio-router-invalid" $nowUtc.AddDays(-90) | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $testRoot "sys-audio-router-unmarked") | Out-Null
    $malformedPath = Join-Path $testRoot "sys-audio-router-malformed"
    New-Item -ItemType Directory -Path $malformedPath | Out-Null
    Set-Content -LiteralPath (Join-Path $malformedPath ".sar-slot-finished.json") `
        -Value '{"outcome":"unknown","finished_utc":"not-a-time"}' -Encoding ASCII

    Add-ActiveToken $testRoot "active" $nowUtc.AddHours(-1)
    $emptyCommandLines = [string[]]@()
    $oldMarker = Get-Content -LiteralPath (Join-Path $oldPath ".sar-slot-finished.json") -Raw |
        ConvertFrom-Json
    Assert-Equal "failure" $oldMarker.outcome "The failed-slot fixture lost its outcome."

    $selection = @(Get-FinishedSlotRetentionSelection `
        -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
        -RetentionDays 30 -RetentionCount 2 -CleanupLimit 10 -StaleActiveHours 24 `
        -ProcessInspectionSucceeded $true -ProcessCommandLines $emptyCommandLines)
    Assert-Equal "sys-audio-router-old,sys-audio-router-excess" `
        ([string]::Join(",", $selection.Name)) `
        "Age/count selection failed for '$scriptName'."
    Assert-Equal "age,count" $selection[0].Reason "Old-slot reasons were incomplete."
    Assert-Equal $true ($selection.Name -contains "sys-audio-router-old") `
        "A failed finished slot did not become eligible."
    Assert-Equal "count" $selection[1].Reason "Count reason was incorrect."
    Assert-Equal $false ($selection.Name -contains "sys-audio-router-active") `
        "An active slot was selected."
    Assert-Equal $false ($selection.Name -contains "sys-audio-router-current") `
        "The current slot was selected."
    Assert-Equal $false ($selection.Name -contains "sys-audio-router-malformed") `
        "A malformed finished marker was selected."

    $limited = @(Get-FinishedSlotRetentionSelection `
        -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
        -RetentionDays 30 -RetentionCount 2 -CleanupLimit 1 -StaleActiveHours 24 `
        -ProcessInspectionSucceeded $true -ProcessCommandLines $emptyCommandLines)
    Assert-Equal 1 $limited.Count "Cleanup limit was not enforced."
    Assert-Equal "sys-audio-router-old" $limited[0].Name `
        "Cleanup limit should prefer the oldest completed slot."

    $slotRoot = $testRoot
    $repoDir = $currentPath
    $RetentionDays = [uint32]30
    $RetentionCount = [uint32]2
    $CleanupLimit = [uint32]1
    $StaleActiveHours = [uint32]24
    Invoke-FinishedSlotRetention -DryRun
    Assert-Equal $true (Test-Path -LiteralPath $oldPath) "Dry-run removed a slot."
    Invoke-FinishedSlotRetention
    Assert-Equal $false (Test-Path -LiteralPath $oldPath) "Live cleanup did not remove the selected slot."
    Assert-Equal $true (Test-Path -LiteralPath $excessPath) "Cleanup exceeded its per-run limit."
    Assert-Equal $true (Test-Path -LiteralPath $activePath) "Cleanup removed an active slot."
    Assert-Equal $true (Test-Path -LiteralPath $currentPath) "Cleanup removed the current slot."

    $orphanPath = Add-FinishedSlot $testRoot "sys-audio-router-orphan" $nowUtc.AddDays(-45) "failure"
    Add-ActiveToken $testRoot "orphan" $nowUtc.AddHours(-48)
    $matchingPath = Add-FinishedSlot $testRoot "sys-audio-router-building" $nowUtc.AddDays(-45) "failure"
    Add-ActiveToken $testRoot "building" $nowUtc.AddHours(-48)
    $matchingCommandLines = [string[]]@("cmake --build `"$matchingPath\build-building`"")

    $staleSelection = @(Get-FinishedSlotRetentionSelection `
        -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
        -RetentionDays 30 -RetentionCount 100 -CleanupLimit 10 -StaleActiveHours 24 `
        -ProcessInspectionSucceeded $true -ProcessCommandLines $matchingCommandLines)
    Assert-Equal $true ($staleSelection.Name -contains "sys-audio-router-orphan") `
        "A stale orphan token without a matching process did not expire."
    Assert-Equal $false ($staleSelection.Name -contains "sys-audio-router-building") `
        "A stale token with an exact-path process match became eligible."

    $inspectionFailedSelection = @(Get-FinishedSlotRetentionSelection `
        -SlotRoot $testRoot -CurrentRepoDir $currentPath -NowUtc $nowUtc `
        -RetentionDays 30 -RetentionCount 100 -CleanupLimit 10 -StaleActiveHours 24 `
        -ProcessInspectionSucceeded $false -ProcessCommandLines $emptyCommandLines)
    Assert-Equal $false ($inspectionFailedSelection.Name -contains "sys-audio-router-orphan") `
        "A stale token expired when process inspection failed."
  } finally {
    if (Test-Path -LiteralPath $testRoot) {
      Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
  }
}

foreach ($wrapperName in @("windows-winrm-local-test.cmd", "windows-winrm-local-measure.cmd")) {
  $wrapperText = Get-Content -LiteralPath (Join-Path $PSScriptRoot $wrapperName) -Raw
  foreach ($settingName in @(
      "SAR_SLOT_CLEANUP", "SAR_SLOT_CLEANUP_DRY_RUN", "SAR_SLOT_RETENTION_DAYS",
      "SAR_SLOT_RETENTION_COUNT", "SAR_SLOT_CLEANUP_LIMIT",
      "SAR_SLOT_STALE_ACTIVE_HOURS")) {
    Assert-Equal $true $wrapperText.Contains($settingName) `
        "Wrapper '$wrapperName' does not expose $settingName."
  }
  foreach ($argumentName in @(
      "CleanupCompletedSlotsText", "CleanupDryRunText", "RetentionDays",
      "RetentionCount", "CleanupLimit", "StaleActiveHours")) {
    Assert-Equal $true $wrapperText.Contains("-$argumentName") `
        "Wrapper '$wrapperName' does not pass -$argumentName."
  }
}

$archiveNamespaces = @{
  "windows-winrm-local-test.ps1" = "lt"
  "windows-winrm-local-measure.ps1" = "lm"
}
foreach ($entry in $archiveNamespaces.GetEnumerator()) {
  $scriptText = Get-Content -LiteralPath (Join-Path $PSScriptRoot $entry.Key) -Raw
  Assert-Equal $true $scriptText.Contains(
      "`$slotKey = `"$($entry.Value)-`$PathSlot`"") `
      "Archive workflow '$($entry.Key)' does not namespace its remote slot."
  Assert-Equal $true $scriptText.Contains(
      '"sys-audio-router-$slotKey"') `
      "Archive workflow '$($entry.Key)' does not namespace its repository directory."
  Assert-Equal $true $scriptText.Contains('"b-$PathSlot"') `
      "Archive workflow '$($entry.Key)' does not use a bounded build directory."
  Assert-Equal $true $scriptText.Contains('".sar-slot-active\$slotKey"') `
      "Archive workflow '$($entry.Key)' does not namespace its active token."
  Assert-Equal $true $scriptText.Contains('$slotHash.Substring(0, 12)') `
      "Archive workflow '$($entry.Key)' does not bound slot path length."
}

Write-Output "WinRM slot retention tests passed"
