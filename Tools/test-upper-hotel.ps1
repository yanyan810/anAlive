param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
$executable = Join-Path (Split-Path $project) "generated/outputs/$Configuration/CG2_Setup.exe"
$results = Join-Path $project 'generated/upper-hotel-tests'
New-Item -ItemType Directory -Force $results | Out-Null
$result = Join-Path $results 'result.txt'
Set-Content -LiteralPath $result -Value 'PENDING'
$process = Start-Process -FilePath $executable -ArgumentList '--upper-hotel-test' -WorkingDirectory $project -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) {
    Stop-Process -Id $process.Id
    throw 'Upper hotel runtime tests timed out.'
}
$process.Refresh()
$report = Get-Content -LiteralPath $result -Raw
$report
if ($process.ExitCode -ne 0 -or !$report.StartsWith('PASS:')) {
    throw "Upper hotel runtime tests failed (exit $($process.ExitCode))."
}
Copy-Item -LiteralPath $result -Destination (Join-Path $results "result-$Configuration.txt")
