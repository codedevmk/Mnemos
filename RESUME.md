# Mnemos Mainline Resume

Updated: 2026-06-29 America/Chicago

Workspace: `C:\dev\emu\Mnemos`
Branch: `master`

This file is a lightweight handoff for the mainline checkout after long-running
feature-branch work. Treat the live git graph, current source, and current test
results as authoritative. Branch-specific handoff details age quickly once a
feature branch is merged.

## Current Resume Rules

- Start with `CONSTITUTION.md`, then `README.md`.
- Verify `git status --short --branch` and `git rev-list --left-right --count
  master...origin/master` before assuming this checkout is ready to publish.
- Use the README Windows validation path from a Visual Studio developer
  environment:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

## Recently Merged Feature Lines

- `feature/cps2`
- `feature/msx2`
- `feature/taito-arcade`
- `feature/irem-arcade`

Use `git log --oneline --decorate --first-parent master` for the exact merge
commit order and `git branch --merged master` for current branch inclusion.

## Evidence Discipline

Do not claim emulator-family completion from this file. For any system family,
use the current unit tests, data-gated scripts, and real-media proof paths in
`scripts/run-data-gated-tests.ps1` and the relevant `scripts/<family>/`
subdirectory.
