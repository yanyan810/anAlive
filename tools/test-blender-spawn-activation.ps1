param([string]$BlenderPath = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
if (!$BlenderPath) {
    $blenderCommand = Get-Command blender -ErrorAction SilentlyContinue
    if ($blenderCommand) { $BlenderPath = $blenderCommand.Source }
    else {
        $blenderInstall = Get-ChildItem -LiteralPath "$env:ProgramFiles/Blender Foundation" -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($blenderInstall) { $BlenderPath = Join-Path $blenderInstall.FullName 'blender.exe' }
    }
}
if (!$BlenderPath -or !(Test-Path -LiteralPath $BlenderPath)) { throw 'Blender not found. Pass -BlenderPath explicitly.' }
$env:BLENDER_USER_RESOURCES = Join-Path $projectRoot 'generated/blender-registration-user'
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $PSScriptRoot 'blender/test_level_addon_registration.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:BLENDER_USER_RESOURCES = Join-Path $projectRoot 'generated/blender-user'
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $PSScriptRoot 'blender/test_spawn_activation.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $PSScriptRoot 'blender/test_spawn_composition.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot 'test-enemy-spawn.ps1')
exit $LASTEXITCODE
