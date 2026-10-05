$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$installation = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
$testDir = Join-Path $projectRoot 'generated/tests'
New-Item -ItemType Directory -Force $testDir | Out-Null
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $env:ComSpec
$info.Arguments = '/d /s /c ""' + $vcvars + '" >nul && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I"' + $projectRoot + '/Game/enemy" /I"' + $projectRoot + '/Engine/math" /I"' + $projectRoot + '/externals/assimp/include" "' + $projectRoot + '/tests/EnemyProceduralAnimationTests.cpp" "' + $projectRoot + '/Engine/math/Matrix4x4.cpp" /Fe:EnemyProceduralAnimationTests.exe && EnemyProceduralAnimationTests.exe"'
$info.WorkingDirectory = $testDir
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$pathValue = $env:PATH
[void]$info.Environment.Remove('PATH')
[void]$info.Environment.Remove('Path')
$info.Environment['Path'] = $pathValue
$process = [Diagnostics.Process]::Start($info)
$outputTask = $process.StandardOutput.ReadToEndAsync()
$errorTask = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
$outputTask.Result
$errorTask.Result
exit $process.ExitCode


