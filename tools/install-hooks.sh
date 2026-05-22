#!/usr/bin/env sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
source_hook="$repo_root/tools/git-hooks/pre-commit"
target_hook="$repo_root/.git/hooks/pre-commit"

if [ ! -f "$source_hook" ]; then
  echo "Missing hook source: $source_hook" >&2
  exit 1
fi

cp "$source_hook" "$target_hook"
chmod +x "$target_hook"
echo "Installed pre-commit hook to $target_hook"
