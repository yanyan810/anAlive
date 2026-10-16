param([string]$Configuration = 'Debug', [string]$Executable = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe" }
$resultPath = Join-Path $projectRoot 'generated/hotel-tests/result.txt'
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process = Start-Process -FilePath $Executable -ArgumentList '--hotel-stage-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(600000)) {
    Stop-Process -Id $process.Id
    throw 'Hotel runtime tests timed out (600s).'
}
$process.Refresh()
$result = Get-Content -LiteralPath $resultPath -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Hotel runtime tests failed (exit $($process.ExitCode))." }
Copy-Item -LiteralPath $resultPath -Destination (Join-Path (Split-Path $resultPath) "result-$Configuration.txt")
$captureDirectory=Join-Path (Split-Path $resultPath) $Configuration
New-Item -ItemType Directory -Force $captureDirectory | Out-Null
Get-ChildItem -LiteralPath (Split-Path $resultPath) -Filter '*.png' -File |
    Copy-Item -Destination $captureDirectory
Copy-Item -LiteralPath (Join-Path (Split-Path $resultPath) 'route-trace.csv') -Destination $captureDirectory
$regressionPath=Join-Path (Split-Path $resultPath) 'regression.txt'
Set-Content -LiteralPath $regressionPath -Value 'PENDING'
$regression=Start-Process -FilePath $Executable -ArgumentList '--hotel-stage-test','--hotel-regression-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if (!$regression.WaitForExit(600000)) {
    Stop-Process -Id $regression.Id
    throw 'Hotel stage regressions timed out (600s).'
}
$regression.Refresh()
$regressionResult=Get-Content -LiteralPath $regressionPath -Raw
$regressionResult
if ($regression.ExitCode -ne 0 -or !$regressionResult.StartsWith('PASS:')) { throw "Stage02/Showroom regressions failed (exit $($regression.ExitCode))." }
Copy-Item -LiteralPath $regressionPath -Destination (Join-Path (Split-Path $resultPath) "regression-$Configuration.txt")
