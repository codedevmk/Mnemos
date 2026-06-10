---
id: ADR-0002
title: "CMake and Toolchain"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-22
---

# ADR 0002: CMake and Toolchain

**Status:** Accepted for M0 scaffold
**Date:** 2026-05-22

## Context

Mnemos needs one build contract that works on Windows and Linux, supports strict
C++23 warning policy, and gives agents repeatable configure/build/test commands.

## Decision

Use CMake 3.28+, Ninja, and `CMakePresets.json`.

The M0 presets cover:

- `windows-msvc-debug`
- `windows-msvc-release`
- `windows-msvc-relwithdebinfo`
- `linux-gcc-debug`
- `linux-gcc-release`
- `linux-clang-debug`
- `linux-clang-release`
- `linux-clang-asan`

Project CMake modules provide tier validation, warning flags, and Catch2 test
registration.

CMake presets write build trees to `build/<preset>`.

## Consequences

- The root stays solution-file independent.
- CI and local agents use the same named presets.
- Windows Ninja builds require a Visual Studio developer environment with `cl`
  available on `PATH`.
