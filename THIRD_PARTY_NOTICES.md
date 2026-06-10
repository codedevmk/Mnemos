# Third-Party Notices

Mnemos v0.1 uses third-party code only through pinned CMake `FetchContent`
entries documented here and in the relevant ADR.

| Component | Version / pin | License | Consumer | Notes |
|-----------|---------------|---------|----------|-------|
| Catch2 | `v3.8.1` / `56809e5282f104c5c8b570e7c2996cdc352d94f1` | BSL-1.0 | tests | Unit-test framework only. |
| nlohmann/json | `v3.11.3` | MIT | tests | JSON parsing for the CPU conformance harnesses. |
| SDL3 | `release-3.2.0` | Zlib | `mnemos_player` | Window / GPU / audio / input for the player frontend. Built statically. |
| tomlplusplus | `v3.4.0` | MIT | `manifests` | TOML manifest parsing (ADR-0007). |
| zstd | `v1.5.6` | BSD-3-Clause | `runtime` | Save-state compression (ADR-0008). Dual BSD/GPLv2 upstream; consumed under the BSD license only. |

No third-party emulator source is vendored in this repository.
