param(
    [string]$Configuration = 'Debug',
    [string]$Executable = '',
    [switch]$CheckLiveDiagnostics,
    [Alias('CheckLivePhase2')][switch]$CheckLivePhase3
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$Executable) { $Executable = Join-Path (Split-Path $projectRoot) "generated/outputs/$Configuration/CG2_Setup.exe" }
$resultPath = Join-Path $projectRoot 'generated/enemy-ai-tests/scene-result.txt'
New-Item -ItemType Directory -Force (Split-Path $resultPath) | Out-Null
$arguments = @('--enemy-ai-scene-test')
if ($CheckLiveDiagnostics) { $arguments += '--check-live-diagnostics' }
if ($CheckLivePhase3) {
    if ($Configuration -ne 'Debug') { throw 'Live development tests require Debug x64.' }
    $fixtures = Join-Path $projectRoot 'generated/live-phase2-tests'
    New-Item -ItemType Directory -Path $fixtures -Force | Out-Null
    $run = Join-Path $fixtures ([Guid]::NewGuid().ToString('N'))
    foreach ($variant in 'A', 'B', 'C', 'D') {
        & (Join-Path $PSScriptRoot 'build-live-enemy-diagnostics.ps1') -Variant $variant -OutputDirectory $run
    }
    foreach ($fault in 'Abi', 'Output') {
        & (Join-Path $PSScriptRoot 'build-live-enemy-diagnostics.ps1') -TestFault $fault -OutputDirectory $run
    }
    Set-Content -LiteralPath (Join-Path $fixtures 'run.txt') -Value (Split-Path $run -Leaf) -Encoding ascii
    $arguments += '--check-live-phase2'
}
function Invoke-SceneTest([string[]]$LaunchArguments) {
    Set-Content -LiteralPath $resultPath -Value 'PENDING'
    $process = Start-Process -FilePath $Executable -ArgumentList $LaunchArguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(180000)) {
        Stop-Process -Id $process.Id
        throw 'Enemy AI scene tests timed out (180s).'
    }
    $process.Refresh()
    $result = Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8
    if ($process.ExitCode -ne 0 -or !$result.StartsWith('PASS:')) {
        throw "Enemy AI scene tests failed (exit $($process.ExitCode)). $result"
    }
    return $result
}
$result = Invoke-SceneTest $arguments
if ($CheckLivePhase3) {
    $result += Invoke-SceneTest @('--enemy-ai-scene-test', '--check-live-phase2-restart')
    $result += Get-Content -LiteralPath (Join-Path $run 'lua-result.txt') -Raw -Encoding UTF8
    [IO.File]::WriteAllText($resultPath, $result)
}
$result
Copy-Item -LiteralPath $resultPath -Destination (Join-Path (Split-Path $resultPath) "scene-result-$Configuration.txt")
