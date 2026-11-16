param(
    [ValidateSet('A', 'B', 'C', 'D')][string]$Variant = 'A',
    [string]$OutputDirectory = '',
    [ValidateSet('Abi', 'Output')][string]$TestFault
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$project = Join-Path $projectRoot 'Game/LiveDevelopment/EnemyAIDiagnostics.vcxproj'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) {
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (!$installation) { throw 'Visual Studio with the C++ build tools is required.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'

$generation = [Guid]::NewGuid().ToString('N')
if ($TestFault -and !$OutputDirectory) { throw 'Use a separate OutputDirectory for faulty test DLLs.' }
$root = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectRoot 'generated/live-enemy-diagnostics' }
New-Item -ItemType Directory -Path $root -Force | Out-Null
$publishLock = [IO.File]::Open((Join-Path $root 'publish.lock'), 'OpenOrCreate', 'Write', 'None')
try {
    $abiHeader = Get-Content -LiteralPath (Join-Path $projectRoot 'Game/LiveDevelopment/EnemyAIDiagnosticsAbi.h') -Raw
    $abi = [regex]::Match($abiHeader, '(?m)^#define ENEMY_AI_DIAGNOSTICS_ABI_VERSION (\d+)u?\s*$')
    if (!$abi.Success) { throw 'Unable to read the DLL ABI version.' }
    $buildVariant = $Variant
    $fixture = @()
    if ($TestFault) {
        if ($root -eq (Join-Path $projectRoot 'generated\live-enemy-diagnostics')) { throw 'Use an isolated test output.' }
        $buildVariant = "TEST_$($TestFault.ToUpperInvariant())"
        $fixture += '/p:LiveDiagnosticsSource=../../tests/LiveDiagnosticsFaultFixture.cpp'
    }
    & $msbuild $project /t:Build /nologo /nr:false /v:minimal /p:Configuration=Debug /p:Platform=x64 /p:PreferredToolArchitecture=x64 "/p:LiveDiagnosticsGeneration=$generation" "/p:LiveDiagnosticsVariant=$buildVariant" "/p:LiveDiagnosticsRoot=$root" @fixture
    if ($LASTEXITCODE -ne 0) { throw "DLL build failed (MSBuild exit $LASTEXITCODE)." }

    $base = Join-Path $root "$generation/EnemyAIDiagnostics-$generation"
    $manifest = [ordered]@{
        schemaVersion = 1
        generation = $generation
        dllSha256 = (Get-FileHash -LiteralPath "$base.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
        pdbSha256 = (Get-FileHash -LiteralPath "$base.pdb" -Algorithm SHA256).Hash.ToLowerInvariant()
        builtAtUtc = [DateTime]::UtcNow.ToString('o')
        variant = $buildVariant
        abiVersion = [uint32]$abi.Groups[1].Value
    }
    $latest = Join-Path $root 'latest.json'
    $draft = Join-Path $root "latest-$generation.tmp"
    try {
        $manifest | ConvertTo-Json | Set-Content -LiteralPath $draft -Encoding ascii
        Copy-Item -LiteralPath $draft -Destination (Join-Path $root "$generation/generation.json")
        if (!$TestFault) {
            try {
                [IO.File]::Move($draft, $latest)
            } catch [IO.IOException] {
                if (!(Test-Path -LiteralPath $latest -PathType Leaf)) { throw }
                [IO.File]::Replace($draft, $latest, [NullString]::Value)
            }
        }
    } finally {
        if (Test-Path -LiteralPath $draft) { Remove-Item -LiteralPath $draft }
    }
    Write-Output "Built variant $buildVariant, generation $generation"
} finally {
    $publishLock.Dispose()
}
