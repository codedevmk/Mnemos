#!/usr/bin/env sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

for hook in pre-commit commit-msg; do
  source_hook="$repo_root/tools/git-hooks/$hook"
  target_hook="$repo_root/.git/hooks/$hook"

  if [ ! -f "$source_hook" ]; then
    echo "Missing hook source: $source_hook" >&2
    exit 1
  fi

  cp "$source_hook" "$target_hook"
  chmod +x "$target_hook"
  echo "Installed $hook hook to $target_hook"
done
