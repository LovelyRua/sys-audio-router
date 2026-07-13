param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "local-measure",
  [string]$RepoRoot = "",
  [ValidateSet("render", "duplex", "loopback", "both", "all")]
  [string]$Mode = "render",
  [uint32]$DurationMs = 1000,
  [uint32]$TimeoutMs = 10,
  [uint32]$Iterations = 1,
  [string]$RequireHealthyText = "false",
  [string]$AllowUnavailableText = "false"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
  $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
  $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$safeSlot = ($Slot -replace '[^A-Za-z0-9_.-]', '-').Trim('.-_')
if ([string]::IsNullOrWhiteSpace($safeSlot)) {
  throw "Slot '$Slot' does not contain any valid path characters."
}

$requireHealthy = $false
if ($RequireHealthyText -match '^(1|true|yes|y|on)$') {
  $requireHealthy = $true
} elseif ($RequireHealthyText -notmatch '^(0|false|no|n|off)$') {
  throw "RequireHealthyText must be true/false, yes/no, on/off, or 1/0."
}

$allowUnavailable = $false
if ($AllowUnavailableText -match '^(1|true|yes|y|on)$') {
  $allowUnavailable = $true
} elseif ($AllowUnavailableText -notmatch '^(0|false|no|n|off)$') {
  throw "AllowUnavailableText must be true/false, yes/no, on/off, or 1/0."
}
if ($Iterations -eq 0) {
  throw "Iterations must be at least one."
}

$archive = Join-Path $env:TEMP "sar-local-measure-$safeSlot.zip"
if (Test-Path $archive) {
  Remove-Item -LiteralPath $archive -Force
}

Write-Host "Creating local source archive from HEAD"
Write-Host "Repository: $RepoRoot"
Write-Host "Slot:       $safeSlot"
Write-Host "Archive:    $archive"

& git -C $RepoRoot archive --format=zip HEAD -o $archive
if ($LASTEXITCODE -ne 0) {
  throw "git archive failed with exit code $LASTEXITCODE."
}

$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($UserName, $securePassword)
$session = $null

