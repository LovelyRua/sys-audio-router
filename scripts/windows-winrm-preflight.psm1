Set-StrictMode -Version 2.0

function Get-SarExceptionText {
  param([System.Management.Automation.ErrorRecord]$ErrorRecord)

  $parts = [System.Collections.Generic.List[string]]::new()
  if ($null -ne $ErrorRecord.ErrorDetails -and
      ![string]::IsNullOrWhiteSpace($ErrorRecord.ErrorDetails.Message)) {
    $parts.Add($ErrorRecord.ErrorDetails.Message)
  }
  $exception = $ErrorRecord.Exception
  while ($null -ne $exception) {
    if (![string]::IsNullOrWhiteSpace($exception.Message)) {
      $parts.Add($exception.Message)
    }
    $exception = $exception.InnerException
  }
  return [string]::Join(" | ", $parts)
}

function Get-SarWinRmFailureDiagnosis {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory = $true)]
    [System.Management.Automation.ErrorRecord]$ErrorRecord,
    [Parameter(Mandatory = $true)]
    [string]$HostName,
    [int]$Port = 5985,
    [bool]$TcpReachable = $false
  )

  $message = Get-SarExceptionText -ErrorRecord $ErrorRecord
  $nativeCodes = [System.Collections.Generic.List[int]]::new()
  $exception = $ErrorRecord.Exception
  while ($null -ne $exception) {
    $nativeCodes.Add(($exception.HResult -band 0xffff))
    $exception = $exception.InnerException
  }

  $blockCode = "winrm_wsman_failed"
  $summary = "WinRM WSMan preflight failed."
  $action = "Run Test-WSMan $HostName from this machine and verify the remote WinRM listener and credentials."
  if ($message -match '(?<!\d)12152(?!\d)' -or $nativeCodes.Contains(12152)) {
    $blockCode = "winrm_http_invalid_server_response_12152"
    $summary = "WinRM error 12152: TCP answered, but the HTTP response was not a valid WSMan response."
    $action = "On the remote console run 'winrm enumerate winrm/config/listener' and 'winrm quickconfig'; verify port $Port is owned by WinRM/HTTP.sys, remove any proxy or port-forward interception, then retry 'Test-WSMan $HostName'."
  } elseif (!$TcpReachable) {
    $blockCode = "winrm_port_unreachable"
    $summary = "WinRM TCP port $Port is not reachable."
    $action = "Start WinRM on the remote machine, allow TCP $Port through the firewall, and retry Test-WSMan $HostName."
  } elseif ($message -match 'Access is denied|authentication|credentials') {
    $blockCode = "winrm_authentication_failed"
    $summary = "WinRM answered, but authentication failed."
    $action = "Verify the username, password, TrustedHosts/domain policy, and that the account may use PowerShell remoting."
  }

  return [pscustomobject][ordered]@{
    status = "blocked"
    stage = "winrm-preflight"
    block_code = $blockCode
    summary = $summary
    action = $action
    host = $HostName
    port = $Port
    tcp_reachable = $TcpReachable
    native_codes = @($nativeCodes | Sort-Object -Unique)
    error = $message
  }
}

function Test-SarTcpPort {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory = $true)]
    [string]$HostName,
    [int]$Port = 5985,
    [ValidateRange(100, 30000)]
    [int]$TimeoutMilliseconds = 2000
  )

  $client = [Net.Sockets.TcpClient]::new()
  try {
    $pending = $client.BeginConnect($HostName, $Port, $null, $null)
    if (!$pending.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) {
      return $false
    }
    $client.EndConnect($pending)
    return $client.Connected
  } catch {
    return $false
  } finally {
    $client.Dispose()
  }
}

Export-ModuleMember -Function Get-SarWinRmFailureDiagnosis, Test-SarTcpPort
