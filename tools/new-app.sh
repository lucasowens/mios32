#!/usr/bin/env bash
# Scaffold a new MIOS32 app from app_skeleton.
#
# Usage:
#   ./tools/new-app.sh my_app                # -> mios32/apps/quickies/my_app
#   ./tools/new-app.sh quickies/my_app       # explicit subdir
#   ./tools/new-app.sh examples/my_demo

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <name>           # creates mios32/apps/quickies/<name>" >&2
  echo "       $0 <subdir>/<name>  # creates mios32/apps/<subdir>/<name>" >&2
  exit 1
fi

repo_root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
skeleton="$repo_root/mios32/apps/templates/app_skeleton"

raw="$1"
if [[ "$raw" == */* ]]; then
  rel="$raw"
else
  rel="quickies/$raw"
fi

dest="$repo_root/mios32/apps/$rel"

if [[ -e "$dest" ]]; then
  echo "error: $dest already exists" >&2
  exit 1
fi

mkdir -p "$(dirname "$dest")"
cp -R "$skeleton" "$dest"

echo "scaffolded $dest"
echo "build with: make app APP=$rel"
