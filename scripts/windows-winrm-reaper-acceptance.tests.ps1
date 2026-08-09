$ErrorActionPreference = "Stop"

function Assert-True {
  param([bool]$Condition, [string]$Message)
  if (!$Condition) { throw $Message }
}

function Assert-Contains {
  param([string]$Text, [string]$Needle, [string]$Message)
  Assert-True $Text.Contains($Needle) $Message
}

$scriptPath = Join-Path $PSScriptRoot "windows-winrm-reaper-acceptance.ps1"
$cmdPath = Join-Path $PSScriptRoot "windows-winrm-reaper-acceptance.cmd"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$parseErrors)
Assert-True ($parseErrors.Count -eq 0) `
    "Unexpected parser errors in '$scriptPath': $($parseErrors -join '; ')"

$parameters = $ast.ParamBlock.Parameters.Name.VariablePath.UserPath
foreach ($parameter in @(
    "EvidenceDirectory", "RecoverUntrackedProcesses", "ClientCount",
    "DurationSeconds")) {
  Assert-True ($parameters -contains $parameter) `
      "Acceptance parameter '$parameter' is missing."
}

$scriptText = Get-Content -LiteralPath $scriptPath -Raw
foreach ($artifact in @(
    "preflight.json", "manifest.json", "diagnostics-initial.json",
    "diagnostics-final.json", "result.json", "local-failure.json")) {
  Assert-Contains $scriptText $artifact "Evidence artifact '$artifact' is missing."
}
Assert-Contains $scriptText '"interactive_user_missing"' `
    "Missing interactive desktop must have a stable block code."
Assert-Contains $scriptText '"untracked_processes"' `
    "Untracked process conflicts must have a stable block code."
Assert-Contains $scriptText "Save-RunState" `
    "The acceptance run does not persist owned task/process state."
Assert-Contains $scriptText "Stop-OwnedProcess" `
    "Stale process recovery is not ownership-aware."
Assert-Contains $scriptText "started_utc" `
    "PID reuse protection must validate the recorded process start time."
Assert-Contains $scriptText "Copy-Item -FromSession" `
    "Remote evidence is not copied back to the invoking machine."
Assert-Contains $scriptText 'Remove-Item -LiteralPath $StatePath' `
    "Successful cleanup does not remove the resumable state file."

$saveIndex = $scriptText.IndexOf("Save-RunState @($engineTask)")
$startIndex = $scriptText.IndexOf("Start-ScheduledTask -TaskName $engineTask")
Assert-True ($saveIndex -ge 0 -and $saveIndex -lt $startIndex) `
    "Engine task ownership must be saved before it is started."

$cmdText = Get-Content -LiteralPath $cmdPath -Raw
Assert-Contains $cmdText 'set "CLIENT_COUNT=2"' `
    "The wrapper must default to the dual-client acceptance."
Assert-Contains $cmdText "SAR_REAPER_RECOVER_UNTRACKED" `
    "The wrapper does not expose explicit untracked-process recovery."
Assert-Contains $cmdText "SAR_REAPER_EVIDENCE_DIR" `
    "The wrapper does not expose evidence placement."
Assert-Contains $scriptText "reaper_asio_preflight status=" `
    "The preflight does not emit a stable automation summary."

Write-Output "REAPER acceptance script tests passed"
