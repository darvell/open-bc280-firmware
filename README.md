# open-bc280-firmware

> Safety / status: Experimental reverse-engineering + firmware project.
>
> Do **not** flash this onto a bike you ride, and do **not** rely on it for safety-critical behavior.

Attempted open-source firmware for the **BC280** e-bike display used on bikes with **Shengyi DWG22**
motor controllers (Aventon-focused, but intended to be adaptable to other BC280-class variants).

This project is not affiliated with Aventon, Shengyi, or any OEM vendor.

## What this is

The BC280 ecosystem typically ships as a vendor bootloader + application image. This repo contains:

- An open application firmware source tree (this repo) designed to be accepted by the **stock OEM bootloader**
  at the standard application base address.
- Host-side tests and a deterministic “fake bike” simulator (UART/BLE/sensor shims) to iterate quickly
  without touching hardware.
- OEM binary artifacts under `firmware/` kept for diffing/regression reference only.

The open firmware must remain self-contained and must **never** replace or take over the OEM bootloader.
All boot and validation behavior should continue to pass through the vendor bootloader’s jump/validate path.

## Hardware target

- Display: BC280-class e-bike display (various OEM bike models; Aventon is the primary reference)
- Motor controller: Shengyi **DWG22** (custom variant) using a display↔controller UART protocol
- MCU: **AT32F403AVCT7** (Cortex‑M4F)
  - Internal flash: 256KB total
  - Typical layout:
    - Bootloader: `0x0800_0000–0x0800_FFFF` (first 64KB)
    - Application: `0x0801_0000–0x0803_FFFF` (192KB)
  - SRAM: 96KB default (some configs extend usable SRAM; this project budgets for 96KB by default)

## How it works (high-level)

At a system level, the display firmware sits between two buses:

- **UART1** ↔ BLE module passthrough (used for debugging/telemetry and OTA control flows)
- **UART2** ↔ motor controller (Shengyi DWG22 display↔controller protocol)

The open firmware implements enough of the display-side application behavior to:

- Boot as an OEM-compatible application image (vector table + reset handler constraints)
- Speak the motor bus protocol well enough for parity testing and simulation
- Expose a debug surface for bring-up (UART “peek/poke”, logging, limited flash reads, etc.)

Detailed protocol notes live in `docs/firmware/README.md`.

## Boot / recovery model

This repo is intentionally conservative about boot behavior:

- The OEM bootloader remains the authority for validation and jumping to the application.
- When a guaranteed return to bootloader is required, the firmware uses the bootloader mode flag:
  - SPI flash mapped window: `0x0030_0000`
  - Bootloader mode flag: `0x0030_0000 + 0x3FF080`
  - Convention: write `0xAA` then reboot/jump via the OEM bootloader.

Safety invariants and boot constraints are documented in:

- `docs/firmware/SAFETY.md`
- `docs/firmware/REQUIREMENTS.md`

## Repository layout

- `firmware/` — OEM and reference binaries (for diffing/regression only; the open firmware does not link against them)
- `docs/chipset/` — MCU reference notes / datasheets
- `docs/firmware/` — safety/requirements/specs and UI screenshots
- `docs/re/` — reverse-engineering notes (reference-only)
- `src/`, `platform/`, `drivers/`, `startup/`, `storage/`, `ui/`, `gfx/`, `util/` — firmware source tree (Meson project root)

## Prerequisites

- Meson + Ninja (host builds and cross builds)
- For cross-compiling the firmware image: an ARM embedded toolchain as configured by
  `cross/arm-none-eabi-clang.txt`

## Build & test (recommended path)

This repo is set up to prefer fast host-side validation before any emulation or hardware work.

### Host tests (fast)

```bash
meson setup build-host || meson setup --reconfigure build-host
ninja -C build-host test
```

### Host simulator (fake bike loop)

```bash
meson setup build-host || meson setup --reconfigure build-host
ninja -C build-host host_sim
./build-host/tests/host/host_sim
```

The simulator can emit deterministic traces (including generated Shengyi frames) when configured via
environment variables; see `docs/firmware/README.md` for the full set of knobs.

### Cross build (firmware image)

```bash
./scripts/build_open_firmware.sh
# output: build/open_firmware.bin
```

### Image preflight (recommended before flashing)

The OEM bootloader enforces vector-table and range constraints. Run the preflight checker against
any image before attempting an update:

```bash
python3 scripts/preflight_open_firmware.py --image build/open_firmware.bin
```

## OTA update (BLE)

This repo includes a host-side BLE updater script intended to drive the OEM bootloader’s update path.

```bash
python3 scripts/ble_push_open_firmware.py <BLE_MAC_ADDRESS>
```

## Notes on OEM artifacts

The `firmware/` directory contains OEM binaries that were used for reverse engineering and regression
reference. The open firmware is not linked against OEM images or blobs; OEM binaries are treated as
reference inputs only.

## Contributing

If you want to contribute, start with the safety and boot constraints:

- `docs/firmware/SAFETY.md`
- `docs/firmware/REQUIREMENTS.md`

Then use the host tests/simulator as the default feedback loop. Avoid changes that would bypass,
replace, or otherwise undermine the OEM bootloader path.

## License

No explicit top-level license file is provided in this repo. Do not assume reuse rights until a license
is added.
