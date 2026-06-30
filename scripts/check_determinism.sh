#!/usr/bin/env sh
set -eu

split="${MNEMOS_ARBOR_EVAL_SPLIT:-dev}"
tier="${MNEMOS_ARBOR_EVAL_TIER:-a500_ocs_ce}"
report="${MNEMOS_ARBOR_REPORT_PATH:-build/scratch/arbor/amiga/determinism_${split}_${tier}.json}"

python tools/conformance/run_eval.py \
  --split "${split}" \
  --tier "${tier}" \
  --report "${report}" \
  --repeat 2
