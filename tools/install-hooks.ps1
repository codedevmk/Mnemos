Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$targetDir = Join-Path $repoRoot ".git\hooks"

if (-not (Test-Path -LiteralPath $targetDir)) {
    throw "Missing git hooks directory: $targetDir"
}

foreach ($hook in @("pre-commit", "commit-msg")) {
    $source = Join-Path $repoRoot "tools\git-hooks\$hook"
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Missing hook source: $source"
    }
    $target = Join-Path $targetDir $hook
    Copy-Item -LiteralPath $source -Destination $target -Force
    Write-Host "Installed $hook hook to $target"
}
