param([string]$Configuration = 'Debug', [string]$Executable = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe" }
$resultPath = Join-Path $projectRoot 'generated/enemy-ai-tests/scene-result.txt'
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
Set-Content -LiteralPath $resultPath -Value 'PENDING'
$process = Start-Process -FilePath $Executable -ArgumentList '--enemy-ai-scene-test' -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) {
    Stop-Process -Id $process.Id
    throw 'Enemy AI scene tests timed out (180s).'
}
$process.Refresh()
$result = Get-Content -LiteralPath $resultPath -Raw
$result
if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Enemy AI scene tests failed (exit $($process.ExitCode))." }
Copy-Item -LiteralPath $resultPath -Destination (Join-Path (Split-Path $resultPath) "scene-result-$Configuration.txt")
