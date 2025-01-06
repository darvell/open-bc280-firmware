# POWER_CONTROL_SPEC — governor-based power policy (motor-safe + range-friendly)

This document defines a **fixed-function** (“no scripting”) control policy that:
- protects motors from **low-duty lugging** (the classic overheat scenario),
- avoids brownouts via **sag-aware limiting**,
- smoothly derates using real temperature if present, or a conservative **I²t thermal proxy** if not,
- still lets riders “manually drive” the system with predictable behavior.

It is written to fit BC280-class constraints:
- **96 KiB default SRAM** target (extendable to 224 KiB), no malloc required,
- **no floating point required** (fixed-point throughout),
- **Renode-testable** (every limiter produces a reason code + observable state).

This is a *policy* layer; it does not assume a particular motor controller protocol.
It only assumes we can compute/update a small set of telemetry signals.

---

## 0) High-level model: multiple governors, take the minimum

Every control tick (e.g., 50–200 Hz):

1. Ingest current state (speed, volts/amps if available, temps if available, cadence, etc.)
2. Compute derived signals (duty estimate, phase-current proxy, filters)
3. Compute independent limiters (“governors”):
   - `LIMIT_USER`    — what the rider asked for (profiles/gears/assist)
   - `LIMIT_LUGGING` — low duty protection (phase-current proxy)
   - `LIMIT_THERMAL` — real temp or I²t proxy
   - `LIMIT_SAG`     — keep voltage above a floor
   - (optional) `LIMIT_CONTROLLER` — protocol/controller constraints
4. Select final allowed output:
   - `P_allow = min(P_user, P_lug, P_thermal, P_sag, …)`
5. Produce a command (`P_cmd` or `I_cmd`) based on mode (manual, eco, sport, cruise).

Critical UI/diagnostics requirement:
> The firmware must always be able to answer: **“which limiter is active and why?”**

So we define:
- `limit_reason` enum (one byte),
- `P_allow_*` per-governor (debug-visible),
- a small “headroom” gauge per governor (0–100%).

---

## 1) Capability matrix (how much data we might have)

We may not know what the controller exposes yet. Design must degrade gracefully.

### Always assumed available
- Wheel speed `v` (from speed sensor or controller).
- Timebase `dt` / `ms`.

### Often available (best effort)
- Battery voltage `V_batt` (deci-volts).
- Battery current `I_batt` (deci-amps) — if missing, some features degrade.
- Cadence `cad` (rpm) — optional.
- Motor/controller temp `T_motor`, `T_ctrl` — optional.

### Optional “phone-assisted” (BLE app)
- Fused grade estimate (baro/GPS), already filtered.
- Ambient temperature estimate.

The policy layer must run with these profiles:

**Profile A (minimal):** `v` only  
→ only basic user limits; no lugging/thermal/sag.

**Profile B:** `v + V_batt`  
→ duty estimate + partial lugging protection.

**Profile C:** `v + V_batt + I_batt`  
→ full lugging + sag-aware current limit + thermal proxy.

**Profile D:** add temps  
→ replace thermal proxy with real-temp thermostat/PI.

---

## 2) The “lugging” protection (duty-based phase current proxy)

### Why
On many PWM BLDC controllers, at low duty cycle `D`:
- phase current can greatly exceed battery current,
- copper heating scales as `I_phase²`,
- slow steep climbs can cook motors even when battery current looks fine.

### Duty estimate
We need an estimate of PWM duty `D` without direct controller access.

Approximation:
- `D ≈ v / v_nl(V_batt)`

Where `v_nl(V_batt)` is “no-load” wheel speed at the current battery voltage.
In practice, it can be approximated by:
- a calibrated top speed at nominal voltage, scaled by voltage ratio.

We keep it simple and calibratable:
- `v_nl = k_v * V_batt` (units chosen so this is fixed-point cheap)
- clamp `v_nl` to avoid divide noise.

Then:
- `D = clamp( v / max(v_nl, v_nl_min), D_min, 1.0 )`

Recommended defaults:
- `D_min = 0.10` (avoid infinite estimates)
- `D_lug_start = 0.45` (start derating)
- `D_lug_hard = 0.30` (heavy derate)

### Phase-current proxy (when I_batt available)
- `I_phase_est ≈ I_batt / max(D, D_min)`

We don’t pretend this is exact; it’s a conservative heuristic.
We then compute a lugging limiter factor `f_lug(D)`:

Example piecewise mapping:
- if `D >= D_lug_start`: `f_lug = 1.00`
- if `D <= D_lug_hard`: `f_lug = f_min` (e.g. 0.35)
- else: linear interpolate between 1.0 and `f_min`

Then:
- `P_lug = P_user * f_lug`
or (if controlling current):
- `I_lug = I_user * f_lug`

### Time-domain behavior (avoid jerk)
Lugging should feel like “the bike is protecting itself”, not glitching:
- Derate ramps in over ~1–2 seconds when entering lugging zone.
- Recovery ramps out faster (e.g. 0.5–1s) once duty improves.

Implementation: asymmetric rate limiter on `f_lug`.

### UI requirement
When lugging is active:
- show a **LIMIT chip**: `LIMIT: LUG`
- show duty gauge (0–100%) and optionally “optimal climb speed band”.

