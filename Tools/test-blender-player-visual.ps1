param([string]$BlenderPath='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
if (!$BlenderPath) {
    $install=Get-ChildItem -LiteralPath "$env:ProgramFiles/Blender Foundation" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $BlenderPath=Join-Path $install.FullName 'blender.exe'
}
$env:BLENDER_USER_SCRIPTS=Join-Path $projectRoot 'generated/player-visual-blender/scripts'
$env:BLENDER_USER_CONFIG=Join-Path $projectRoot 'generated/player-visual-blender/config'
New-Item -ItemType Directory -Force $env:BLENDER_USER_SCRIPTS,$env:BLENDER_USER_CONFIG | Out-Null
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $PSScriptRoot 'blender/test_player_visual_export.py')
exit $LASTEXITCODE
