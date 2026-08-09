[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackagePath,

  [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA `
      "Programs\SystemAudioRoute-Alpha-Acceptance"),

  [ValidateRange(1, 30)]
  [int]$GuiHealthSeconds = 4
)

$ErrorActionPreference = "Stop"

function Invoke-CommandScript {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [string[]]$Argument = @()
  )

  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = @(& $Path @Argument 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousPreference
  }
  return @{
    ExitCode = $exitCode
    Output = $output
  }
}

function Write-CommandOutput {
  param([hashtable]$Result)
  foreach ($line in $Result.Output) {
    Write-Output $line
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
  throw "Alpha package was not found: $resolvedPackagePath"
}
if ([IO.Path]::GetExtension($resolvedPackagePath) -ne ".zip") {
  throw "Alpha package must be a ZIP file."
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

$suffix = [guid]::NewGuid().ToString("N").Substring(0, 8)
$workPath = Join-Path ([IO.Path]::GetTempPath()) "sar-alpha-acceptance-$suffix"
$negativeInstallPath = "$installPath.missing-runtime-$suffix"
$ownershipInstallPath = "$installPath.ownership-$suffix"
$primaryInstalled = $false
$ownershipInstalled = $false
$guiProcess = $null
$engineProcess = $null
$packageRoot = $null
$packageHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $resolvedPackagePath).Hash
foreach ($unusedPath in @($negativeInstallPath, $ownershipInstallPath)) {
  if (Test-Path -LiteralPath $unusedPath) {
    throw "Acceptance scratch path must not already exist: $unusedPath"
  }
}

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
  New-Item -ItemType Directory -Path $workPath | Out-Null
  Expand-Archive -LiteralPath $resolvedPackagePath -DestinationPath $workPath

  $roots = @(Get-ChildItem -LiteralPath $workPath -Directory)
  if ($roots.Count -ne 1) {
    throw "Alpha package must contain exactly one top-level directory."
  }
  $packageRoot = $roots[0].FullName
  $installerPath = Join-Path $packageRoot "install-alpha.cmd"
  if (!(Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Alpha package does not contain install-alpha.cmd."
  }

  $platformPluginPath = Join-Path $packageRoot `
      "plugins\platforms\qwindows.dll"
  if (!(Test-Path -LiteralPath $platformPluginPath -PathType Leaf)) {
    throw "Alpha package does not contain qwindows.dll."
  }

  $hiddenPlatformPluginPath = "$platformPluginPath.acceptance-missing"
  Move-Item -LiteralPath $platformPluginPath `
      -Destination $hiddenPlatformPluginPath
  try {
    $negativeResult = Invoke-CommandScript -Path $installerPath `
        -Argument @($negativeInstallPath)
    if ($negativeResult.ExitCode -eq 0) {
      throw "Installer accepted a package with a missing platform plugin."
    }
    if (Test-Path -LiteralPath $negativeInstallPath) {
      throw "Rejected package changed the negative-test install directory."
    }
  } finally {
    if (Test-Path -LiteralPath $hiddenPlatformPluginPath -PathType Leaf) {
      Move-Item -LiteralPath $hiddenPlatformPluginPath `
          -Destination $platformPluginPath
    }
  }

  $installResult = Invoke-CommandScript -Path $installerPath `
      -Argument @($installPath)
  Write-CommandOutput -Result $installResult
  if ($installResult.ExitCode -ne 0) {
    throw "Alpha installer failed with exit code $($installResult.ExitCode)."
  }
  $primaryInstalled = $true

  $installedFiles = @(
    "bin\SystemAudioRouteLauncher.exe",
    "bin\sar_engine_service.exe",
    "bin\sar_control_cli.exe",
    "bin\SystemAudioRoute.exe",
    "bin\Qt6Core.dll",
    "bin\Qt6Quick.dll",
    "bin\qt.conf",
    "bin\sar_virtual_asio_register.exe",
    "bin\SystemAudioRouteVirtualASIO.dll",
    "plugins\platforms\qwindows.dll",
    "qml\QtQuick\qtquick2plugin.dll",
    ".system-audio-route-alpha",
    "uninstall-alpha.cmd"
  )
  foreach ($relativePath in $installedFiles) {
    $path = Join-Path $installPath $relativePath
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Installed Alpha payload is missing '$relativePath'."
    }
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

  $launcherPath = Join-Path $installPath `
      "bin\SystemAudioRouteLauncher.exe"
  $launcherProcess = Start-Process -FilePath $launcherPath `
      -WorkingDirectory (Join-Path $installPath "bin") `
      -PassThru
  if (!$launcherProcess.WaitForExit(10000)) {
    Stop-Process -Id $launcherProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Installed bootstrap launcher did not exit within ten seconds."
  }
  if ($launcherProcess.ExitCode -ne 0) {
    throw "Installed bootstrap launcher failed with exit code $($launcherProcess.ExitCode)."
  }

  $launchDeadline = [DateTime]::UtcNow.AddSeconds($GuiHealthSeconds)
  do {
    $guiProcesses = Get-InstalledProcess -Name "SystemAudioRoute" `
        -Directory $installPath
    $engineProcesses = Get-InstalledProcess -Name "sar_engine_service" `
        -Directory $installPath
    if ($guiProcesses.Count -eq 1 -and $engineProcesses.Count -eq 1) {
      break
    }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $launchDeadline)
  if ($guiProcesses.Count -ne 1 -or $engineProcesses.Count -ne 1) {
    throw "Bootstrap launcher did not leave exactly one GUI and one engine process."
  }
  $guiProcess = $guiProcesses[0]
  $engineProcess = $engineProcesses[0]
  Start-Sleep -Seconds $GuiHealthSeconds
  if ($guiProcess.HasExited -or $engineProcess.HasExited) {
    throw "A bootstrapped process exited before the health window completed."
  }

  $controlResult = Invoke-CommandScript `
      -Path (Join-Path $installPath "bin\sar_control_cli.exe") `
      -Argument @("devices")
  Write-CommandOutput -Result $controlResult
  if ($controlResult.ExitCode -ne 0 -or
      $controlResult.Output.Count -eq 0 -or
      $controlResult.Output[0] -notmatch
          '^control_response status=accepted command_id=cli-\d+-\d+(?:\s|$)') {
    throw "Installed control-plane handshake failed with exit code $($controlResult.ExitCode): $([string]::Join('; ', $controlResult.Output))"
  }

  $diagnosticsResult = Invoke-CommandScript `
      -Path (Join-Path $installPath "bin\sar_control_cli.exe") `
      -Argument @("diagnostics")
  Write-CommandOutput -Result $diagnosticsResult
  if ($diagnosticsResult.ExitCode -ne 0 -or
      $diagnosticsResult.Output.Count -eq 0 -or
      $diagnosticsResult.Output[0] -notmatch
          '^control_response status=accepted command_id=cli-\d+-\d+\s' -or
      $diagnosticsResult.Output[0] -notmatch '\sprocessed_blocks=\d+(?:\s|$)' -or
      $diagnosticsResult.Output[0] -notmatch '\sxruns=\d+(?:\s|$)' -or
      $diagnosticsResult.Output[0] -notmatch '\scallback_peak_us=') {
    throw "Installed diagnostics handshake failed with exit code $($diagnosticsResult.ExitCode): $([string]::Join('; ', $diagnosticsResult.Output))"
  }

  Stop-Process -Id $guiProcess.Id -Force
  Wait-Process -Id $guiProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  $guiProcess = $null
  Stop-Process -Id $engineProcess.Id -Force
  Wait-Process -Id $engineProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  $engineProcess = $null

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

  $updateResult = Invoke-CommandScript -Path $installerPath `
      -Argument @($installPath)
  Write-CommandOutput -Result $updateResult
  if ($updateResult.ExitCode -ne 0) {
    throw "Alpha in-place update failed with exit code $($updateResult.ExitCode)."
  }
  & $registerPath --verify $driverPath --user --x64
  if ($LASTEXITCODE -ne 0) {
    throw "Updated Virtual ASIO verification failed with exit code $LASTEXITCODE."
  }

  $ownershipInstallResult = Invoke-CommandScript -Path $installerPath `
      -Argument @($ownershipInstallPath)
  Write-CommandOutput -Result $ownershipInstallResult
  if ($ownershipInstallResult.ExitCode -ne 0) {
    throw "Alpha ownership install failed with exit code $($ownershipInstallResult.ExitCode)."
  }
  $ownershipInstalled = $true
  $ownershipDriverPath = Join-Path $ownershipInstallPath `
      "bin\SystemAudioRouteVirtualASIO.dll"
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  if (![IO.Path]::GetFullPath($registeredDriver).Equals(
          [IO.Path]::GetFullPath($ownershipDriverPath),
          [StringComparison]::OrdinalIgnoreCase)) {
    throw "Second install did not take Virtual ASIO registration ownership."
  }

  $ownershipLauncher = Start-Process `
      -FilePath (Join-Path $ownershipInstallPath `
          "bin\SystemAudioRouteLauncher.exe") `
      -WorkingDirectory (Join-Path $ownershipInstallPath "bin") `
      -PassThru
  if (!$ownershipLauncher.WaitForExit(10000)) {
    Stop-Process -Id $ownershipLauncher.Id -Force -ErrorAction SilentlyContinue
    throw "Second installed bootstrap launcher did not exit within ten seconds."
  }
  if ($ownershipLauncher.ExitCode -ne 0) {
    throw "Second installed bootstrap launcher failed."
  }
  $ownershipDeadline = [DateTime]::UtcNow.AddSeconds(4)
  do {
    $ownershipGuiProcesses = Get-InstalledProcess -Name "SystemAudioRoute" `
        -Directory $ownershipInstallPath
    $ownershipEngineProcesses = Get-InstalledProcess `
        -Name "sar_engine_service" -Directory $ownershipInstallPath
    if ($ownershipGuiProcesses.Count -eq 1 -and
        $ownershipEngineProcesses.Count -eq 1) {
      break
    }
    Start-Sleep -Milliseconds 100
  } while ([DateTime]::UtcNow -lt $ownershipDeadline)
  if ($ownershipGuiProcesses.Count -ne 1 -or
      $ownershipEngineProcesses.Count -ne 1) {
    throw "Second launcher did not start exactly one GUI and one engine."
  }
  $guiProcess = $ownershipGuiProcesses[0]
  $engineProcess = $ownershipEngineProcesses[0]

  $primaryUninstallResult = Invoke-CommandScript `
      -Path (Join-Path $installPath "uninstall-alpha.cmd")
  Write-CommandOutput -Result $primaryUninstallResult
  if ($primaryUninstallResult.ExitCode -ne 0) {
    throw "Primary uninstall failed while its same-prefix sibling was running."
  }
  if (![string]::Join("`n", $primaryUninstallResult.Output).Contains(
          "registration=not_owned")) {
    throw "Primary uninstall did not preserve the second install's registration."
  }
  $primaryInstalled = $false
  if ($guiProcess.HasExited -or $engineProcess.HasExited) {
    throw "Primary uninstall disturbed a same-prefix sibling process."
  }
  if (Test-Path -LiteralPath $installPath) {
    throw "Primary uninstaller left the installation directory behind."
  }
  $registeredDriver = (Get-Item -LiteralPath $asioRegistrationKey).GetValue("")
  if (![IO.Path]::GetFullPath($registeredDriver).Equals(
          [IO.Path]::GetFullPath($ownershipDriverPath),
          [StringComparison]::OrdinalIgnoreCase)) {
    throw "Primary uninstall changed the second install's registration."
  }

  Stop-Process -Id $guiProcess.Id -Force
  Wait-Process -Id $guiProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  $guiProcess = $null
  Stop-Process -Id $engineProcess.Id -Force
  Wait-Process -Id $engineProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  $engineProcess = $null

  $ownershipUninstallResult = Invoke-CommandScript `
      -Path (Join-Path $ownershipInstallPath "uninstall-alpha.cmd")
  Write-CommandOutput -Result $ownershipUninstallResult
  if ($ownershipUninstallResult.ExitCode -ne 0) {
    throw "Ownership uninstaller failed with exit code $($ownershipUninstallResult.ExitCode)."
  }
  $ownershipInstalled = $false
  Start-Sleep -Milliseconds 750
  if (Test-Path -LiteralPath $ownershipInstallPath) {
    throw "Ownership uninstaller left the installation directory behind."
  }
  if (Test-Path -LiteralPath $asioRegistrationKey) {
    throw "Ownership uninstaller left the Virtual ASIO registration behind."
  }

  Write-Output (
      "alpha_package_acceptance passed=1" +
      " package=`"$resolvedPackagePath`"" +
      " sha256=$packageHash" +
      " gui_health_seconds=$GuiHealthSeconds" +
      " missing_runtime_guard=passed" +
      " install=passed update=passed gui_launch=passed control=passed diagnostics=passed asio=passed" +
      " path_boundary=passed ownership=passed uninstall=passed")
} finally {
  if ($null -ne $guiProcess -and !$guiProcess.HasExited) {
    Stop-Process -Id $guiProcess.Id -Force -ErrorAction SilentlyContinue
  }
  if ($null -ne $engineProcess -and !$engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
  }
  foreach ($name in $savedEnvironment.Keys) {
    [Environment]::SetEnvironmentVariable(
        $name,
        $savedEnvironment[$name],
        [EnvironmentVariableTarget]::Process)
  }
  foreach ($cleanupInstall in @(
      @{ Active = $primaryInstalled; Path = $installPath },
      @{ Active = $ownershipInstalled; Path = $ownershipInstallPath })) {
    if ($cleanupInstall.Active -and
        (Test-Path -LiteralPath $cleanupInstall.Path)) {
      $markerPath = Join-Path $cleanupInstall.Path ".system-audio-route-alpha"
      $uninstallerPath = Join-Path $cleanupInstall.Path "uninstall-alpha.cmd"
      if ((Test-Path -LiteralPath $markerPath -PathType Leaf) -and
          (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
        Invoke-CommandScript -Path $uninstallerPath | Out-Null
      }
    }
  }
  if (Test-Path -LiteralPath $workPath) {
    Remove-Item -LiteralPath $workPath -Recurse -Force
  }
}
