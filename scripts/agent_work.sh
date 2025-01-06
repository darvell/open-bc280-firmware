#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK_ROOT="$ROOT/.agent_work"

cmd="${1:-}"
shift || true

function usage() {
  cat <<'EOF'
Usage: scripts/agent_work.sh <command> [args]

Commands:
  init <issue-id>              Initialize work folder for issue
  add <issue-id> <path>        Copy file from repo into work folder (preserve path)
  list <issue-id>              List files in work folder
  diff <issue-id>              Generate changes.patch + changed_files.txt
  sandbox <issue-id>           Create sandbox repo with work overlay (for tests/builds)
  collect                      Combine all changes.patch into .agent_work/combined.patch
EOF
}

function ensure_issue() {
  local issue_id="$1"
  if [[ -z "$issue_id" ]]; then
    echo "Issue id required"
    exit 1
  fi
  mkdir -p "$WORK_ROOT/$issue_id/work" "$WORK_ROOT/$issue_id/meta"
}

case "$cmd" in
  init)
    issue_id="${1:-}"
    ensure_issue "$issue_id"
    printf 'issue=%s\ncreated=%s\n' "$issue_id" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$WORK_ROOT/$issue_id/meta/info.txt"
    echo "$WORK_ROOT/$issue_id/work"
    ;;
  add)
    issue_id="${1:-}"
    rel="${2:-}"
    ensure_issue "$issue_id"
    if [[ -z "$rel" ]]; then
      echo "Path required"
      exit 1
    fi
    src="$ROOT/$rel"
    if [[ ! -e "$src" ]]; then
      echo "Source path not found: $rel"
      exit 1
    fi
    dest="$WORK_ROOT/$issue_id/work/$rel"
    mkdir -p "$(dirname "$dest")"
    cp -a "$src" "$dest"
    echo "$dest"
    ;;
  list)
    issue_id="${1:-}"
    ensure_issue "$issue_id"
    find "$WORK_ROOT/$issue_id/work" -type f | sed "s|^$WORK_ROOT/$issue_id/work/||" | sort
    ;;
  diff)
    issue_id="${1:-}"
    ensure_issue "$issue_id"
    patch="$WORK_ROOT/$issue_id/changes.patch"
    manifest="$WORK_ROOT/$issue_id/changed_files.txt"
    : > "$patch"
    : > "$manifest"
    find "$WORK_ROOT/$issue_id/work" -type f | while read -r f; do
      rel="${f#"$WORK_ROOT/$issue_id/work/"}"
      echo "$rel" >> "$manifest"
      if [[ -f "$ROOT/$rel" ]]; then
        diff -u --label "a/$rel" --label "b/$rel" "$ROOT/$rel" "$f" >> "$patch" || true
      else
        diff -u --label "a/$rel" --label "b/$rel" /dev/null "$f" >> "$patch" || true
      fi
      echo >> "$patch"
    done
    echo "$patch"
    ;;
  sandbox)
    issue_id="${1:-}"
    ensure_issue "$issue_id"
    sandbox="$WORK_ROOT/$issue_id/sandbox"
    mkdir -p "$sandbox"
    rsync -a --delete \
      --exclude ".git" \
      --exclude ".agent_work" \
      --exclude "open-firmware/build" \
      --exclude "open-firmware/renode/output" \
      "$ROOT/" "$sandbox/"
    if [[ -d "$WORK_ROOT/$issue_id/work" ]]; then
      rsync -a "$WORK_ROOT/$issue_id/work/" "$sandbox/"
    fi
    echo "$sandbox"
    ;;
  collect)
    mkdir -p "$WORK_ROOT"
    out="$WORK_ROOT/combined.patch"
    manifest="$WORK_ROOT/combined_manifest.txt"
    : > "$out"
    : > "$manifest"
    for patch in "$WORK_ROOT"/*/changes.patch; do
      [[ -f "$patch" ]] || continue
      issue_id="$(basename "$(dirname "$patch")")"
      echo "## $issue_id" >> "$manifest"
      cat "$patch" >> "$out"
      echo >> "$out"
    done
    echo "$out"
    ;;
  *)
    usage
    exit 1
    ;;
esac
