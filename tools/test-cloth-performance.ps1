param([string]$Executable = '', [string]$Label = 'current')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid report label.' }
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) 'generated/outputs/Debug/CG2_Setup.exe' }
$resultPath = Join-Path $projectRoot 'generated/cloth-tests/performance-result.txt'
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process = Start-Process -FilePath $Executable -ArgumentList '--cloth-performance-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) {
    Stop-Process -Id $process.Id
    throw 'Cloth performance tests timed out (180s).'
}
$process.Refresh()
$result = Get-Content -LiteralPath $resultPath -Raw
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw $result }
$csv = Join-Path $projectRoot 'generated/cloth-tests/performance.csv'
Copy-Item -LiteralPath $csv -Destination (Join-Path (Split-Path $csv) "performance-$Label.csv") -Force
Get-Content -LiteralPath $csv
$result
