param([string]$Configuration='Debug',[string]$Executable='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
if (!$Executable) { $Executable=Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe" }
$resultPath=Join-Path $projectRoot 'generated/placement-tests/result.txt'
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process=Start-Process -FilePath $Executable -ArgumentList '--enemy-placement-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
for ($i=0;$i -lt 4 -and !$process.WaitForExit(45000);$i++) { Write-Output 'Placement runtime test is running...' }
if (!$process.HasExited) { Stop-Process -Id $process.Id; throw 'Placement runtime tests timed out.' }
$process.Refresh()
$result=Get-Content -LiteralPath $resultPath -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Placement runtime test failed (exit $($process.ExitCode))." }
Copy-Item -LiteralPath $resultPath -Destination (Join-Path (Split-Path $resultPath) "result-$Configuration.txt")
Copy-Item -LiteralPath (Join-Path (Split-Path $resultPath) 'camera-editor-result.txt') -Destination (Join-Path (Split-Path $resultPath) "camera-editor-result-$Configuration.txt")
Copy-Item -LiteralPath (Join-Path (Split-Path $resultPath) 'camera-path-result.txt') -Destination (Join-Path (Split-Path $resultPath) "camera-path-result-$Configuration.txt")
Copy-Item -LiteralPath (Join-Path (Split-Path $resultPath) 'descriptors.txt') -Destination (Join-Path (Split-Path $resultPath) "descriptors-$Configuration.txt")
$captureDirectory=Join-Path (Split-Path $resultPath) $Configuration
New-Item -ItemType Directory -Force $captureDirectory | Out-Null
Get-ChildItem -LiteralPath (Split-Path $resultPath) -Filter '*.png' -File | Copy-Item -Destination $captureDirectory
exit 0
