[CmdletBinding()]
param(
    [string] $SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [Parameter(Mandatory = $true)]
    [string] $BuildDir,
    [double] $MinimumBranchCoverage = 90.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-Tool {
    param([string] $Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        if ($IsWindows) {
            $vsLlvm = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin'
            $toolName = if ($Name.EndsWith('.exe')) { $Name } else { "$Name.exe" }
            $candidate = Join-Path $vsLlvm $toolName
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }

        throw "Required tool not found: $Name"
    }

    return $command.Source
}

$sourceRoot = (Resolve-Path -LiteralPath $SourceDir).Path
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$testDir = Join-Path $buildRoot 'src/foundation'
$testBinary = Get-ChildItem -LiteralPath $testDir -File |
    Where-Object { $_.BaseName -eq 'mnemos_foundation_smoke_test' } |
    Select-Object -First 1

if ($null -eq $testBinary) {
    throw "Foundation test binary not found under $testDir"
}

$coverageDir = Join-Path $buildRoot 'coverage/foundation'
New-Item -ItemType Directory -Path $coverageDir -Force | Out-Null

$profilePattern = Join-Path $coverageDir 'foundation-%p.profraw'
$profileData = Join-Path $coverageDir 'foundation.profdata'
$summaryPath = Join-Path $coverageDir 'foundation-summary.json'

Remove-Item -LiteralPath $profileData -Force -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $coverageDir -Filter '*.profraw' -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

$oldProfile = $env:LLVM_PROFILE_FILE
try {
    $env:LLVM_PROFILE_FILE = $profilePattern
    & $testBinary.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Foundation tests failed with exit code $LASTEXITCODE"
    }
}
finally {
    $env:LLVM_PROFILE_FILE = $oldProfile
}

$profraw = @(Get-ChildItem -LiteralPath $coverageDir -Filter '*.profraw' -File)
if ($profraw.Count -eq 0) {
    throw "No LLVM profile data was written under $coverageDir"
}

$llvmProfdata = Resolve-Tool 'llvm-profdata'
$llvmCov = Resolve-Tool 'llvm-cov'

& $llvmProfdata merge -sparse @($profraw.FullName) -o $profileData
if ($LASTEXITCODE -ne 0) {
    throw "llvm-profdata merge failed with exit code $LASTEXITCODE"
}

$foundationHeaders = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'src/foundation/include') -Recurse -File -Filter '*.hpp'
$coverageJson = & $llvmCov export -summary-only -format=text $testBinary.FullName "-instr-profile=$profileData" @($foundationHeaders.FullName)
if ($LASTEXITCODE -ne 0) {
    throw "llvm-cov export failed with exit code $LASTEXITCODE"
}

$coverageJson | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary = ($coverageJson | ConvertFrom-Json).data[0].totals
$branchCoverage = [double] $summary.branches.percent

"Foundation branch coverage: {0:N2}% (minimum {1:N2}%)" -f $branchCoverage, $MinimumBranchCoverage

if ($branchCoverage -lt $MinimumBranchCoverage) {
    throw "Foundation branch coverage is below the required threshold."
}
