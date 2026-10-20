param([string]$BlenderPath='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
if (!$BlenderPath) {
    $install=Get-ChildItem -LiteralPath "$env:ProgramFiles/Blender Foundation" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $BlenderPath=Join-Path $install.FullName 'blender.exe'
}
$env:BLENDER_USER_RESOURCES=Join-Path $projectRoot 'generated/blender-camera-user'
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $PSScriptRoot 'blender/test_camera_sequences.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# This pass requires all Blender references, then compares the native evaluator.
foreach ($name in @('stage01-samples','stage02-samples','stage03-samples','mixed-reference')) {
    if (!(Test-Path (Join-Path $projectRoot "generated/camera-tests/$name.json"))) { throw "Missing Blender reference: $name" }
}
& (Join-Path $PSScriptRoot 'test-camera-sequence.ps1')
exit $LASTEXITCODE