---

## 3) Thermal limiting: real temp if available, otherwise I²t proxy

### A) With real temperature
Do not use “hard cutoff”. Use smooth derating.

Simple thermostat with hysteresis works, but a PI “soft thermostat” feels better:
- target `T_target` (below the hard redline)
- compute `P_thermal` such that the system settles near `T_target`

Keep it conservative and bounded.

### B) Without temperature: I²t model
We implement a first-order thermal proxy with two timescales:
- **fast** state (controller-ish): seconds
- **slow** state (motor-ish): minutes

We estimate heating input:
- `heat ≈ k_cu * I_phase_est²`
- optional iron term: `k_fe * rpm²` (often small during climbs; can be omitted)

Update each state (discrete first-order filter):
```
state += (heat - state) * dt / tau
```
All fixed-point.

Then map state→allowed power factor:
- `f_thermal = map(state)` (1.0 when cool, taper down, hard cap near “redline”)
- `P_thermal = P_user * f_thermal`

### Burst budget (“boost seconds remaining”)
We can expose a “burst” allowance above continuous using the same I²t proxy:
- allow `P_burst` when `state < burst_threshold`
- subtract budget proportionally to `heat`
- show remaining time

This is purely policy; it does not require scripting.

### UI requirement
When thermal limiting active:
- `LIMIT: THERM`
- show “headroom” bar (continuous) and optionally “burst seconds”.

---

## 4) Sag-aware limiting (range + brownout avoidance)

Range killers are current spikes (I²R) and sag-induced shutdowns.

If we have `V_batt` and `I_batt`, estimate internal resistance:
- On step loads: `R_batt_est ≈ ΔV / ΔI`
- Low-pass filter over time.

Then predict loaded voltage:
- `V_load_pred = V_oc_est - I_batt * R_batt_est`
or (simpler) use measured `V_batt` as loaded voltage and just enforce a floor.

Define voltage floor:
- `V_min = V_cutoff + margin` (configurable)

Compute sag limit:
- choose `I_sag_max` so `V_load_pred >= V_min`
- convert to `P_sag` or `I_sag` depending on control interface.

UI:
- `LIMIT: SAG`
- sag meter (margin to cutoff).

---

## 5) Grade / load estimation (optional, but useful)

Grade is useful for pacing and pre-derate, but **lugging protection works even without it**.

### Option A: phone-assisted grade (best)
BLE app provides `grade_fused` (already filtered), plus confidence.

### Option B: sensorless “effective grade” (works with limited data)
When `v` and `P_batt` are available:
- estimate wheel power `P_wheel ≈ η * P_batt`
- subtract aero/rolling/accel estimates
- solve for effective grade

Implementation guardrails:
- only compute when `v` > threshold (division stability)
- clamp and heavily low-pass (2–5s time constant)
- treat as “trend” signal, not truth.

Use grade mainly for:
- pre-derate if grade trending up AND duty trending down,
- pacing suggestions.

---

## 6) Manual control modes (no scripting, maximum “driver” feel)

We explicitly support “manual drive” without a VM by offering **fixed modes**:

### Mode: Manual current limit (simple, universal)
- User selects `I_user_max` via gear/level.
- Firmware outputs current limit directly (or maps to controller command).
- Governors can still reduce it (lugging/thermal/sag).

### Mode: Manual battery power (constant power)
- User selects `P_target` (watts).
- Firmware tries to hold battery power near target using a simple controller:
  - if `P_batt < P_target`, increase output
  - if `P_batt > P_target`, reduce output
- Add a droop curve for safety/feel.

This is range-friendly and avoids big current spikes.

### Mode: Sport (bursty)
- Uses burst budget (I²t) to allow temporary higher limits.
- UI shows remaining boost seconds.

### Implementation note
Even in manual modes, we always enforce:
- brake override,
- bootloader entry,
- config bounds.

---

## 7) What to expose on-screen so riders trust it

Minimum “trust UI” set:
- **LIMIT chip**: `USER/LUG/THERM/SAG`
- **Duty gauge** (utilization)
- **Thermal headroom** (continuous + burst)
- **Sag margin** (voltage floor margin)
- Optional grade estimate with confidence

The most important UX rule:
> Never silently reduce power. Always show the reason.

---

## 8) Fixed-point implementation notes

Avoid floats. Recommended units:
- Speed: deci-mph or deci-km/h (already used in open-firmware)
- Voltage: deci-volts
- Current: deci-amps
- Power: watts (integer)
- Duty: Q0.16 (0..65535)

Use simple saturating math and bounded filters.

---

## 9) Renode testing strategy (required)

For each governor, tests must:
1) set inputs (V/I/speed/etc.) via debug protocol injection,
2) run for a bounded time,
3) read back debug state:
   - `P_allow_user`, `P_allow_lug`, `P_allow_thermal`, `P_allow_sag`
   - `P_allow_final`
   - `limit_reason`
   - `duty_q16`, `I_phase_est`, `thermal_state`, `sag_margin`
4) assert expected limiter selection and monotonic behavior.

Pixel-perfect testing is not required; use render-hash/trace for UI indicators.
