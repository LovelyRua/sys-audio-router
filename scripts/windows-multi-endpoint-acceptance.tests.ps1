$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Assert-True {
  param([bool]$Condition, [string]$Message)
  if (!$Condition) { throw $Message }
}

$localPath = Join-Path $PSScriptRoot "windows-multi-endpoint-acceptance.ps1"
$winrmPath = Join-Path $PSScriptRoot "windows-winrm-multi-endpoint-acceptance.ps1"
$modulePath = Join-Path $PSScriptRoot "windows-winrm-preflight.psm1"
$cmdPath = Join-Path $PSScriptRoot "windows-winrm-multi-endpoint-acceptance.cmd"

foreach ($path in @($localPath, $winrmPath, $modulePath)) {
  $tokens = $null
  $errors = $null
  $null = [Management.Automation.Language.Parser]::ParseFile(
      $path, [ref]$tokens, [ref]$errors)
  Assert-True ($errors.Count -eq 0) "$path has PowerShell parse errors"
}

$localText = Get-Content -LiteralPath $localPath -Raw
$winrmText = Get-Content -LiteralPath $winrmPath -Raw
$moduleText = Get-Content -LiteralPath $modulePath -Raw
$cmdText = Get-Content -LiteralPath $cmdPath -Raw

Assert-True ($localText -match 'runtime-configure-matrix') "local gate does not configure matrix mode"
Assert-True ($localText -match '"capture", "capture-a"') "capture-a endpoint is missing"
Assert-True ($localText -match '"capture", "capture-b"') "capture-b endpoint is missing"
Assert-True ($localText -match '"render", "render-main"') "render-main endpoint is missing"
Assert-True ($localText -match 'session-running\.sars') "running session snapshot is missing"
Assert-True ($localText -match 'session-restored\.sars') "restored session snapshot is missing"
Assert-True ($localText -match 'diagnostics-before') "initial diagnostics evidence is missing"
Assert-True ($localText -match 'diagnostics-restored') "restored diagnostics evidence is missing"
Assert-True ($localText -match 'Stop-OwnedService') "owned-process cleanup is missing"
Assert-True ($winrmText -match 'Test-WSMan') "WinRM wrapper does not perform WSMan preflight"
Assert-True ($winrmText -match 'FileShare\]::None') "WinRM slot lock is missing"
Assert-True ($winrmText -match 'LogonType Interactive') "WinRM wrapper does not enter the interactive audio session"
Assert-True ($winrmText -match "Name='explorer\.exe'") "WinRM wrapper does not identify the interactive user"
Assert-True ($winrmText -match 'interactive_user_mismatch') "WinRM/desktop user mismatch guard is missing"
Assert-True ($winrmText -match 'Copy-Item -FromSession') "remote evidence collection is missing"
Assert-True ($moduleText -match 'winrm_http_invalid_server_response_12152') "12152 block code is missing"
Assert-True ($moduleText -match 'winrm enumerate winrm/config/listener') "12152 listener action is missing"
Assert-True ($cmdText -match 'SAR_MULTI_ENDPOINT_EVIDENCE_DIR') "CMD evidence override is missing"
Assert-True ($cmdText -match 'SAR_MULTI_ENDPOINT_INTERACTIVE_USER') "CMD interactive-user override is missing"

$allText = [string]::Join("`n", @($localText, $winrmText, $moduleText, $cmdText))
Assert-True ($allText -notmatch '(?i)winget\s+install|choco\s+install|Install-Module|msiexec') `
    "acceptance flow must not install software on the development host"

Import-Module $modulePath -Force
$fakeException = [Exception]::new("The WinRM client received HTTP error code 12152")
$fakeError = [Management.Automation.ErrorRecord]::new(
    $fakeException, "WinRm12152", [Management.Automation.ErrorCategory]::ConnectionError, $null)
$diagnosis = Get-SarWinRmFailureDiagnosis -ErrorRecord $fakeError `
    -HostName "audio-test.invalid" -Port 5985 -TcpReachable $true
Assert-True ($diagnosis.block_code -eq "winrm_http_invalid_server_response_12152") `
    "12152 message was not classified correctly"
Assert-True ($diagnosis.tcp_reachable) "12152 diagnosis lost TCP reachability"
Assert-True ($diagnosis.action -match 'Test-WSMan audio-test\.invalid') `
    "12152 diagnosis does not provide a retry command"

Write-Output "windows_multi_endpoint_acceptance_tests passed=1"
