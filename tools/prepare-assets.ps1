param([string[]]$Models = @('MyGtYUhe6t/安比.pmx', 'ema/SakurabaEma_ByPOWER.pmx'), [string]$Executable = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) 'generated/outputs/Release/CG2_Setup.exe' }
$output = Join-Path $projectRoot 'generated/loading'
New-Item -ItemType Directory -Force $output | Out-Null
ConvertTo-Json -InputObject @($Models) | Set-Content -LiteralPath (Join-Path $output 'prepare-list.json') -Encoding utf8
Set-Content -LiteralPath (Join-Path $output 'prepare-result.txt') -Value 'PENDING'
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $Executable
$info.Arguments = '--prepare-assets'
$info.WorkingDirectory = $projectRoot
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$info.Environment['YAN_ASSET_CACHE'] = '1'
$info.Environment['YAN_LOAD_PROFILE'] = '1'
$process = [Diagnostics.Process]::Start($info)
if (!$process.WaitForExit(300000)) {
    Stop-Process -Id $process.Id
    throw 'Asset preparation timed out (300s).'
}
$result = Get-Content -LiteralPath (Join-Path $output 'prepare-result.txt') -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Asset preparation failed (exit $($process.ExitCode))." }
