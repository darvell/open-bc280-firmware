# AGENTS.md — open-bc280-firmware workspace

## Project
Open-source, hacker-friendly firmware for BC280-class e-bike displays controlling Shengyi DWG22 (custom variant) motor controllers. Goal: flexible, inspectable stack that runs on Aventon bikes (primary target) but is adaptable to other BC280-family models. We keep OEM images alongside our own builds for reference, diffing, and regression testing. Open firmware must stay self-contained (no static linking against OEM blobs) and must **never replace/take over the OEM bootloader**; it should coexist and rely on the bootloader’s jump/validation path.

## Layout
- Firmware source tree lives at repo root:
  - `src/` + `platform/` + `drivers/` + `startup/` + `storage/` + `ui/` + `gfx/` + `util/`
  - `meson.build` + linker scripts (`startup/*.ld`, `link.ld`)
- `firmware/` — sample binaries: at least two combined OEM images and several standalone app/boot/our builds for analysis and flashing.
- `scripts/` — build + OTA helpers (BLE updater, etc.).
- `docs/firmware/SAFETY.md` + `docs/firmware/REQUIREMENTS.md` — safety invariants and v1.0 requirements.

## Expectations for agents
- Default to preserving OEM artifacts; never delete binaries unless explicitly told.
- When adding tooling or configs, keep them self-contained under this workspace; do not reach back into other repos without noting it.
- Prefer host unit tests and host-side simulators first; use Renode only when a peripheral behavior must be exercised.
- Keep instructions succinct and automatable (one-liners or scripts). Document new commands in README/AGENTS when they affect workflow.
- Open-firmware must not be linked against OEM images; treat OEM binaries only as references/tests.
- Do not modify or replace the OEM bootloader. All boot behavior should continue to pass through vendor bootloader validate/jump. If runtime patches are needed, do them in RAM and preserve the bootloader entry.
- On restart behavior: when we want guaranteed bootloader entry, set the bootloader flag (0xAA at SPI flash 0x0030_0000 + 0x3FF080) and jump; open-firmware should keep that path available and never block it.
- Keep build flows single-path: no feature flags or legacy asset toggles; update docs if you add new build steps.

## Host-first testing & simulation
- Run `meson setup build-host` then `ninja -C build-host test` as the default fast check for logic changes.
- Use `ninja -C build-host host_sim` then `./build-host/tests/host/host_sim` to exercise the full fake-bike loop with UART/BLE/sensor shims.
- Keep a host-side peripheral shim/simulator (UART/BLE/sensors) that is **not** compiled into the embedded build; it should live under `tests/host/` or `scripts/`.
- Use the shim to drive full-bike simulations (fake motor/sensors/ble UART) and compare deterministic traces/hashes.
- Renode remains the integration backstop for verifying MMIO-level behavior.

## Targets & assumptions
- Hardware: BC280 display + Shengyi DWG22 (custom variant) controller; Aventon bike variants prioritized.
- Boot path: vendor bootloader at 0x0800_0000, app at 0x0801_0000; bootloader flag in SPI flash window 0x0030_0000 + 0x3FF080.
- MCU: AT32F403AVCT7 (Cortex-M4F), 256KB internal flash (bootloader uses first 64KB), default SRAM 96KB (extendable to 224KB via config).
- BLE/UART protocol: 0x55-framed commands; see `docs/firmware/README.md` for supported debug ops.

## Build (Meson/Ninja)
- Firmware (cross): `meson setup build --cross-file cross/arm-none-eabi-clang.txt` then `ninja -C build`.
- Host: `meson setup build-host` then `ninja -C build-host test`.
- No optional build flags: single-path build only (legacy UI assets, Renode trace hooks, BLE compat/MITM, fault injection, release signing removed).

## Renode quickstart (macOS)
- Preferred: use the bundled app at `renode/Renode.app` and run `./scripts/renode_smoke.sh`.
- If Renode isn’t in `/Applications`, set `RENODE_APP` to the bundle path:
  - `RENODE_APP="$PWD/renode/Renode.app" ./scripts/renode_smoke.sh`
- If you have a native binary, set `RENODE_BIN` instead (or put `renode` on PATH).
- The smoke script will fall back to `Renode.exe` via `mono64` when no native binary exists.
- Tests expect UART output under `BC280_RENODE_OUTDIR` (defaults to a temp dir).

