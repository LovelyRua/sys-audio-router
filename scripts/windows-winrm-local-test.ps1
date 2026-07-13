param(
  [string]$HostName = "192.168.123.123",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password,
  [string]$Slot = "local",
  [string]$RepoRoot = ""
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

$archive = Join-Path $env:TEMP "sar-local-$safeSlot.zip"
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
  $remoteArchive = "C:\Windows\Temp\sar-local-$safeSlot.zip"
  Write-Host "Uploading archive to $HostName"
  Copy-Item -LiteralPath $archive -Destination $remoteArchive -ToSession $session -Force

  Invoke-Command -Session $session -ArgumentList $safeSlot, $remoteArchive -ScriptBlock {
    param([string]$SafeSlot, [string]$RemoteArchive)

    $ErrorActionPreference = "Stop"
    $repoDir = Join-Path $env:USERPROFILE "src\sys-audio-router-$SafeSlot"
    $buildDir = "build-$SafeSlot"
    $cmdFile = "C:\Windows\Temp\sar-local-build-$SafeSlot.cmd"

    try {
      Write-Host "Repository: $repoDir"
      Write-Host "Build dir:  $buildDir"
      Write-Host "Archive:    $RemoteArchive"

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
        "cmake --build `"$buildDir`"",
        "if errorlevel 1 exit /b 1",
        "ctest --test-dir `"$buildDir`" --output-on-failure",
        "exit /b %errorlevel%"
      )
      Set-Content -LiteralPath $cmdFile -Value $lines -Encoding ASCII
      cmd.exe /c "`"$cmdFile`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Local archive build failed with exit code $LASTEXITCODE."
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
