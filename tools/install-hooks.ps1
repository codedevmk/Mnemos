Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$source = Join-Path $repoRoot "tools\git-hooks\pre-commit"
$targetDir = Join-Path $repoRoot ".git\hooks"
$target = Join-Path $targetDir "pre-commit"

if (-not (Test-Path -LiteralPath $source)) {
    throw "Missing hook source: $source"
}

if (-not (Test-Path -LiteralPath $targetDir)) {
    throw "Missing git hooks directory: $targetDir"
}

Copy-Item -LiteralPath $source -Destination $target -Force
Write-Host "Installed pre-commit hook to $target"
