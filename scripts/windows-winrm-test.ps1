param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = ""
)

$ErrorActionPreference = "Stop"

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

  Invoke-Command -Session $session -ArgumentList $safeSlot -ScriptBlock {
    param([string]$SafeSlot)

    $ErrorActionPreference = "Continue"
    if ([string]::IsNullOrWhiteSpace($SafeSlot)) {
      $bootstrap = "C:\Windows\Temp\sar-bootstrap.cmd"
      $repoDir = ""
      $buildDir = ""
    } else {
      $bootstrap = "C:\Windows\Temp\sar-bootstrap-$SafeSlot.cmd"
      $repoDir = Join-Path $env:USERPROFILE "src\sys-audio-router-$SafeSlot"
      $buildDir = "build-$SafeSlot"
    }
    $url = "https://raw.githubusercontent.com/LovelyRua/sys-audio-router/main/scripts/windows-test-bootstrap.cmd"

    curl.exe --silent --show-error -L $url -o $bootstrap
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to download bootstrap with exit code $LASTEXITCODE."
    }

    if ([string]::IsNullOrWhiteSpace($SafeSlot)) {
      cmd.exe /c "$bootstrap 2>&1"
    } else {
      cmd.exe /c "`"$bootstrap`" `"$repoDir`" `"$buildDir`" 2>&1"
    }
    if ($LASTEXITCODE -ne 0) {
      throw "Bootstrap failed with exit code $LASTEXITCODE."
    }
  }
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
