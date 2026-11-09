param([string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
$executable=Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe"
$testDirectory=Join-Path $projectRoot 'generated/enemy-stairs-tests'
New-Item -ItemType Directory -Force $testDirectory | Out-Null
$resultPath=Join-Path $testDirectory 'result.txt'
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process=Start-Process -FilePath $executable -ArgumentList '--hotel-enemy-stairs-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
for ($i=0;$i -lt 8 -and !$process.WaitForExit(45000);$i++) { Write-Output 'Hotel enemy stair test is running...' }
if (!$process.HasExited) { Stop-Process -Id $process.Id; throw 'Hotel enemy stair tests timed out.' }
$process.Refresh()
$result=Get-Content -LiteralPath $resultPath -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Hotel enemy stair test failed (exit $($process.ExitCode))." }
$captureDirectory=Join-Path $testDirectory $Configuration
New-Item -ItemType Directory -Force $captureDirectory | Out-Null
Get-ChildItem -LiteralPath $testDirectory -Filter '*.png' -File | Copy-Item -Destination $captureDirectory
Copy-Item -LiteralPath $resultPath -Destination (Join-Path $captureDirectory 'result.txt')
Copy-Item -LiteralPath (Join-Path $testDirectory 'trace.csv') -Destination $captureDirectory
