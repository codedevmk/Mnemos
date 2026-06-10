---
id: ADR-0008
title: "Save-State Format and the zstd Dependency"
status: accepted
version: 1.0.0
supersedes: []
superseded_by: null
ratified: 2026-05-25
---

# ADR 0008: Save-State Format and the zstd Dependency

**Status:** Accepted for M3 runtime save-state
**Date:** 2026-05-25

## Context

M3 requires deterministic save-state and rewind (TDS §11.4, §11.5, §15). The
runtime must serialise the full machine — every chip's state plus bus RAM — into
a stable, versioned byte stream that reloads to an identical execution point, and
maintain a circular ring of states for rewind.

TDS §15 specifies the container:

```
Header (uncompressed): magic "MNMS", format version, manifest id, manifest rev,
                       master cycle, chunk count.
Payload (zstd):        per chunk -> chunk id, chunk version, size, bytes.
Trailer (uncompressed):CRC32 over the preceding bytes.
```

Unknown chunks are skipped at load for forward compatibility; the payload is
compressed by default. This needs (a) a compression codec and (b) an integrity
checksum.

## Decision

1. **Compression: `zstd`** (`facebook/zstd`, pinned `v1.5.6`) via `FetchContent`,
   built as the static `libzstd_static` target and linked **only** by the runtime
   tier (tier 5). TDS §6.5 already names zstd as the v0.1 codec.
   - **Need:** fast, ratio-good, ubiquitous block compression for save states and
     the rewind ring. Rolling our own is out of scope.
   - **License:** BSD-3-Clause / GPLv2 dual — we use the permissive BSD terms,
     compatible with the Apache-2.0 core and MIT chip tiers.
   - **Isolation:** linked only by `mnemos::runtime`; no tier below it gains a
     compression dependency. The on-disk format treats compression as one framed
     step, so the codec is replaceable behind the format version.

2. **Integrity: CRC32 (IEEE 802.3)** as a header-only foundation utility
   (`mnemos::foundation::crc32`), mirroring the existing header-only `sha256`. The
   trailing CRC32 covers the header + compressed payload; a mismatch rejects the
   state. CRC32 (not SHA-256) because this is a fast corruption check, not a
   cryptographic one, and the table-driven CRC has no third-party dependency.

3. **State serialisation contract:** concrete `state_writer` / `state_reader`
   (tier 2, `mnemos::chips::common`) back the existing `i_chip::save_state` /
   `load_state` hooks. They encode primitives little-endian with bounds-checked
   reads, so chip chunks are self-describing and host-endian-independent — a save
   produced on one platform reloads bit-identically on another (TDS §11.3).

## Consequences

- The runtime gains one compiled third-party dependency (zstd), pinned and
  isolated to tier 5. CI builds zstd from source on each job.
- Foundation gains a header-only CRC32 with no new dependency.
- Every chip implements real `save_state` / `load_state` against the new
  serialiser; the empty M1/M2 stubs are filled in.
- Save states are forward-compatible within a format major version (unknown
  chunks skipped) and cross-platform deterministic.
- Rewind reuses the same per-frame state in a fixed-depth ring (TDS §11.5).
