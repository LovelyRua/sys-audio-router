[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackagePath,

  [string]$InstallDirectory = "",

  [ValidateRange(1, 30)]
  [int]$GuiHealthSeconds = 4
)

$ErrorActionPreference = "Stop"

function Invoke-NsisExecutable {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [string[]]$Argument
  )

  $process = Start-Process -FilePath $Path -ArgumentList $Argument `
      -Wait -PassThru
  if ($process.ExitCode -ne 0) {
    throw "'$Path' failed with exit code $($process.ExitCode)."
  }
}

function Get-InstalledProcess {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [Parameter(Mandatory = $true)]
    [string]$Directory
  )

  $prefix = [IO.Path]::GetFullPath($Directory).TrimEnd(
      [IO.Path]::DirectorySeparatorChar,
      [IO.Path]::AltDirectorySeparatorChar) +
      [IO.Path]::DirectorySeparatorChar
  return @(Get-Process -Name $Name -ErrorAction SilentlyContinue |
      Where-Object {
        try {
          [IO.Path]::GetFullPath($_.Path).StartsWith(
              $prefix, [StringComparison]::OrdinalIgnoreCase)
        } catch {
          $false
        }
      })
}

$resolvedPackagePath = [IO.Path]::GetFullPath($PackagePath.Trim().Trim('"'))
if (!(Test-Path -LiteralPath $resolvedPackagePath -PathType Leaf)) {
  throw "Installer package was not found: $resolvedPackagePath"
}
if ([IO.Path]::GetExtension($resolvedPackagePath) -ne ".exe") {
  throw "Installer package must be an executable."
}

$suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
if ([string]::IsNullOrWhiteSpace($InstallDirectory)) {
  $InstallDirectory = Join-Path ([IO.Path]::GetTempPath()) `
      "sar-installer-acceptance-$suffix"
}
$installPath = [IO.Path]::GetFullPath($InstallDirectory.Trim().Trim('"'))
if (Test-Path -LiteralPath $installPath) {
  throw "Acceptance install directory must not already exist: $installPath"
}

$asioRegistrationKey =
    "HKCU:\Software\Classes\CLSID\" +
    "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\InprocServer32"
if (Test-Path -LiteralPath $asioRegistrationKey) {
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  throw "Acceptance requires no existing Virtual ASIO registration; found '$registeredDriver'."
}

$installed = $false
$savedEnvironment = @{}
foreach ($name in @(
    "PATH",
    "QTDIR",
    "QT_PLUGIN_PATH",
    "QML2_IMPORT_PATH",
    "QT_QPA_PLATFORM",
    "QT_QUICK_BACKEND")) {
  $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable(
      $name, [EnvironmentVariableTarget]::Process)
}

