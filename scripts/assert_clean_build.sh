#!/usr/bin/env sh
set -eu

preset="${MNEMOS_ARBOR_BUILD_PRESET:-windows-msvc-debug}"
target="${MNEMOS_ARBOR_BUILD_TARGET:-mnemos_amiga_eval_probe}"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is required for the Arbor pre-experiment build gate" >&2
  exit 1
fi

if [ ! -d "build/${preset}" ]; then
  cmake --preset "${preset}"
fi

cmake --build --preset "${preset}" --target "${target}"
