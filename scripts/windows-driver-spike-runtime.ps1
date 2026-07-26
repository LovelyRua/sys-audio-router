[CmdletBinding(SupportsShouldProcess = $true)]
[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$SamplesRoot = "$env:USERPROFILE\src\windows-driver-samples-acx",
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

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  return $principal.IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Require-File {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Description
  )

  if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "$Description was not found at '$Path'."
  }
  return (Get-Item -LiteralPath $Path).FullName
}

function Get-LatestDevcon {
  $toolsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Tools"
  $candidates = @(Get-ChildItem -LiteralPath $toolsRoot -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName "$Platform\devcon.exe" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
  if ($candidates.Count -eq 0) {
    throw "No $Platform DevCon executable was found under '$toolsRoot'."
  }
  return $candidates[0]
}

function Invoke-NativeCommand {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [Parameter(Mandatory = $true)][string[]]$Arguments,
    [int[]]$AllowedExitCodes = @(0)
  )

  Write-Host "> $FilePath $($Arguments -join ' ')"
  $previousErrorActionPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    $output = @(& $FilePath @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  $output | ForEach-Object { Write-Host $_ }
  if ($AllowedExitCodes -notcontains $exitCode) {
    throw "'$FilePath' failed with exit code $exitCode."
  }
  return $exitCode
}

function Get-TargetDevices {
  param([Parameter(Mandatory = $true)][string]$HardwareId)

  return @(Get-CimInstance Win32_PnPEntity | Where-Object {
    $ids = @($_.HardwareID)
    $ids -contains $HardwareId
  })
}

function Get-DevicePropertyValue {
  param(
    [Parameter(Mandatory = $true)][string]$InstanceId,
    [Parameter(Mandatory = $true)][string]$KeyName
  )

  try {
    return (Get-PnpDeviceProperty -InstanceId $InstanceId `
        -KeyName $KeyName -ErrorAction Stop).Data
  } catch {
    return $null
  }
}

function Format-PropertyValue {
  param([AllowNull()]$Value)

  if ($null -eq $Value) {
    return "<unavailable>"
  }
  if ($Value -is [array]) {
    return ($Value -join ", ")
  }
  return [string]$Value
}

function Write-RuntimeStatus {
  param(
    [Parameter(Mandatory = $true)][string]$HardwareId,
    [Parameter(Mandatory = $true)][string]$ServiceName,
    [Parameter(Mandatory = $true)][string]$Devcon
  )

  Write-Host ""
  Write-Host "=== Device nodes ==="
  $devices = @(Get-TargetDevices -HardwareId $HardwareId)
  if ($devices.Count -eq 0) {
    Write-Host "No device node has hardware ID '$HardwareId'."
  }

  $propertyKeys = @(
    "DEVPKEY_Device_ProblemCode",
    "DEVPKEY_Device_ProblemStatus",
    "DEVPKEY_Device_Service",
    "DEVPKEY_Device_Driver",
    "DEVPKEY_Device_DriverVersion",
    "DEVPKEY_Device_DriverDate"
  )
  foreach ($device in $devices) {
    Write-Host "Instance: $($device.PNPDeviceID)"
    Write-Host "Name:     $($device.Name)"
    Write-Host "Status:   $($device.Status)"
    foreach ($key in $propertyKeys) {
      $value = Get-DevicePropertyValue -InstanceId $device.PNPDeviceID `
          -KeyName $key
      Write-Host ("{0}: {1}" -f $key, (Format-PropertyValue $value))
    }
    Write-Host ""
  }

  Invoke-NativeCommand -FilePath $Devcon -Arguments @("status", $HardwareId) `
      -AllowedExitCodes @(0, 1, 2) | Out-Null

  Write-Host ""
  Write-Host "=== Driver service ==="
  $service = Get-CimInstance Win32_SystemDriver -Filter `
      "Name='$ServiceName'" -ErrorAction SilentlyContinue
  if ($null -eq $service) {
    Write-Host "Service '$ServiceName' is not installed."
  } else {
    $service | Select-Object Name, State, StartMode, Status, PathName |
        Format-List | Out-Host
  }

  Write-Host "=== Audio endpoints ==="
  $endpoints = @(Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue |
    Sort-Object FriendlyName)
  if ($endpoints.Count -eq 0) {
    Write-Host "No AudioEndpoint devices are present."
  } else {
    $endpoints | Select-Object Status, FriendlyName, InstanceId |
        Format-Table -AutoSize | Out-Host
  }

  Write-Host "=== Restart state ==="
  $restartState = [pscustomobject]@{
    CbsRebootPending = Test-Path -LiteralPath `
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending"
    WindowsUpdateRebootRequired = Test-Path -LiteralPath `
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired"
    PendingFileRenameOperations = [bool](Get-ItemProperty -LiteralPath `
        "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager" `
        -Name PendingFileRenameOperations -ErrorAction SilentlyContinue)
  }
  $restartState | Format-List | Out-Host

  Write-Host "=== Relevant system events (last 60 minutes) ==="
  $pattern = [regex]::Escape($HardwareId) + `
      "|" + [regex]::Escape($ServiceName) + "|ComponentizedAudio|sysvad"
  $events = @(Get-WinEvent -FilterHashtable @{
      LogName = "System"
      StartTime = (Get-Date).AddMinutes(-60)
    } -ErrorAction SilentlyContinue | Where-Object {
      $_.ProviderName -in @(
        "Microsoft-Windows-Kernel-PnP",
        "Microsoft-Windows-UserPnp",
        "Service Control Manager"
      ) -and $_.Message -match $pattern
    } | Select-Object -First 30 TimeCreated, Id, LevelDisplayName,
        ProviderName, Message)
  if ($events.Count -eq 0) {
    Write-Host "No matching events were found."
  } else {
    $events | Format-List | Out-Host
  }
}

function Get-InstalledInfNames {
  param([Parameter(Mandatory = $true)][string[]]$OriginalInfNames)

  $nameSet = @{}
  foreach ($name in $OriginalInfNames) {
    $nameSet[$name.ToLowerInvariant()] = $true
  }
  return @(Get-WindowsDriver -Online | Where-Object {
    $originalName = Split-Path -Leaf $_.OriginalFileName
    $nameSet.ContainsKey($originalName.ToLowerInvariant())
  } | Select-Object -ExpandProperty Driver -Unique)
}

$samplesRootFull = [IO.Path]::GetFullPath($SamplesRoot)
if ($Target -eq "Acx") {
  $packageRoot = Join-Path $samplesRootFull `
      "audio\Acx\Samples\AudioCodec\Driver\$Platform\$Configuration\AudioCodec"
  $certificate = Join-Path $packageRoot "AudioCodec.cer"
  $infNames = @("AudioCodec.inf")
  $hardwareId = "ROOT\AudioCodec"
  $serviceName = "AudioCodec"
} else {
  $packageRoot = Join-Path $samplesRootFull `
      "audio\sysvad\$Platform\$Configuration\package"
  $certificate = Join-Path (Split-Path -Parent $packageRoot) "package.cer"
  $infNames = @(
    "ComponentizedApoSample.inf",
    "ComponentizedAudioSample.inf",
    "ComponentizedAudioSampleExtension.inf"
  )
  $hardwareId = "Root\sysvad_ComponentizedAudioSample"
  $serviceName = "TabletAudioSample"
}

$devcon = Get-LatestDevcon
Write-Host "Windows driver spike runtime"
Write-Host "Action:   $Action"
Write-Host "Target:   $Target"
Write-Host "Package:  $packageRoot"
Write-Host "Hardware: $hardwareId"
Write-Host "DevCon:   $devcon"

if ($Action -eq "Status") {
  Write-RuntimeStatus -HardwareId $hardwareId -ServiceName $serviceName `
      -Devcon $devcon
  exit 0
}

if (!(Test-IsAdministrator)) {
  throw "Action '$Action' requires an elevated PowerShell process."
}

$pnputil = Join-Path $env:SystemRoot "System32\pnputil.exe"
Require-File -Path $pnputil -Description "PnPUtil" | Out-Null

if ($Action -eq "Install") {
  Require-File -Path $certificate -Description "$Target test certificate" |
      Out-Null
  $infPaths = @($infNames | ForEach-Object {
    Require-File -Path (Join-Path $packageRoot $_) `
        -Description "$Target package INF"
  })

  if ($PSCmdlet.ShouldProcess($Target, "trust the package certificate")) {
    Import-Certificate -FilePath $certificate `
        -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
    Import-Certificate -FilePath $certificate `
        -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" | Out-Null
  }
  foreach ($infPath in $infPaths) {
    if ($PSCmdlet.ShouldProcess($infPath, "stage and install driver package")) {
      Invoke-NativeCommand -FilePath $pnputil `
          -Arguments @("/add-driver", $infPath, "/install") `
          -AllowedExitCodes @(0, 259, 3010) | Out-Null
    }
  }

  $mainInf = $infPaths | Where-Object {
    (Split-Path -Leaf $_) -in @(
      "AudioCodec.inf",
      "ComponentizedAudioSample.inf"
    )
  } | Select-Object -First 1
  $devices = @(Get-TargetDevices -HardwareId $hardwareId)
  if ($devices.Count -eq 0) {
    if ($PSCmdlet.ShouldProcess($hardwareId, "create root device node")) {
      $devconExit = Invoke-NativeCommand -FilePath $devcon `
          -Arguments @("install", $mainInf, $hardwareId) `
          -AllowedExitCodes @(0, 1)
    }
  } else {
    if ($PSCmdlet.ShouldProcess($hardwareId, "update existing device node")) {
      $devconExit = Invoke-NativeCommand -FilePath $devcon `
          -Arguments @("update", $mainInf, $hardwareId) `
          -AllowedExitCodes @(0, 1)
    }
  }

  Write-RuntimeStatus -HardwareId $hardwareId -ServiceName $serviceName `
      -Devcon $devcon
  if ($Restart -or $devconExit -eq 1) {
    Write-Host "Restarting Windows to complete driver activation."
    Restart-Computer -Force
  }
  exit 0
}

$devices = @(Get-TargetDevices -HardwareId $hardwareId)
foreach ($device in $devices) {
  if ($PSCmdlet.ShouldProcess($device.PNPDeviceID, "remove device node")) {
    Invoke-NativeCommand -FilePath $devcon `
        -Arguments @("remove", "@$($device.PNPDeviceID)") `
        -AllowedExitCodes @(0, 1) | Out-Null
  }
}

$publishedInfNames = @(Get-InstalledInfNames -OriginalInfNames $infNames)
foreach ($publishedInfName in $publishedInfNames) {
  if ($PSCmdlet.ShouldProcess($publishedInfName, "delete driver package")) {
    Invoke-NativeCommand -FilePath $pnputil `
        -Arguments @("/delete-driver", $publishedInfName, "/uninstall", "/force") `
        -AllowedExitCodes @(0, 259, 3010) | Out-Null
  }
}

if ($RemoveCertificate -and (Test-Path -LiteralPath $certificate -PathType Leaf)) {
  $thumbprint = (Get-PfxCertificate -FilePath $certificate).Thumbprint
  foreach ($store in @("Root", "TrustedPublisher")) {
    $certificatePath = "Cert:\LocalMachine\$store\$thumbprint"
    if ((Test-Path -LiteralPath $certificatePath) -and
        $PSCmdlet.ShouldProcess($certificatePath, "remove test certificate")) {
      Remove-Item -LiteralPath $certificatePath -Force
    }
  }
}

Write-RuntimeStatus -HardwareId $hardwareId -ServiceName $serviceName `
    -Devcon $devcon
if ($Restart) {
  Write-Host "Restarting Windows to complete driver removal."
  Restart-Computer -Force
}
