#!/usr/bin/env pwsh
# Clear the repo-local scratch dir of debug artifacts.
#
# scratch/ is the canonical, git-ignored home for everything a debugging
# session produces: headless --screenshot framebuffers, VRAM/CRAM dumps,
# trace CSVs, ad-hoc logs, coverage profraw (see AGENTS.md "Scratch artifacts").
# Run this whenever it grows; nothing here touches tracked files.
#
# Examples:
#   ./scripts/clean-scratch.ps1                 # empty scratch/ entirely
#   ./scripts/clean-scratch.ps1 -OlderThanDays 7  # purge only files >7 days old
#   ./scripts/clean-scratch.ps1 -Deep           # also sweep stray root droppings
#   ./scripts/clean-scratch.ps1 -WhatIf         # preview without deleting

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    # Only remove files last written more than N days ago. 0 = remove everything.
    [int]$OlderThanDays = 0,

    # Also remove known ad-hoc droppings that historically landed in the repo
    # root (coverage profraw, *.log) instead of scratch/. Legacy session dirs
    # such as scratch_sms/ are deliberately left alone.
    [switch]$Deep
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$scratch = Join-Path $repoRoot 'scratch'

# Resolve the age cutoff. With -OlderThanDays 0 the cutoff is "now", so every
# existing file qualifies (LastWriteTime is always <= now).
$cutoff = (Get-Date).AddDays(-$OlderThanDays)

function Remove-Stale {
    param([string]$Root, [string]$Label)

    if (-not (Test-Path -LiteralPath $Root)) { return }

    $victims = Get-ChildItem -LiteralPath $Root -Recurse -File -Force |
        Where-Object { $_.LastWriteTime -le $cutoff }

    if (-not $victims) {
        Write-Host "  ${Label}: nothing to clean." -ForegroundColor DarkGray
        return
    }

    $bytes = ($victims | Measure-Object -Property Length -Sum).Sum
    foreach ($f in $victims) {
        if ($PSCmdlet.ShouldProcess($f.FullName, 'Remove')) {
            Remove-Item -LiteralPath $f.FullName -Force
        }
    }

    # Drop now-empty subdirectories (but keep $Root itself).
    Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force |
        Sort-Object { $_.FullName.Length } -Descending |
        Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } |
        ForEach-Object {
            if ($PSCmdlet.ShouldProcess($_.FullName, 'Remove empty dir')) {
                Remove-Item -LiteralPath $_.FullName -Force
            }
        }

    $mb = [math]::Round($bytes / 1MB, 1)
    Write-Host ("  {0}: removed {1} file(s), {2} MB." -f $Label, $victims.Count, $mb) -ForegroundColor Green
}

Write-Host "Cleaning scratch artifacts under $repoRoot" -ForegroundColor Cyan
if ($OlderThanDays -gt 0) {
    Write-Host "  (only files older than $OlderThanDays day(s))" -ForegroundColor DarkGray
}

if (Test-Path -LiteralPath $scratch) {
    Remove-Stale -Root $scratch -Label 'scratch/'
} else {
    Write-Host "  scratch/: does not exist yet -- nothing to clean." -ForegroundColor DarkGray
}

if ($Deep) {
    # Known ad-hoc droppings that predate the scratch/ convention. Patterns are
    # matched at the repo root only; tracked files never match these globs.
    $rootDroppings = @('*.profraw', '*.profdata', 'dma.log', 'watch*.log', 'fps_*.log')
    $found = foreach ($pat in $rootDroppings) {
        Get-ChildItem -LiteralPath $repoRoot -Filter $pat -File -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -le $cutoff }
    }
    if ($found) {
        foreach ($f in $found) {
            if ($PSCmdlet.ShouldProcess($f.FullName, 'Remove root dropping')) {
                Remove-Item -LiteralPath $f.FullName -Force
            }
        }
        Write-Host ("  root droppings: removed {0} file(s)." -f @($found).Count) -ForegroundColor Green
    } else {
        Write-Host "  root droppings: none found." -ForegroundColor DarkGray
    }
}

Write-Host "Done." -ForegroundColor Cyan
