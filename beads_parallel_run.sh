#!/usr/bin/env bash
set -euo pipefail

PARALLEL="${1:-4}"
LABEL="${PARALLEL_LABEL:-parallel:ui-sprint-2025-12}"
MODEL="${PARALLEL_MODEL:-gpt-5.2-codex}"
REASONING="${PARALLEL_REASONING_EFFORT:-medium}"
SLEEP_SECS="${PARALLEL_SLEEP_SECS:-10}"
FINAL_AGENT="${PARALLEL_FINAL_AGENT:-1}"

if [[ "$PARALLEL" -lt 1 ]]; then
  echo "Parallelism must be >= 1"
  exit 1
fi

function close_epics_if_done() {
  local epics_json epic_count
  epics_json="$(bd list --status=open --type epic --label "$LABEL" --json)"
  epic_count="$(printf '%s' "$epics_json" | jq 'length')"
  if [[ "$epic_count" -eq 0 ]]; then
    return 0
  fi

  printf '%s' "$epics_json" | jq -r '.[].id' | while read -r epic_id; do
    [[ -z "$epic_id" ]] && continue
    local show_json open_children
    show_json="$(bd show "$epic_id" --json)"
    open_children="$(printf '%s' "$show_json" | jq '[.[0].dependents[]? | select(.dependency_type=="parent-child" and .status!="closed")] | length')"
    if [[ "$open_children" -eq 0 ]]; then
      bd close "$epic_id" --reason "Completed (all parallel child issues closed)" >/dev/null
      echo "Closed epic: $epic_id"
    fi
  done
}

function dispatch_agent() {
  local issue_id="$1"
  scripts/agent_work.sh init "$issue_id" >/dev/null

  PROMPT=$'You are running inside the open-bc280-firmware repository as an automated coding agent.\n\nYour job is to fully complete exactly ONE bd issue per run: '"$issue_id"$'. If the issue is already closed, blocked, or assigned to someone else, exit without making changes.\n\nIMPORTANT: Do not edit files in the repo directly. Use the agent work folder workflow to avoid conflicts with other agents.\n\nAgent work folder workflow:\n- Initialize: `scripts/agent_work.sh init '"$issue_id"$'`\n- Before editing a file, copy it into your work folder:\n  `scripts/agent_work.sh add '"$issue_id"$' <path>`\n- Edit files under `.agent_work/'"$issue_id"$'/work/...` only.\n- If you need a private build/test sandbox:\n  `scripts/agent_work.sh sandbox '"$issue_id"$'` (returns a sandbox path with your changes overlayed).\n- When done, generate a patch for the final merge agent:\n  `scripts/agent_work.sh diff '"$issue_id"$'`\n\nFollow this process strictly:\n\n1) Open the issue\n   - Run `bd show '"$issue_id"$' --json` and read description/design/acceptance/notes.\n\n2) Claim it in bd\n   - Immediately mark it in progress and assign it to yourself:\n     - `bd update '"$issue_id"$' --status in_progress --assignee \"$USER\" --notes \"Automated agent started work on this issue.\" --json`\n\n3) Understand context and constraints\n   - Read `AGENTS.md`, `open-firmware/README.md`, and any files referenced in the issue.\n   - Repository rules: preserve OEM binaries; do NOT modify/replace the OEM bootloader; open-firmware must stay self-contained; keep the bootloader jump/validation path intact.\n\n4) Plan and implement the change (work folder only)\n   - Keep changes tightly scoped to the issue; avoid unrelated fixes.\n   - Use repo tooling (`rg`, `apply_patch`, scripts) where possible.\n   - Always copy files to the work folder before editing.\n\n5) Validate with appropriate feedback loops\n   - Prefer Renode emulation and the smallest targeted builds/tests relevant to the change.\n   - If testing is required, run it in your sandbox returned by `scripts/agent_work.sh sandbox '"$issue_id"$'`.\n\n6) Record what you did (leave a note)\n   - Generate patch: `scripts/agent_work.sh diff '"$issue_id"$'`\n   - Add a bd note with files changed + tests run + patch path (`.agent_work/'"$issue_id"$'/changes.patch`).\n\n7) Close the issue\n   - `bd close '"$issue_id"$' --reason \"Completed\" --json`\n   - Do NOT commit; final merge agent will apply patches.\n\n8) Exit after one issue\n   - Do not pick up a second issue within this run.\n'

  codex exec -m "$MODEL" --config model_reasoning_effort="$REASONING" -s danger-full-access "$PROMPT" &
}

while true; do
  open_json="$(bd list --status=open --label "$LABEL" --json)"
  open_count="$(printf '%s' "$open_json" | jq '[.[] | select(.issue_type!="epic")] | length')"
  if [[ "$open_count" -eq 0 ]]; then
    close_epics_if_done
    echo "No open parallel issues remaining for label: $LABEL"
    break
  fi

  ready_json="$(bd ready --json --label "$LABEL" --unassigned)"
  mapfile -t issue_ids < <(printf '%s' "$ready_json" | jq -r '.[] | select(.issue_type!="epic") | [.priority, .id] | @tsv' | sort -k1,1n -k2,2 | head -n "$PARALLEL" | awk '{print $2}')

  if [[ "${#issue_ids[@]}" -eq 0 ]]; then
    echo "No unassigned ready issues; waiting (${SLEEP_SECS}s)..."
    sleep "$SLEEP_SECS"
    continue
  fi

  echo "Dispatching ${#issue_ids[@]} agent(s)..."
  pids=()
  for issue_id in "${issue_ids[@]}"; do
    dispatch_agent "$issue_id"
    pids+=("$!")
  done

  for pid in "${pids[@]}"; do
    wait "$pid"
  done
done

if [[ "$FINAL_AGENT" -eq 1 ]]; then
  scripts/agent_work.sh collect >/dev/null
  FINAL_PROMPT=$'You are the final merge agent. All parallel issues are closed. Your job:\n\n1) Load `.agent_work/combined.patch` and inspect for conflicts/overlaps.\n2) Apply changes to the repo carefully (manual edits if needed).\n3) Review for correctness, coherence, and conflicts across agents; improve anything that needs improving.\n4) Run the minimal relevant tests (Renode/host sim only if required by acceptance criteria).\n5) Update bd notes on any issues you touched during merge.\n6) Do NOT commit. Stop after producing a clean merged working tree.\n'
  codex exec -m "$MODEL" --config model_reasoning_effort="$REASONING" -s danger-full-access "$FINAL_PROMPT"
fi
