CLEANUP WORK INSTRUCTIONS

This document describes ongoing work to eliminate duplicate code and consolidate type definitions in the BC280 firmware codebase. The goal is reducing main.c from its current size to approximately 2000 lines by extracting modules and removing redundant definitions.


PROBLEM STATEMENT

Multiple parallel agents worked on this codebase simultaneously. This created duplicate type definitions across files. The same struct might be defined in main.c, app.c, and a module header with slightly different field orders or names. This causes maintenance problems and potential bugs when definitions drift apart.


CURRENT STATE

main.c is approximately 5846 lines. Target is 2000 lines.
app.c is approximately 356 lines.
All 6 host tests pass. Run with: ninja -C build-host test


CANONICAL LOCATIONS FOR TYPES

These headers contain the authoritative definitions. All other files should include these headers rather than redefining types.

config_t and config_curve_pt_t: src/config/config.h
PROFILE_COUNT: src/config/config.h
ASSIST_CURVE_MAX_POINTS: src/config/config.h

trip_acc_t and trip_snapshot_t: src/telemetry/trip.h
trip_hist_t: src/telemetry/trip.h
HIST_GEAR_BINS and HIST_POWER_BINS: src/telemetry/trip.h

motor_state_t: src/motor/app_data.h
debug_inputs_t: src/motor/app_data.h
debug_outputs_t: src/motor/app_data.h

power_policy_state_t: src/power/power.h
adaptive_assist_state_t: src/power/power.h

cruise_state_t: src/control/control.h
regen_state_t: src/control/control.h
drive_state_t: src/control/control.h
boost_state_t: src/control/control.h
vgear_table_t: src/control/control.h
cadence_bias_t: src/control/control.h

volatile uint32_t g_ms: platform/time.h


HOW TO FIND DUPLICATES

Search for typedef struct patterns:
grep -n "typedef struct" src/main.c
grep -n "typedef struct" src/app.c

Search for specific type names across files:
grep -rn "typedef.*config_t" src/
grep -rn "typedef.*trip_acc_t" src/

Check for duplicate defines:
grep -rn "#define PROFILE_COUNT" src/
grep -rn "#define HIST_GEAR_BINS" src/


HOW TO FIX A DUPLICATE

Step 1: Identify the canonical location from the list above.

Step 2: In the file with the duplicate, add an include for the canonical header.

Step 3: Delete the duplicate typedef or define.

Step 4: If the duplicate had different field names or types, update all usages in that file to match the canonical definition.

Step 5: Run ninja -C build-host test to verify nothing broke.

Step 6: Commit the change with a message describing what was consolidated.


REMAINING WORK IN MAIN.C

These areas in main.c contain code that could be extracted to modules:

assist_profile_t, assist_curve_t, assist_curve_profile_t around line 393: These profile types and the g_profiles array could move to a profiles module.

wizard_state_t and wizard functions around line 889: Setup wizard could be its own module.

bus_ui_entry_t and bus capture code around line 850: Bus debugging UI could be extracted.

Graph and streaming code: Functions related to telemetry streaming and graphing.

Protocol handlers: The handle_* functions that process incoming commands.


REMAINING WORK IN APP.C

event_meta_t around line 121: This is a local typedef. Check if main.c has a matching definition that should be canonical.

bus_ui_entry_t around line 129: This typedef differs from main.c version. The main.c version has more fields including diff_mask. Either fix app.c to match or create a proper accessor API.


BUILD COMMANDS

Setup build directory: meson setup build-host
Build: ninja -C build-host
Run tests: ninja -C build-host test
Reconfigure after meson.build changes: meson setup build-host --reconfigure


GIT WORKFLOW

Check status: git status
Stage changes: git add path/to/file
Commit with message: git commit -m "refactor: description of consolidation"

Do not force push. Do not amend commits that have been pushed. Each consolidation should be a separate commit.


VALIDATION CHECKLIST

Before committing any change:
1. All 6 tests pass
2. No new compiler warnings
3. The duplicate is actually removed, not just commented out
4. The include for the canonical header is present
5. All usages in the file work with the canonical type definition


KNOWN ISSUES

The extern declarations for g_ms appear in many files. This is intentional for HOST_TEST portability since platform/time.h may not exist in host test builds. Do not consolidate these.

app.c declares extern for g_bus_ui_view but main.c declares it as static. This is a bug that only works because app.c is not compiled in host tests. Fixing this requires creating an accessor API.


FILES MODIFIED THIS SESSION

src/main.c: Removed config_blob_t duplicate, removed PROFILE_COUNT and ASSIST_CURVE_MAX_POINTS duplicates
src/app.c: Replaced trip_acc_t and trip_snapshot_t duplicates with includes, fixed HIST_GEAR_BINS
src/config/config.h: Added PROFILE_COUNT with ifndef guard
src/input/input.c: Removed PROFILE_COUNT duplicate
src/telemetry/trip.c: New file extracted from main.c
src/telemetry/trip.h: New file with trip types


NEXT STEPS

1. Look for more typedef struct blocks in main.c that duplicate types from headers
2. Extract profile management code to src/profiles/ module
3. Extract wizard code to src/config/wizard.c
4. Extract bus capture UI code to src/bus/bus_ui.c
5. Extract protocol handlers to src/comm/handlers.c
6. Continue until main.c is approximately 2000 lines
