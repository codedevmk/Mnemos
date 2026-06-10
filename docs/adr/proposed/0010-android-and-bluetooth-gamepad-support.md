---
id: ADR-0010
title: "Android and Bluetooth Gamepad Support"
status: proposed
version: 1.0.0
supersedes: []
superseded_by: null
proposed: 2026-06-01
---

# ADR 0010: Android and Bluetooth Gamepad Support

**Status:** Proposed
**Date:** 2026-06-01

## Context

Mnemos v0.1 targets Windows 11 x64 and Linux x64 only; macOS, ARM64, and
console targets are explicitly out of scope. There is demand to run Mnemos on
Android phones and tablets, and to use Bluetooth game controllers as input.

Two properties of the existing architecture make this tractable:

- Tiers 1–6 (foundation through instrumentation) are headless and
  platform-agnostic. The deterministic core has no UI and uses
  `std::expected<T>` rather than exceptions/RTTI in the chip library.
- The player frontend already depends on **SDL3**, which has a first-class
  Android backend (Activity lifecycle, Vulkan surface creation, audio, and
  input), and on **Vulkan**, which Android supports natively.

The frontend already drives input through SDL3's high-level `SDL_Gamepad` API
(`SDL_GetGamepads`, `SDL_OpenGamepad`, `SDL_GetGamepadButton`,
`SDL_GetGamepadAxis`) with `SDL_INIT_GAMEPAD` and hot-plug handling
(`SDL_EVENT_GAMEPAD_ADDED` / `SDL_EVENT_GAMEPAD_REMOVED`). Physical input is
normalised into the device-agnostic `frontend_sdk::controller_state` struct and
applied via `player_system::apply_input`. Peripheral adapters and emulated pad
devices consume `controller_state` and are unaware of the physical device.

The dependency stack (SDL3, Vulkan, Lua 5.4 + sol2, tomlplusplus, zstd,
Cap'n Proto, Catch2) is portable C/C++ and builds with the Android NDK
(r27+, Clang 18+), which covers the project's C++23 requirement.

## Decision

Add Android as a supported runtime via a **native NDK application built on
SDL3's Android backend**, reusing the existing deterministic core and
`frontend_sdk` layer unchanged. We explicitly reject a WebAssembly/WebView
wrapper (insufficient performance for cycle-accurate emulation) and a full
Kotlin/Jetpack Compose rewrite over JNI (discards the SDL3 frontend for
marginal platform-integration gain).

Work proceeds in dependency order:

1. **ARM64 core correctness first.** Add an aarch64 build and run the existing
   golden-frame regression suite on it before any Android packaging. This
   proves the deterministic core is bit-correct on ARM and flushes out any
   x86-specific SIMD/intrinsics in `dsp/`, `audio/`, and `video/` (scalar or
   NEON fallbacks). ARM is little-endian, so byte-order logic is unaffected.
2. **Android build path.** Wrap the existing CMake build with Gradle
   `externalNativeBuild` targeting `arm64-v8a` (and `x86_64` for the emulator),
   using the SDL3 Android Gradle template as the shell. No fork of the build
   logic — Gradle points at the same `CMakeLists.txt`.
3. **Android-aware storage.** Extend the `foundation/fs` facade and ROM loaders
   to handle app-specific directories, APK-bundled assets, and the Storage
   Access Framework (SAF) for user-selected ROMs. The `tcp_transport` modem's
   BSD sockets work as-is given the `INTERNET` permission.
4. **Touch input adapter.** Add an on-screen gamepad overlay in `frontend_sdk`
   (Tier 7) mapping to the existing `controller_state` model.
5. **Mobile UX.** Screen scaling across DPI/aspect ratios, save-state UI, and
   pause-on-background per the Android lifecycle.

The Android entry point lives as a sibling app target under
`src/apps/` (e.g. `apps/player_android/`) sharing `frontend_sdk`, rather than
`#ifdef`-ing the desktop player.

**Bluetooth gamepad support** is adopted as a cross-platform capability of the
existing SDL3 `SDL_Gamepad` integration: paired controllers are surfaced by the
OS and appear through the API already in use, so no new device code is
required. To make this usable across heterogeneous controllers (and alongside
the Android touch overlay), add a **button-remapping configuration layer**
(persisted mapping from SDL gamepad buttons/axes to `controller_state` fields),
replacing the current hard-coded mappings in the player. SDL's controller
mapping database is bundled so unknown controllers are recognised.

## Consequences

- The headless core (Tiers 1–6) remains the single source of truth across
  desktop and mobile; only Tier 1 platform shims (`fs`) and Tiers 7–8 gain
  Android-aware code.
- ARM64 becomes a supported architecture in CI (a headless core + tests build
  on a native aarch64 runner), expanding the build matrix and CI cost. The
  foundation/chip/runtime unit and conformance tests gate ARM correctness today;
  the golden-frame boot tests self-skip in CI when ROM env vars are absent, so
  they only validate ARM once ROMs/test vectors are made available to CI.
- A new Gradle/Android Studio toolchain and signing/packaging pipeline join the
  build surface, beyond the current CMake-only flow.
- Bluetooth controller support lands on all platforms (not just Android) as a
  side effect of the remapping layer; the layer is new surface area that must
  be persisted, versioned, and tested.
- Mobile UX work (touch overlay, lifecycle handling) is genuinely new frontend
  code with no desktop equivalent.
- Scope grows beyond the v0.1 Windows/Linux-x64 boundary; this ADR records that
  deliberate expansion.
