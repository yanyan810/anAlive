param([string]$Configuration='Debug', [string]$Stage='stage01')
$ErrorActionPreference='Stop'
if ($Stage -notin @('stage01','stage02','stage03')) { throw 'Invalid Stage' }
$projectRoot=Split-Path $PSScriptRoot
$executable=Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe"
$resultPath=Join-Path $projectRoot "generated/camera-tests/$Stage/result.txt"
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process=Start-Process -FilePath $executable -ArgumentList "--camera-sequence-test --camera-$Stage" -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) {
    Stop-Process -Id $process.Id
    throw 'Camera runtime tests timed out (180s).'
}
$process.Refresh()
$result=Get-Content -LiteralPath $resultPath -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) {
    $failure=Join-Path $projectRoot 'generated/camera-tests/runtime-failure.txt'
    if (Test-Path $failure) { Get-Content -LiteralPath $failure }
    throw "Camera runtime tests failed (exit $($process.ExitCode))."
}
Copy-Item -LiteralPath $resultPath -Destination (Join-Path (Split-Path $resultPath) "result-$Configuration.txt")
$captureDirectory=Join-Path (Split-Path $resultPath) $Configuration
New-Item -ItemType Directory -Force $captureDirectory | Out-Null
Get-ChildItem -LiteralPath (Split-Path $resultPath) -Filter '*.png' -File | Copy-Item -Destination $captureDirectory
exit 0
