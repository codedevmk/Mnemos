#!/usr/bin/env python3
"""Deterministic Amiga conformance harness for Arbor/Mnemos eval runs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CASES = REPO_ROOT / "tools" / "conformance" / "amiga_cases.yaml"
ARTIFACT_NAMES = {
    "trace": "trace.csv",
    "framebuffer": "framebuffer.ppm",
    "audio": "audio.s16le",
    "metadata": "metadata.json",
}


class HarnessError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_json_subset_yaml(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    try:
        data = json.loads(text)
    except json.JSONDecodeError as json_error:
        try:
            import yaml  # type: ignore[import-not-found]
        except ImportError as import_error:
            raise HarnessError(
                f"{path} must be JSON-subset YAML unless PyYAML is already installed"
            ) from import_error
        data = yaml.safe_load(text)
        if data is None:
            raise HarnessError(f"{path} is empty") from json_error
    if not isinstance(data, dict):
        raise HarnessError(f"{path} must contain a mapping")
    return data


def find_probe(explicit: str | None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env_probe = os.environ.get("MNEMOS_AMIGA_EVAL_PROBE")
    if env_probe:
        candidates.append(Path(env_probe))

    exe_name = "mnemos_amiga_eval_probe.exe" if platform.system() == "Windows" else "mnemos_amiga_eval_probe"
    candidates.append(REPO_ROOT / "build" / "windows-msvc-debug" / "tools" / "amiga_eval_probe" / exe_name)
    candidates.append(REPO_ROOT / "build" / "windows-msvc-release" / "tools" / "amiga_eval_probe" / exe_name)
    candidates.append(REPO_ROOT / "build" / "linux-gcc-debug" / "tools" / "amiga_eval_probe" / exe_name)
    candidates.append(REPO_ROOT / "build" / "linux-gcc-release" / "tools" / "amiga_eval_probe" / exe_name)

    for candidate in candidates:
        resolved = candidate if candidate.is_absolute() else REPO_ROOT / candidate
        if resolved.is_file():
            return resolved

    build_root = REPO_ROOT / "build"
    if build_root.is_dir():
        for child in build_root.iterdir():
            path = child / "tools" / "amiga_eval_probe" / exe_name
            if path.is_file():
                return path
    return None


def resolve_asset_path(case: dict[str, Any], key: str, errors: list[str]) -> Path | None:
    direct = case.get(key)
    env_key = case.get(f"{key}_env")
    if direct:
        path = Path(str(direct))
    elif env_key:
        value = os.environ.get(str(env_key))
        if not value:
            errors.append(f"{case['id']}: ${env_key} is not set")
            return None
        path = Path(value)
    else:
        errors.append(f"{case['id']}: no {key} or {key}_env configured")
        return None
    if not path.is_absolute():
        path = REPO_ROOT / path
    if not path.is_file():
        errors.append(f"{case['id']}: {key} does not exist: {path}")
        return None
    return path


def case_disk_paths(case: dict[str, Any], errors: list[str]) -> list[Path] | None:
    disks: list[Path] = []
    for index, spec in enumerate(case.get("disks", [])):
        path: Path | None = None
        if isinstance(spec, str):
            path = Path(spec)
        elif isinstance(spec, dict):
            if spec.get("path"):
                path = Path(str(spec["path"]))
            elif spec.get("env"):
                value = os.environ.get(str(spec["env"]))
                if not value:
                    errors.append(f"{case['id']}: disk {index} env ${spec['env']} is not set")
                    return None
                path = Path(value)
        if path is None:
            errors.append(f"{case['id']}: disk {index} has no path/env")
            return None
        if not path.is_absolute():
            path = REPO_ROOT / path
        if not path.is_file():
            errors.append(f"{case['id']}: disk {index} does not exist: {path}")
            return None
        disks.append(path)
    return disks


def run_probe(
    probe: Path,
    case: dict[str, Any],
    out_dir: Path,
    timeout_seconds: float,
    errors: list[str],
) -> dict[str, Any]:
    kickstart = resolve_asset_path(case, "kickstart", errors)
    disks = case_disk_paths(case, errors)
    if kickstart is None or disks is None:
        return {"status": "blocked"}

    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(probe),
        "--kickstart",
        str(kickstart),
        "--out-dir",
        str(out_dir),
        "--system",
        str(case.get("system", "amiga500")),
        "--region",
        str(case.get("region", "pal")),
    ]
    if case.get("cycles"):
        cmd.extend(["--cycles", str(case["cycles"])])
    elif case.get("frames"):
        cmd.extend(["--frames", str(case["frames"])])
    else:
        errors.append(f"{case['id']}: no cycles or frames configured")
        return {"status": "blocked"}
    if case.get("fast_ram_size"):
        cmd.extend(["--fast-ram", str(case["fast_ram_size"])])
    for disk in disks:
        cmd.extend(["--disk", str(disk)])

    started = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=timeout_seconds,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        return {
            "status": "timeout",
            "elapsed_seconds": time.perf_counter() - started,
            "stdout": exc.stdout[-4000:] if isinstance(exc.stdout, str) else "",
            "stderr": exc.stderr[-4000:] if isinstance(exc.stderr, str) else "",
        }

    hashes: dict[str, str | None] = {}
    missing: list[str] = []
    for key, name in ARTIFACT_NAMES.items():
        path = out_dir / name
        if path.is_file():
            hashes[key] = sha256_file(path)
        else:
            hashes[key] = None
            missing.append(name)

    metadata: dict[str, Any] = {}
    metadata_path = out_dir / ARTIFACT_NAMES["metadata"]
    if metadata_path.is_file():
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            metadata = {"parse_error": True}

    return {
        "status": "ok" if proc.returncode == 0 and not missing else "failed",
        "returncode": proc.returncode,
        "elapsed_seconds": time.perf_counter() - started,
        "stdout_tail": proc.stdout[-4000:],
        "stderr_tail": proc.stderr[-4000:],
        "missing_artifacts": missing,
        "hashes": hashes,
        "metadata": metadata,
        "artifact_dir": str(out_dir.relative_to(REPO_ROOT)),
    }


def expected_hashes(case: dict[str, Any]) -> dict[str, str | None]:
    golden = case.get("golden", {})
    if not isinstance(golden, dict):
        return {"trace": None, "framebuffer": None, "audio": None}
    return {
        "trace": golden.get("trace_sha256"),
        "framebuffer": golden.get("framebuffer_sha256"),
        "audio": golden.get("audio_sha256"),
    }


def score_case(case: dict[str, Any], run: dict[str, Any], deterministic: bool) -> dict[str, Any]:
    expected = expected_hashes(case)
    available = all(expected[name] for name in ("trace", "framebuffer", "audio"))
    hashes = run.get("hashes", {})
    artifact_pass = {
        name: bool(expected[name]) and hashes.get(name) == expected[name]
        for name in ("trace", "framebuffer", "audio")
    }
    compatibility_pass = run.get("status") == "ok" and deterministic
    frame_audio = (float(artifact_pass["framebuffer"]) + float(artifact_pass["audio"])) / 2.0
    acs = 100.0 * (
        0.55 * float(artifact_pass["trace"])
        + 0.35 * frame_audio
        + 0.10 * float(compatibility_pass)
    )
    if not available:
        return {
            "scored": False,
            "acs": None,
            "trace_pass": None,
            "framebuffer_pass": None,
            "audio_pass": None,
            "compatibility_pass": compatibility_pass,
            "missing_golden_hashes": [
                f"{name}_sha256" for name, value in expected.items() if not value
            ],
        }
    return {
        "scored": True,
        "acs": acs,
        "trace_pass": artifact_pass["trace"],
        "framebuffer_pass": artifact_pass["framebuffer"],
        "audio_pass": artifact_pass["audio"],
        "compatibility_pass": compatibility_pass,
    }


def ptr_for_case(case: dict[str, Any], run: dict[str, Any], manifest: dict[str, Any]) -> float | None:
    metadata = run.get("metadata", {})
    elapsed = metadata.get("elapsed_seconds") or run.get("elapsed_seconds")
    if not isinstance(elapsed, (int, float)) or elapsed <= 0:
        return None
    master_hz = float(case.get("master_hz", manifest.get("default_master_hz", 7093790.0)))
    if case.get("cycles"):
        emulated_seconds = float(case["cycles"]) / master_hz
    elif case.get("frames"):
        fps_x1000 = metadata.get("video_fps_x1000")
        fps = float(fps_x1000) / 1000.0 if isinstance(fps_x1000, (int, float)) else 50.0
        emulated_seconds = float(case["frames"]) / fps
    else:
        return None
    return emulated_seconds / float(elapsed)


def compare_hash_dicts(a: dict[str, Any], b: dict[str, Any]) -> bool:
    return all(
        a.get(name) and a.get(name) == b.get(name)
        for name in ("trace", "framebuffer", "audio")
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--split", required=True, choices=["dev", "test"])
    parser.add_argument("--tier", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--cases", default=str(DEFAULT_CASES))
    parser.add_argument("--probe")
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--timeout-seconds", type=float, default=600.0)
    args = parser.parse_args(argv)

    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = REPO_ROOT / report_path
    report_path.parent.mkdir(parents=True, exist_ok=True)
    artifact_root = report_path.parent / "artifacts" / f"{args.split}_{args.tier}"
    superiority_log = report_path.parent / "artifacts" / "superiority_wins.jsonl"
    superiority_log.parent.mkdir(parents=True, exist_ok=True)
    superiority_log.touch(exist_ok=True)

    blocking: list[str] = []
    try:
        case_path = Path(args.cases)
        if not case_path.is_absolute():
            case_path = REPO_ROOT / case_path
        manifest = load_json_subset_yaml(case_path)
    except (OSError, HarnessError) as exc:
        manifest = {}
        blocking.append(str(exc))

    probe = find_probe(args.probe)
    if probe is None:
        blocking.append(
            "mnemos_amiga_eval_probe was not found; build the target or set MNEMOS_AMIGA_EVAL_PROBE"
        )

    cases = [
        c
        for c in manifest.get("cases", [])
        if isinstance(c, dict)
        and c.get("split") == args.split
        and c.get("tier") == args.tier
    ]
    if not cases:
        blocking.append(f"no cases configured for split={args.split} tier={args.tier}")

    case_reports: list[dict[str, Any]] = []
    scored_values: list[float] = []
    ptr_values: list[float] = []
    regression_clean = True
    deterministic_all = True

    if not blocking and probe is not None:
        for case in cases:
            case_errors: list[str] = []
            repeat_runs: list[dict[str, Any]] = []
            repeat_count = max(1, args.repeat)
            for index in range(repeat_count):
                repeat_dir = artifact_root / str(case["id"]) / f"run{index + 1}"
                repeat_runs.append(run_probe(probe, case, repeat_dir, args.timeout_seconds, case_errors))

            first = repeat_runs[0]
            runs_executed = all(run.get("status") != "blocked" for run in repeat_runs)
            deterministic: bool | None = True if runs_executed else None
            if len(repeat_runs) > 1 and runs_executed:
                first_hashes = first.get("hashes", {})
                deterministic = all(
                    compare_hash_dicts(first_hashes, run.get("hashes", {}))
                    for run in repeat_runs[1:]
                )
            if deterministic is False:
                deterministic_all = False

            score = score_case(case, first, bool(deterministic))
            if score.get("scored") and isinstance(score.get("acs"), (int, float)):
                scored_values.append(float(score["acs"]))
            elif score.get("missing_golden_hashes"):
                blocking.append(
                    f"{case['id']}: missing Tier-A golden hashes: "
                    + ", ".join(score["missing_golden_hashes"])
                )

            ptr = ptr_for_case(case, first, manifest)
            if ptr is not None:
                ptr_values.append(ptr)

            guard = bool(case.get("regression_guard", True))
            case_passed = bool(score.get("scored")) and bool(score.get("trace_pass")) and bool(
                score.get("framebuffer_pass")
            ) and bool(score.get("audio_pass")) and bool(score.get("compatibility_pass"))
            if guard and score.get("scored") and not case_passed:
                regression_clean = False

            case_reports.append(
                {
                    "id": case["id"],
                    "split": case.get("split"),
                    "tier": case.get("tier"),
                    "system": case.get("system"),
                    "region": case.get("region"),
                    "cycles": case.get("cycles"),
                    "frames": case.get("frames"),
                    "deterministic": deterministic,
                    "ptr": ptr,
                    "score": score,
                    "runs": repeat_runs,
                    "blocking_reasons": sorted(set(case_errors)),
                }
            )
            blocking.extend(sorted(set(case_errors)))

    acs = sum(scored_values) / len(scored_values) if scored_values else None
    ptr = min(ptr_values) if ptr_values else None
    ptr_floor = float(manifest.get("default_ptr_floor", 1.0)) if manifest else 1.0
    ptr_passed = ptr is not None and ptr >= ptr_floor

    if blocking:
        status = "blocked"
    elif acs is None:
        status = "blocked"
        blocking.append("no cases were scored")
    elif not deterministic_all or not ptr_passed or not regression_clean:
        status = "failed"
    else:
        status = "ok"

    report = {
        "schema": "mnemos.amiga.eval_report/1",
        "status": status,
        "split": args.split,
        "tier": args.tier,
        "probe": str(probe) if probe else None,
        "metric": {"name": "ACS", "goal": "maximize", "value": acs},
        "constraints": [
            {"name": "PTR", "goal": "maximize", "floor": ptr_floor, "value": ptr, "passed": ptr_passed}
        ],
        "deterministic": deterministic_all
        if any(case.get("deterministic") is not None for case in case_reports)
        else None,
        "regression_veto": {
            "rule": "no previously-passing conformance test may transition to failing",
            "clean": regression_clean,
        },
        "scores": {
            "ACS": acs,
            "PTR": ptr,
            "scored_cases": len(scored_values),
            "total_cases": len(case_reports),
        },
        "cases": case_reports,
        "blocking_reasons": sorted(set(blocking)),
        "required_outputs": {
            "report_path": str(report_path.relative_to(REPO_ROOT)),
            "superiority_wins": str(superiority_log.relative_to(REPO_ROOT)),
        },
    }
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if status == "ok":
        print(f"ACS={acs:.6f} PTR={ptr:.6f} report={report_path}")
        return 0
    if status == "blocked":
        print(f"blocked: report={report_path}", file=sys.stderr)
        return 2
    print(f"failed: report={report_path}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