## IDA quick-reference for this workspace
- Active DB: `BC280_Combined_Firmware_3.3.6_4.2.5.bin` (ARM Cortex-M4F, AT32F403A).
- Segments of interest:
  - `FLASH_BOOT` 0x0800_0000–0x0800_FFFF: OEM bootloader.
  - `FLASH_APP`  0x0801_0000–…      : OEM application.
  - SPI flash window mapped in Renode stubs: `storage` region and BOOTLOADER_MODE flag at 0x0030_0000+0x3FF080.
- Entry vectors: Reset vector at 0x0800_0004 -> `BL_Reset_Handler_0` -> `bootloader_main` -> validates app via `validate_firmware_header` / `jump_to_application_firmware`.
- App boot image layout: app vector table at 0x0801_0000 (SP, Reset). Use `mcporter ida.disassemble selector=0x8010000-0x8010100` to inspect.
- Key functions:
  - `bootloader_main_init` sets UI + tasks, reads bootloader flag.
  - `validate_firmware_header` / `jump_to_application_firmware` enforce SP&0x2FFE0000==0x20000000 and PC&0xFFF80000==0x08000000.
  - `BOOT_BLE_CommandProcessor` (0x8000394) handles OTA in bootloader.
  - `APP_BLE_UART_CommandProcessor` (app-side BLE command handler).
- IDA MCP workflows:
  - `mcporter ida.list_instances`, `ida.info`, `ida.get_segments` for orientation.
  - `ida.search_strings pattern="BLE|boot|update"` to find command handlers.
  - `ida.get_xrefs` on APP_SP_main (0x8010000) for jump points; on BOOTLOADER_MODE flag for mode logic.
- Navigating app/boot separation: treat 0x0800_0000–0x0800_FFFF as boot; 0x0801_0000+ as app. When decompiling, keep `max_lines` small (60–120) and validate with `disassemble` around jumps/BLX.
- Don’ts in IDA: don’t bulk-rename OEM symbols to open-firmware names; keep OEM analysis neutral. Don’t create xrefs to our open-firmware binary; keep analyses separate.

### Using bv as an AI sidecar

bv is a graph-aware triage engine for Beads projects (.beads/beads.jsonl). Instead of parsing JSONL or hallucinating graph traversal, use robot flags for deterministic, dependency-aware outputs with precomputed metrics (PageRank, betweenness, critical path, cycles, HITS, eigenvector, k-core).

