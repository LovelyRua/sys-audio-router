[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackagePath,
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "engineer-a",
  [ValidateRange(1, 30)]
  [int]$GuiHealthSeconds = 4
)

$ErrorActionPreference = "Stop"

$resolvedPackage = [IO.Path]::GetFullPath($PackagePath.Trim().Trim('"'))
if (!(Test-Path -LiteralPath $resolvedPackage -PathType Leaf)) {
  throw "Alpha package was not found: $resolvedPackage"
}
if ([IO.Path]::GetExtension($resolvedPackage) -ne ".zip") {
  throw "Alpha package must be a ZIP file."
}

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}

$credentialUserName = $UserName
if ($credentialUserName -notmatch '[\\@]') {
  $credentialUserName = ".\$credentialUserName"
}
$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($credentialUserName, $securePassword)
$acceptanceScript = Join-Path $PSScriptRoot "windows-alpha-package-acceptance.ps1"
if (!(Test-Path -LiteralPath $acceptanceScript -PathType Leaf)) {
  throw "Local alpha package acceptance script was not found."
}

$session = $null
$remoteStage = $null
try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $remoteStage = Invoke-Command -Session $session -ArgumentList $safeSlot -ScriptBlock {
    param([string]$SafeSlot)
    $path = Join-Path $env:TEMP "sar-alpha-package-$SafeSlot"
    if (Test-Path -LiteralPath $path) {
      throw "Remote package slot '$SafeSlot' is already staged."
    }
    New-Item -ItemType Directory -Path $path | Out-Null
    $path
  }

  $remotePackage = Join-Path $remoteStage ([IO.Path]::GetFileName($resolvedPackage))
  $remoteScript = Join-Path $remoteStage "windows-alpha-package-acceptance.ps1"
  Copy-Item -LiteralPath $resolvedPackage -Destination $remotePackage `
      -ToSession $session
  Copy-Item -LiteralPath $acceptanceScript -Destination $remoteScript `
      -ToSession $session

  Invoke-Command -Session $session -ArgumentList `
      $remotePackage, $remoteScript, $safeSlot, $GuiHealthSeconds -ScriptBlock {
    param(
      [string]$RemotePackage,
      [string]$RemoteScript,
      [string]$SafeSlot,
      [int]$GuiHealthSeconds
    )

    $ErrorActionPreference = "Stop"
    $lockPath = Join-Path $env:TEMP "sar-alpha-package-acceptance.lock"
    $lock = $null
    $registrationBackup = Join-Path (Split-Path -Parent $RemoteScript) `
        "registration-backup"
    $registrationKeys = @(
      "HKCU\Software\ASIO\System Audio Route",
      "HKCU\Software\Classes\CLSID\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}"
    )
    $savedRegistration = @()
    try {
      try {
        $lock = [IO.File]::Open(
            $lockPath, [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
      } catch [IO.IOException] {
        throw "Another alpha package acceptance run is active on $env:COMPUTERNAME."
      }

      $activeDaws = @(Get-Process -Name "reaper", "Cakewalk" `
          -ErrorAction SilentlyContinue)
      if ($activeDaws.Count -ne 0) {
        throw "Close REAPER and Cakewalk before package acceptance changes ASIO registration."
      }

      New-Item -ItemType Directory -Path $registrationBackup | Out-Null
      for ($index = 0; $index -lt $registrationKeys.Count; ++$index) {
        $key = $registrationKeys[$index]
        if (Test-Path -LiteralPath "Registry::$key") {
          $backup = Join-Path $registrationBackup "registration-$index.reg"
          $export = Start-Process -FilePath "$env:SystemRoot\System32\reg.exe" `
              -ArgumentList "export `"$key`" `"$backup`" /y" `
              -WindowStyle Hidden -Wait -PassThru
          if ($export.ExitCode -ne 0) {
            throw "Could not back up existing registration key '$key'."
          }
          $savedRegistration += $backup
        }
      }

      try {
        foreach ($key in $registrationKeys) {
          Remove-Item -LiteralPath "Registry::$key" -Recurse -Force `
              -ErrorAction SilentlyContinue
        }

        $installPath = Join-Path $env:LOCALAPPDATA `
            "Programs\SystemAudioRoute-Alpha-Acceptance-$SafeSlot"
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $RemoteScript `
            -PackagePath $RemotePackage -InstallDirectory $installPath `
            -GuiHealthSeconds $GuiHealthSeconds
        if ($LASTEXITCODE -ne 0) {
          throw "Remote alpha package acceptance failed with exit code $LASTEXITCODE."
        }
      } finally {
        foreach ($key in $registrationKeys) {
          Remove-Item -LiteralPath "Registry::$key" -Recurse -Force `
              -ErrorAction SilentlyContinue
        }
        foreach ($backup in $savedRegistration) {
          $import = Start-Process -FilePath "$env:SystemRoot\System32\reg.exe" `
              -ArgumentList "import `"$backup`"" -WindowStyle Hidden `
              -Wait -PassThru
          if ($import.ExitCode -ne 0) {
            throw "Could not restore existing Virtual ASIO registration from '$backup'."
          }
        }
      }
      Write-Output "winrm_alpha_package_acceptance passed=1 slot=$SafeSlot registration_preserved=1"
    } finally {
      if ($null -ne $lock) {
        $lock.Dispose()
      }
    }
  }
} finally {
  if ($null -ne $session) {
    if ($null -ne $remoteStage) {
      Invoke-Command -Session $session -ArgumentList $remoteStage -ScriptBlock {
        param([string]$RemoteStage)
        $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
        $stagePath = [IO.Path]::GetFullPath($RemoteStage)
        if ($stagePath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
            [IO.Path]::GetFileName($stagePath) -match '^sar-alpha-package-[A-Za-z0-9_.-]+$') {
          Remove-Item -LiteralPath $stagePath -Recurse -Force -ErrorAction SilentlyContinue
        }
      } -ErrorAction SilentlyContinue
    }
    Remove-PSSession $session
  }
}
