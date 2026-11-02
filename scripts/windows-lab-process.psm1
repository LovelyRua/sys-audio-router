Set-StrictMode -Version 2.0

function Get-SarLabSessionId {
  return [Diagnostics.Process]::GetCurrentProcess().SessionId
}

function ConvertTo-SarLabArgument {
  param([AllowEmptyString()][string]$Value)
  if ($Value.Length -gt 32767 -or $Value.IndexOfAny([char[]]@(0, 10, 13)) -ge 0) {
    throw 'Native argument contains an unsupported length or control character.'
  }
  $quoted = [Text.StringBuilder]::new()
  $null = $quoted.Append('"')
  $slashes = 0
  foreach ($character in $Value.ToCharArray()) {
    if ($character -eq '\') { ++$slashes; continue }
    if ($character -eq '"') {
      $null = $quoted.Append('\', 2 * $slashes + 1)
    } elseif ($slashes -ne 0) {
      $null = $quoted.Append('\', $slashes)
    }
    $slashes = 0
    $null = $quoted.Append($character)
  }
  if ($slashes -ne 0) { $null = $quoted.Append('\', 2 * $slashes) }
  $null = $quoted.Append('"')
  return $quoted.ToString()
}

function Invoke-SarLabProcess {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][string]$FilePath,
    [string[]]$ArgumentList = @(),
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$')]
    [string]$Name,
    [ValidateRange(100, 120000)][int]$TimeoutMilliseconds = 10000,
    [ValidateRange(1024, 16777216)][long]$MaximumOutputBytes = 1048576
  )
  $executable = (Resolve-Path -LiteralPath $FilePath -ErrorAction Stop).Path
  if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Native executable must be a file.'
  }
  if ($ArgumentList.Count -gt 128) { throw 'Too many native arguments.' }
  $arguments = @($ArgumentList | ForEach-Object { ConvertTo-SarLabArgument $_ }) -join ' '
  if ($arguments.Length -gt 30000) { throw 'Native command line is too long.' }
  $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
  $stdout = Join-Path $OutputDirectory "$Name.stdout.log"
  $stderr = Join-Path $OutputDirectory "$Name.stderr.log"
  if ((Test-Path -LiteralPath $stdout) -or (Test-Path -LiteralPath $stderr)) {
    throw "Process log already exists for '$Name'."
  }
  $start = @{
    FilePath = $executable; PassThru = $true; WindowStyle = 'Hidden'
    RedirectStandardOutput = $stdout; RedirectStandardError = $stderr
    ErrorAction = 'Stop'
  }
  if ($arguments.Length -ne 0) { $start.ArgumentList = $arguments }
  $process = Start-Process @start
  $null = $process.Handle
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $failure = ''
  try {
    while (!$process.HasExited) {
      foreach ($path in @($stdout, $stderr)) {
        if ((Test-Path -LiteralPath $path -PathType Leaf) -and
            (Get-Item -LiteralPath $path).Length -gt $MaximumOutputBytes) {
          $failure = 'process_output_limit'
          break
        }
      }
      if ($failure -ne '') { break }
      if ($clock.ElapsedMilliseconds -ge $TimeoutMilliseconds) {
        $failure = 'process_timeout'
        break
      }
      $remaining = $TimeoutMilliseconds - [int]$clock.ElapsedMilliseconds
      $null = $process.WaitForExit([Math]::Max(1, [Math]::Min(25, $remaining)))
    }
    if (!$process.HasExited) {
      $process.Kill()
      if (!$process.WaitForExit(5000)) { throw 'Owned lab process did not terminate.' }
    }
    $process.WaitForExit()
    $process.Refresh()
    $exitCode = $process.ExitCode
    $lines = @{}
    foreach ($entry in @(@{key='stdout'; path=$stdout}, @{key='stderr'; path=$stderr})) {
      if ((Test-Path -LiteralPath $entry.path -PathType Leaf) -and
          (Get-Item -LiteralPath $entry.path).Length -le $MaximumOutputBytes) {
        $lines[$entry.key] = @([IO.File]::ReadAllLines($entry.path))
      } else {
        if ($failure -eq '') { $failure = 'process_output_limit' }
        $lines[$entry.key] = @()
      }
    }
    return [pscustomobject][ordered]@{
      exit_code = $exitCode; failure_reason = $failure
      elapsed_ms = $clock.ElapsedMilliseconds
      stdout_lines = $lines.stdout; stderr_lines = $lines.stderr
      stdout_path = $stdout; stderr_path = $stderr
    }
  } finally {
    if (!$process.HasExited) {
      $process.Kill()
      $null = $process.WaitForExit(5000)
    }
    $clock.Stop()
    $process.Dispose()
  }
}

Export-ModuleMember -Function ConvertTo-SarLabArgument, Invoke-SarLabProcess, Get-SarLabSessionId
