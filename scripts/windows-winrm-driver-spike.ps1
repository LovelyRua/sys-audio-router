[CmdletBinding()]
param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$SamplesRoot = "",
  [ValidateSet("Acx", "Sysvad")]
  [string]$Target = "Sysvad",
  [ValidateSet("Status", "Install", "Remove")]
  [string]$Action = "Status",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [ValidateSet("x64", "Win32", "ARM64")]
  [string]$Platform = "x64",
  [switch]$Restart,
  [switch]$RemoveCertificate
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$runtimeScript = Join-Path $PSScriptRoot "windows-driver-spike-runtime.ps1"
if (!(Test-Path -LiteralPath $runtimeScript -PathType Leaf)) {
  throw "Driver runtime script was not found at '$runtimeScript'."
}
$runtimeScriptText = Get-Content -LiteralPath $runtimeScript -Raw
$runtimeParameters = @{
  Target = $Target
  Action = $Action
  Configuration = $Configuration
  Platform = $Platform
  Restart = [bool]$Restart
  RemoveCertificate = [bool]$RemoveCertificate
}
if (![string]::IsNullOrWhiteSpace($SamplesRoot)) {
  $runtimeParameters.SamplesRoot = $SamplesRoot
}

$session = $null
try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  Invoke-Command -Session $session -ArgumentList `
      $runtimeScriptText, $runtimeParameters -ScriptBlock {
    param(
      [Parameter(Mandatory = $true)][string]$ScriptText,
      [Parameter(Mandatory = $true)][hashtable]$Parameters
    )

    $runtime = [scriptblock]::Create($ScriptText)
    & $runtime @Parameters
  }
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