try {
  Invoke-NsisExecutable -Path $resolvedPackagePath `
      -Argument @("/S", "/D=$installPath")
  $installed = $true

  $installedFiles = @(
    "bin\SystemAudioRouteLauncher.exe",
    "bin\sar_engine_service.exe",
    "bin\sar_control_cli.exe",
    "bin\SystemAudioRoute.exe",
    "bin\sar_virtual_asio_register.exe",
    "bin\SystemAudioRouteVirtualASIO.dll",
    "bin\Qt6Core.dll",
    "bin\Qt6Quick.dll",
    "bin\qt.conf",
    "plugins\platforms\qwindows.dll",
    "qml\QtQuick\qtquick2plugin.dll",
    "Uninstall.exe"
  )
  foreach ($relativePath in $installedFiles) {
    $path = Join-Path $installPath $relativePath
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Installed payload is missing '$relativePath'."
    }
  }

  $registerPath = Join-Path $installPath `
      "bin\sar_virtual_asio_register.exe"
  $driverPath = Join-Path $installPath `
      "bin\SystemAudioRouteVirtualASIO.dll"
  & $registerPath --verify $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "Installed Virtual ASIO verification failed with exit code $LASTEXITCODE."
  }

  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  if (![IO.Path]::GetFullPath($registeredDriver).Equals(
          [IO.Path]::GetFullPath($driverPath),
          [StringComparison]::OrdinalIgnoreCase)) {
    throw "Virtual ASIO registration does not belong to the installed payload."
  }

  Invoke-NsisExecutable -Path $resolvedPackagePath `
      -Argument @("/S", "/D=$installPath")
  & $registerPath --verify $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "In-place installer update did not preserve Virtual ASIO registration."
  }

  [Environment]::SetEnvironmentVariable(
      "QTDIR", $null, [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QT_PLUGIN_PATH", $null, [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QML2_IMPORT_PATH", $null, [EnvironmentVariableTarget]::Process)
  $cleanPath = @($env:PATH -split ';' | Where-Object {
      $_ -and $_ -notmatch '(?i)\\Qt\\|\\Qt6\\|\\Qt\\6\\'
    }) -join ';'
  [Environment]::SetEnvironmentVariable(
      "PATH", $cleanPath, [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QT_QPA_PLATFORM", "offscreen", [EnvironmentVariableTarget]::Process)
  [Environment]::SetEnvironmentVariable(
      "QT_QUICK_BACKEND", "software", [EnvironmentVariableTarget]::Process)

  $launcher = Start-Process `
      -FilePath (Join-Path $installPath "bin\SystemAudioRouteLauncher.exe") `
      -WorkingDirectory (Join-Path $installPath "bin") `
      -Wait -PassThru
  if ($launcher.ExitCode -ne 0) {
    throw "Bootstrap launcher failed with exit code $($launcher.ExitCode)."
  }

  $deadline = [DateTime]::UtcNow.AddSeconds($GuiHealthSeconds)
  do {
    $guiProcesses = Get-InstalledProcess -Name "SystemAudioRoute" `
        -Directory $installPath
    $engineProcesses = Get-InstalledProcess -Name "sar_engine_service" `
        -Directory $installPath
    if ($guiProcesses.Count -eq 1 -and $engineProcesses.Count -eq 1) {
      break
    }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $deadline)

  if ($guiProcesses.Count -ne 1) {
    throw "Bootstrap launcher did not leave exactly one GUI process running."
  }
  if ($engineProcesses.Count -ne 1) {
    throw "Bootstrap launcher did not leave exactly one engine service running."
  }
  Start-Sleep -Seconds $GuiHealthSeconds
  if ($guiProcesses[0].HasExited -or $engineProcesses[0].HasExited) {
    throw "A bootstrapped process exited before the health window completed."
  }

  $uninstallerPath = Join-Path $installPath "Uninstall.exe"
  Invoke-NsisExecutable -Path $uninstallerPath -Argument @("/S")

  $uninstallDeadline = [DateTime]::UtcNow.AddSeconds(10)
  while ((Test-Path -LiteralPath $installPath) -and
         [DateTime]::UtcNow -lt $uninstallDeadline) {
    Start-Sleep -Milliseconds 100
  }
  if (Test-Path -LiteralPath $installPath) {
    throw "Silent uninstaller left the installation directory behind."
  }
  if (Test-Path -LiteralPath $asioRegistrationKey) {
    throw "Silent uninstaller left the Virtual ASIO registration behind."
  }
  if ((Get-InstalledProcess -Name "SystemAudioRoute" `
          -Directory $installPath).Count -ne 0 -or
      (Get-InstalledProcess -Name "sar_engine_service" `
          -Directory $installPath).Count -ne 0) {
    throw "Silent uninstaller left product processes running."
  }
  $installed = $false

  $packageHash = (Get-FileHash -Algorithm SHA256 `
      -LiteralPath $resolvedPackagePath).Hash
  Write-Output (
      "windows_installer_acceptance passed=1" +
      " package=`"$resolvedPackagePath`"" +
      " sha256=$packageHash" +
      " install=passed update=passed launcher=passed" +
      " gui=passed engine=passed asio=passed uninstall=passed")
} finally {
  foreach ($processName in @("SystemAudioRoute", "sar_engine_service")) {
    foreach ($process in @(Get-InstalledProcess -Name $processName `
        -Directory $installPath)) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
  }
  foreach ($name in $savedEnvironment.Keys) {
    [Environment]::SetEnvironmentVariable(
        $name,
        $savedEnvironment[$name],
        [EnvironmentVariableTarget]::Process)
  }
  if ($installed -and (Test-Path -LiteralPath $installPath)) {
    $uninstallerPath = Join-Path $installPath "Uninstall.exe"
    if (Test-Path -LiteralPath $uninstallerPath -PathType Leaf) {
      try {
        Invoke-NsisExecutable -Path $uninstallerPath -Argument @("/S")
      } catch {
        Write-Warning "Acceptance cleanup uninstall failed: $($_.Exception.Message)"
      }
    }
  }
}
