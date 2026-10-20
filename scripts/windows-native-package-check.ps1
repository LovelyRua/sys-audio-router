function Test-NativePackage {
  param([Parameter(Mandatory=$true)][string]$InstallPath)

  foreach ($relative in @('bin\SystemAudioRouteVstgui.exe',
      'licenses\vstgui\LICENSE', 'licenses\nlohmann-json\LICENSE.MIT',
      'start-native-preview.cmd')) {
    if (!(Test-Path -LiteralPath (Join-Path $InstallPath $relative) -PathType Leaf)) {
      throw "Native preview payload is missing '$relative'."
    }
  }
  # The experimental kernel driver is deliberately outside this installer.
  if (Test-Path -LiteralPath (Join-Path $InstallPath 'bin\driver-lab')) {
    throw 'Ordinary package unexpectedly contains experimental driver tooling.'
  }
  $engines = @(Get-InstalledProcess -Name sar_engine_service -Directory $InstallPath)
  if ($engines.Count -ne 1) { throw 'Native launcher test requires one owned engine.' }
  $engineId = $engines[0].Id
  $native = $null
  try {
    $launcher = Start-Process -FilePath (Join-Path $InstallPath 'bin\SystemAudioRouteLauncher.exe') `
      -ArgumentList '--native' -WorkingDirectory (Join-Path $InstallPath 'bin') `
      -WindowStyle Hidden -PassThru
    $null = $launcher.Handle
    if (!$launcher.WaitForExit(10000)) { throw 'Native bootstrap timed out.' }
    if ($launcher.ExitCode -ne 0) { throw "Native bootstrap failed: $($launcher.ExitCode)." }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
      $owned = @(Get-InstalledProcess -Name SystemAudioRouteVstgui -Directory $InstallPath)
      if ($owned.Count -eq 1) { break }
      Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($owned.Count -ne 1) { throw 'Native launcher did not start the installed preview.' }
    $native = $owned[0]
    Start-Sleep -Seconds 3
    $native.Refresh()
    if ($native.HasExited) { throw 'Installed native preview exited during health check.' }
    $engines = @(Get-InstalledProcess -Name sar_engine_service -Directory $InstallPath)
    if ($engines.Count -ne 1 -or $engines[0].Id -ne $engineId) {
      throw 'Native launcher replaced or duplicated the shared engine.'
    }
    # Only the exact process started above is disposed in this CI-only probe.
    Stop-Process -Id $native.Id -Force
    $null = $native.WaitForExit(5000)
    $native = $null
    $engines = @(Get-InstalledProcess -Name sar_engine_service -Directory $InstallPath)
    if ($engines.Count -ne 1 -or $engines[0].Id -ne $engineId) {
      throw 'Closing the native preview changed the engine lifetime.'
    }
    Write-Output 'native_package_acceptance passed=1 payload=passed shared_engine=passed launch=passed'
  } finally {
    if ($native -and !$native.HasExited) {
      Stop-Process -Id $native.Id -Force -ErrorAction SilentlyContinue
    }
  }
}
