param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) 'generated/outputs/Debug/CG2_Setup.exe' }
$resultPath = Join-Path $projectRoot 'generated/title-tests/result.txt'
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process = Start-Process -FilePath $Executable -ArgumentList '--title-scene-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) {
    Stop-Process -Id $process.Id
    throw 'Title scene GPU tests timed out (180s).'
}
$process.Refresh()
$result = Get-Content -LiteralPath $resultPath -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Title scene GPU tests failed (exit $($process.ExitCode))." }

