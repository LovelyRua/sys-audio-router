param(
  [string]$HostName = "192.168.123.3",
  [string]$UserName = "codex",
  [Parameter(Mandatory = $true)]
  [string]$Password
)

$ErrorActionPreference = "Stop"

$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$credential = [pscredential]::new($UserName, $securePassword)

$session = $null
try {
  $session = New-PSSession -ComputerName $HostName -Credential $credential

  Invoke-Command -Session $session -ScriptBlock {
    $ErrorActionPreference = "Continue"
    $bootstrap = "C:\Windows\Temp\sar-bootstrap.cmd"
    $url = "https://raw.githubusercontent.com/LovelyRua/sys-audio-router/main/scripts/windows-test-bootstrap.cmd"

    curl.exe --silent --show-error -L $url -o $bootstrap
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to download bootstrap with exit code $LASTEXITCODE."
    }

    cmd.exe /c "$bootstrap 2>&1"
    if ($LASTEXITCODE -ne 0) {
      throw "Bootstrap failed with exit code $LASTEXITCODE."
    }
  }
} finally {
  if ($null -ne $session) {
    Remove-PSSession $session
  }
}