**Scope boundary:** bv handles *what to work on* (triage, priority, planning). For agent-to-agent coordination (messaging, work claiming, file reservations), use [MCP Agent Mail](https://github.com/Dicklesworthstone/mcp_agent_mail).

**⚠️ CRITICAL: Use ONLY `--robot-*` flags. Bare `bv` launches an interactive TUI that blocks your session.**

#### The Workflow: Start With Triage

**`bv --robot-triage` is your single entry point.** It returns everything you need in one call:
- `quick_ref`: at-a-glance counts + top 3 picks
- `recommendations`: ranked actionable items with scores, reasons, unblock info
- `quick_wins`: low-effort high-impact items
- `blockers_to_clear`: items that unblock the most downstream work
- `project_health`: status/type/priority distributions, graph metrics
- `commands`: copy-paste shell commands for next steps

bv --robot-triage        # THE MEGA-COMMAND: start here
bv --robot-next          # Minimal: just the single top pick + claim command

#### Other Commands

**Planning:**
| Command | Returns |
|---------|---------|
| `--robot-plan` | Parallel execution tracks with `unblocks` lists |
| `--robot-priority` | Priority misalignment detection with confidence |

**Graph Analysis:**
| Command | Returns |
|---------|---------|
| `--robot-insights` | Full metrics: PageRank, betweenness, HITS (hubs/authorities), eigenvector, critical path, cycles, k-core, articulation points, slack |
| `--robot-label-health` | Per-label health: `health_level` (healthy\|warning\|critical), `velocity_score`, `staleness`, `blocked_count` |
| `--robot-label-flow` | Cross-label dependency: `flow_matrix`, `dependencies`, `bottleneck_labels` |
| `--robot-label-attention [--attention-limit=N]` | Attention-ranked labels by: (pagerank × staleness × block_impact) / velocity |

**History & Change Tracking:**
| Command | Returns |
|---------|---------|
| `--robot-history` | Bead-to-commit correlations: `stats`, `histories` (per-bead events/commits/milestones), `commit_index` |
| `--robot-diff --diff-since <ref>` | Changes since ref: new/closed/modified issues, cycles introduced/resolved |

**Other Commands:**
| Command | Returns |
|---------|---------|
| `--robot-burndown <sprint>` | Sprint burndown, scope changes, at-risk items |
| `--robot-forecast <id\|all>` | ETA predictions with dependency-aware scheduling |
| `--robot-alerts` | Stale issues, blocking cascades, priority mismatches |
| `--robot-suggest` | Hygiene: duplicates, missing deps, label suggestions, cycle breaks |
| `--robot-graph [--graph-format=json\|dot\|mermaid]` | Dependency graph export |
| `--export-graph <file.html>` | Self-contained interactive HTML visualization |

#### Scoping & Filtering

bv --robot-plan --label backend              # Scope to label's subgraph
bv --robot-insights --as-of HEAD~30          # Historical point-in-time
bv --recipe actionable --robot-plan          # Pre-filter: ready to work (no blockers)
bv --recipe high-impact --robot-triage       # Pre-filter: top PageRank scores
bv --robot-triage --robot-triage-by-track    # Group by parallel work streams
bv --robot-triage --robot-triage-by-label    # Group by domain

#### Understanding Robot Output

**All robot JSON includes:**
- `data_hash` — Fingerprint of source beads.jsonl (verify consistency across calls)
- `status` — Per-metric state: `computed|approx|timeout|skipped` + elapsed ms
- `as_of` / `as_of_commit` — Present when using `--as-of`; contains ref and resolved SHA

**Two-phase analysis:**
- **Phase 1 (instant):** degree, topo sort, density — always available immediately
- **Phase 2 (async, 500ms timeout):** PageRank, betweenness, HITS, eigenvector, cycles — check `status` flags

**For large graphs (>500 nodes):** Some metrics may be approximated or skipped. Always check `status`.

#### jq Quick Reference

bv --robot-triage | jq '.quick_ref'                        # At-a-glance summary
bv --robot-triage | jq '.recommendations[0]'               # Top recommendation
bv --robot-plan | jq '.plan.summary.highest_impact'        # Best unblock target
bv --robot-insights | jq '.status'                         # Check metric readiness
bv --robot-insights | jq '.Cycles'                         # Circular deps (must fix!)
bv --robot-label-health | jq '.results.labels[] | select(.health_level == "critical")'

**Performance:** Phase 1 instant, Phase 2 async (500ms timeout). Prefer `--robot-plan` over `--robot-insights` when speed matters. Results cached by data hash.

Use bv instead of parsing beads.jsonl—it computes PageRank, critical paths, cycles, and parallel tracks deterministically.

<!-- bv-agent-instructions-v1 -->

---

## Beads Workflow Integration

This project uses [beads_viewer](https://github.com/Dicklesworthstone/beads_viewer) for issue tracking. Issues are stored in `.beads/` and tracked in git.

### Essential Commands

```bash
# View issues (launches TUI - avoid in automated sessions)
bv

# CLI commands for agents (use these instead)
bd ready              # Show issues ready to work (no blockers)
bd list --status=open # All open issues
bd show <id>          # Full issue details with dependencies
bd create --title="..." --type=task --priority=2
bd update <id> --status=in_progress
bd close <id> --reason="Completed"
bd close <id1> <id2>  # Close multiple issues at once
bd sync               # Commit and push changes
```

### Workflow Pattern

1. **Start**: Run `bd ready` to find actionable work
2. **Claim**: Use `bd update <id> --status=in_progress`
3. **Work**: Implement the task
4. **Complete**: Use `bd close <id>`
5. **Sync**: Always run `bd sync` at session end

### Key Concepts

- **Dependencies**: Issues can block other issues. `bd ready` shows only unblocked work.
- **Priority**: P0=critical, P1=high, P2=medium, P3=low, P4=backlog (use numbers, not words)
- **Types**: task, bug, feature, epic, question, docs
- **Blocking**: `bd dep add <issue> <depends-on>` to add dependencies

### Session Protocol

**Before ending any session, run this checklist:**

```bash
git status              # Check what changed
git add <files>         # Stage code changes
bd sync                 # Commit beads changes
git commit -m "..."     # Commit code
bd sync                 # Commit any new beads changes
git push                # Push to remote
```

### Best Practices

- Check `bd ready` at session start to find available work
- Update status as you work (in_progress → closed)
- Create new issues with `bd create` when you discover tasks
- Use descriptive titles and set appropriate priority/type
- Always `bd sync` before ending session

<!-- end-bv-agent-instructions -->