try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential
  $remoteArchive = "C:\Windows\Temp\sar-local-measure-$safeSlot.zip"
  Write-Host "Uploading archive to $HostName"
  Copy-Item -LiteralPath $archive -Destination $remoteArchive -ToSession $session -Force

  Invoke-Command -Session $session -ArgumentList `
      $safeSlot, $remoteArchive, $Mode, $DurationMs, $TimeoutMs, $requireHealthy, `
      $allowUnavailable, $Iterations `
      -ScriptBlock {
    param(
      [string]$SafeSlot,
      [string]$RemoteArchive,
      [string]$Mode,
      [uint32]$DurationMs,
      [uint32]$TimeoutMs,
      [bool]$RequireHealthy,
      [bool]$AllowUnavailable,
      [uint32]$Iterations
    )

    $ErrorActionPreference = "Stop"
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    $repoDir = Join-Path $env:USERPROFILE "src\sys-audio-router-$SafeSlot"
    $buildDir = "build-$SafeSlot"
    $cmdFile = "C:\Windows\Temp\sar-local-measure-$SafeSlot.cmd"
    $measureArgs = @("--duration-ms", "$DurationMs", "--timeout-ms", "$TimeoutMs")
    if ($RequireHealthy) {
      $measureArgs += "--require-healthy"
    }

    try {
      Write-Host "Repository: $repoDir"
      Write-Host "Build dir:  $buildDir"
      Write-Host "Archive:    $RemoteArchive"
      Write-Host "Mode:       $Mode"
      Write-Host "Allow unavailable endpoint: $AllowUnavailable"
      Write-Host "Iterations: $Iterations"

      if (Test-Path $repoDir) {
        Remove-Item -LiteralPath $repoDir -Recurse -Force
      }
      New-Item -ItemType Directory -Path $repoDir | Out-Null
      tar.exe -xf $RemoteArchive -C $repoDir
      if ($LASTEXITCODE -ne 0) {
        throw "tar extraction failed with exit code $LASTEXITCODE."
      }

      $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
      if (!(Test-Path $vswhere)) {
        throw "vswhere.exe was not found."
      }
      $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
      if ([string]::IsNullOrWhiteSpace($vsInstall)) {
        throw "Visual Studio C++ tools were not found."
      }
      $vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
      if (!(Test-Path $vsDevCmd)) {
        throw "VsDevCmd.bat was not found."
      }

      $targets = @()
      $executables = @{}
      if ($Mode -eq "render" -or $Mode -eq "both" -or $Mode -eq "all") {
        $targets += "sar_measure_wasapi_render_loop"
        $executables.render = Join-Path $repoDir "$buildDir\sar_measure_wasapi_render_loop.exe"
      }
      if ($Mode -eq "duplex" -or $Mode -eq "both" -or $Mode -eq "all") {
        $targets += "sar_measure_wasapi_duplex_loop"
        $executables.duplex = Join-Path $repoDir "$buildDir\sar_measure_wasapi_duplex_loop.exe"
      }
      if ($Mode -eq "loopback" -or $Mode -eq "all") {
        $targets += "sar_measure_wasapi_loopback_loop"
        $executables.loopback = Join-Path $repoDir "$buildDir\sar_measure_wasapi_loopback_loop.exe"
      }
      if ($targets.Count -eq 0) {
        throw "No measurement targets selected for mode '$Mode'."
      }

      $targetArgs = [string]::Join(" ", $targets)
      $lines = @(
        "@echo off",
        "setlocal EnableExtensions",
        "call `"$vsDevCmd`" -arch=x64 -host_arch=x64",
        "if errorlevel 1 exit /b 1",
        "set `"PATH=$vsInstall\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vsInstall\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%`"",
        "cd /d `"$repoDir`"",
        "where ninja >nul 2>nul",
        "if errorlevel 1 (cmake -S . -B `"$buildDir`") else (cmake -S . -B `"$buildDir`" -G Ninja)",
        "if errorlevel 1 exit /b 1",
        "cmake --build `"$buildDir`" --target $targetArgs",
        "if errorlevel 1 exit /b 1"
      )
      $lines += "exit /b 0"

      Set-Content -LiteralPath $cmdFile -Value $lines -Encoding ASCII
      cmd.exe /c "`"$cmdFile`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Local WASAPI measurement failed with exit code $LASTEXITCODE."
      }

      Import-Module (Join-Path $repoDir "scripts\windows-wasapi-soak-runner.psm1") -Force
      $soakOutput = @(Invoke-WasapiSoak -Mode $Mode -Iterations $Iterations -RunMeasurement {
        param($modeName, $iteration)
        $previousErrorActionPreference = $ErrorActionPreference
        $measurementExitCode = 1
        $ErrorActionPreference = "Continue"
        try {
          & $executables[$modeName] @measureArgs 2>&1 | Write-Host
          $measurementExitCode = $LASTEXITCODE
        } finally {
          $ErrorActionPreference = $previousErrorActionPreference
        }
        return $measurementExitCode
      })
      $soakOutput[0..($soakOutput.Count - 2)] | Write-Output
      $soakResult = $soakOutput[-1]
      if (!$AllowUnavailable -and $soakResult.FailureCount -ne 0) {
        throw "Local WASAPI measurement failed in $($soakResult.FailureCount) of $($soakResult.Attempts) attempts."
      }
    } finally {
      if (Test-Path $RemoteArchive) {
        Remove-Item -LiteralPath $RemoteArchive -Force
      }
      if (Test-Path $cmdFile) {
        Remove-Item -LiteralPath $cmdFile -Force
      }
    }
  }
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
  if (Test-Path $archive) {
    Remove-Item -LiteralPath $archive -Force
  }
}
