# Amiga Arbor Eval Precondition

## Status

Draft prerequisite plan. Do not start the Arbor optimization loop until this is
implemented and repeat-run deterministic.

Current checkout facts:

- No tracked `arbor/` launch contract files are present.
- No tracked `tools/conformance/run_eval.py` exists.
- No tracked `testdata/golden/tier_a/` or
  `testdata/baseline/winuae_6_0_3/` exists.
- `mnemos_player` already has headless screenshot, save-state, capability,
  asset, CPU-trace, and rendered-audio export surfaces.
- `mnemos_player --run-cycles` is currently CPS2-only, so Amiga fixed-cycle
  evaluation needs a dedicated Amiga probe or a generalized headless cycle path.

## Required Placements

1. Restore the Arbor launch files under `arbor/`:
   - `arbor/mnemos-amiga-superiority.arbor-task.md`
   - `arbor/research_config.yaml`
   - `arbor/mnemos_amiga.plugin.yaml`

2. Add the protected eval entry point:
   - `tools/conformance/run_eval.py`

   Required CLI:

   ```text
   python tools/conformance/run_eval.py --split dev --tier a500_ocs_ce --report build/scratch/arbor/amiga/dev_report.json
   ```

   Required report fields:

   - `ACS`, `PTR`, metric direction, and PTR floor status.
   - trace, framebuffer/audio, and compatibility subscores.
   - per-case pass/fail plus regression-veto state.
   - artifact paths and hashes for trace, framebuffer, audio, and report input.

3. Add a small Amiga fixed-cycle probe executable or tool module:
   - preferred source placement: `tools/amiga_eval_probe/`
   - accepted wrapper placement: called only by `tools/conformance/run_eval.py`

   Required behavior:

   - load Kickstart from a local env-provided path;
   - optionally mount ADF/IPF/WHDLoad test media from a local env-provided path;
   - run an exact master-cycle count through `amiga_adapter::scheduler()`;
   - emit framebuffer, rendered audio, CPU trace, register dumps, and machine
     metadata into `build/scratch/arbor/amiga/<run-id>/`;
   - run twice for determinism and compare artifact hashes.

4. Add eval case manifests:
   - `tools/conformance/amiga_cases.yaml`

   Required contents:

   - `split`: `dev` or `test`;
   - `tier`: `a500_ocs_ce` first;
   - legal asset reference by env/path/hash, not committed ROM data;
   - cycle count, expected outputs, and scoring weights per case.

5. Add protected Tier-A golden metadata and local asset pointers:
   - `testdata/golden/tier_a/amiga/a500_ocs_ce/`

   This directory should contain only redistributable hardware-truth captures,
   hashes, metadata, and scripts/manifests that reference local private assets.
   Kickstart ROMs, commercial disk images, IPF/CAPS images, and WHDLoad archives
   must stay outside the repo.

6. Add protected WinUAE regression baselines:
   - `testdata/baseline/winuae_6_0_3/amiga/a500_ocs_ce/`

   These are regression-only references. They must not be used as the correctness
   oracle when Tier-A evidence disagrees.

7. Keep all transient eval artifacts under:
   - `build/scratch/arbor/amiga/`

## First Runnable Gate

The first acceptable gate is not a superiority result. It is this preflight:

```text
python tools/conformance/run_eval.py --split dev --tier a500_ocs_ce --report build/scratch/arbor/amiga/preflight_dev.json
python tools/conformance/run_eval.py --split dev --tier a500_ocs_ce --report build/scratch/arbor/amiga/preflight_dev_repeat.json
```

Acceptance:

- both commands exit 0;
- both reports have the same ACS/PTR inputs and artifact hashes;
- ACS/PTR are computed from Tier-A goldens, not WinUAE traces;
- PTR below floor makes the report rejectable even if ACS improves;
- any previously passing case regression is represented independently of ACS.

## Approval Needed

Implementing this requires editing paths that were marked protected for the
optimization loop:

- `tools/conformance/`
- `testdata/golden/tier_a/`
- `testdata/baseline/winuae_6_0_3/`

That is acceptable only as prerequisite harness work after explicit approval.
Once the harness exists, Arbor executors must treat those paths as read-only.
