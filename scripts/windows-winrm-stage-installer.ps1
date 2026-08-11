[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackagePath,
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$DestinationName = "SystemAudioRoute-latest.exe"
)

$ErrorActionPreference = "Stop"
$package = [IO.Path]::GetFullPath($PackagePath.Trim().Trim('"'))
if (!(Test-Path -LiteralPath $package -PathType Leaf) -or
    [IO.Path]::GetExtension($package) -ne ".exe") {
  throw "Installer package was not found: $package"
}
if ([IO.Path]::GetFileName($DestinationName) -ne $DestinationName -or
    [IO.Path]::GetExtension($DestinationName) -ne ".exe") {
  throw "DestinationName must be a plain .exe file name."
}

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$credential = [pscredential]::new(
    $credentialUserName,
    (ConvertTo-SecureString $Password -AsPlainText -Force))
$session = $null
try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $destination = Invoke-Command -Session $session -ArgumentList `
      $UserName, $DestinationName -ScriptBlock {
    param([string]$RequestedUser, [string]$FileName)

    $explorer = @(Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
        Where-Object {
          $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwner
          $owner.ReturnValue -eq 0 -and $owner.User -ieq $RequestedUser
        })
    if ($explorer.Count -ne 1) {
      throw "Expected exactly one interactive Explorer session for '$RequestedUser'."
    }
    $owner = Invoke-CimMethod -InputObject $explorer[0] -MethodName GetOwner
    $account = [Security.Principal.NTAccount]::new(
        "$($owner.Domain)\$($owner.User)")
    $sid = $account.Translate([Security.Principal.SecurityIdentifier]).Value
    $profile = Get-CimInstance Win32_UserProfile -Filter "SID='$sid'" |
        Select-Object -First 1
    if ($null -eq $profile -or [string]::IsNullOrWhiteSpace($profile.LocalPath)) {
      throw "Could not resolve the interactive user's profile."
    }
    $destination = Join-Path (Join-Path $profile.LocalPath "Desktop") $FileName
    if (Test-Path -LiteralPath $destination) {
      throw "Destination already exists: $destination"
    }
    $destination
  }
  Copy-Item -LiteralPath $package -Destination $destination -ToSession $session
  $remoteHash = Invoke-Command -Session $session -ArgumentList $destination `
      -ScriptBlock {
    param([string]$Path)
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
  }
  $localHash = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
  if ($remoteHash -ne $localHash) {
    throw "Staged installer hash mismatch."
  }
  Write-Output "installer_staged path=`"$destination`" sha256=$localHash"
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
