#!/bin/sh
# Pushes site/ (built by tools/release.sh) to the gh-pages branch, which GitHub
# Pages serves at https://djb-rh.github.io/ups-watcher/
set -e
cd "$(dirname "$0")/.."
[ -f site/manifest.json ] || { echo "run tools/release.sh first" >&2; exit 1; }
remote=$(git remote get-url origin)
tmp=$(mktemp -d)
cp -R site/. "$tmp"/
cd "$tmp"
git init -q -b gh-pages
git add -A
git -c commit.gpgsign=false commit -q -m "Web installer $(sed -n 's/.*"version": "\([^"]*\)".*/\1/p' manifest.json)"
git push -f "$remote" gh-pages:gh-pages
cd / && rm -rf "$tmp"
echo "published"
