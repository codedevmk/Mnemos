#!/usr/bin/env pwsh
# Installs the repo-hygiene git hooks (ADR-0025) by pointing core.hooksPath at the
# tracked tools/githooks/ directory. Run once per clone. Linux/macOS: install-hooks.sh
$root = (git rev-parse --show-toplevel)
git -C "$root" config core.hooksPath tools/githooks
Write-Host "repo-hygiene: git hooks installed (core.hooksPath -> tools/githooks)."
