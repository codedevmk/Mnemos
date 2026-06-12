#!/bin/sh
# Installs the repo-hygiene git hooks (ADR-0025) by pointing core.hooksPath at the
# tracked tools/githooks/ directory. Run once per clone. Windows: install-hooks.ps1
root=$(git rev-parse --show-toplevel) || exit 1
git -C "$root" config core.hooksPath tools/githooks
echo "repo-hygiene: git hooks installed (core.hooksPath -> tools/githooks)."
