param([string]$Configuration = 'Debug', [string]$Executable = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe" }
$testDirectory = Join-Path $projectRoot 'generated/enemy-ai-tests'
New-Item -ItemType Directory -Force $testDirectory | Out-Null
foreach ($stage in @('stage01','stage02','stage03')) {
    $resultPath = Join-Path $testDirectory "$stage-smoke.txt"
    Set-Content -LiteralPath $resultPath -Value 'PENDING'
    $process = Start-Process -FilePath $Executable -ArgumentList "--enemy-ai-$stage-smoke" -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(180000)) { Stop-Process -Id $process.Id; throw "Timeout: $stage" }
    $process.Refresh()
    $result = Get-Content -LiteralPath $resultPath -Raw
    $result
    if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) { throw "Stage smoke test failed: $stage" }
    Copy-Item -LiteralPath $resultPath -Destination (Join-Path $testDirectory "$stage-smoke-$Configuration.txt")
}
