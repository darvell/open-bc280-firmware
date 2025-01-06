# open-bc280-firmware

Working area for open-source BC280 display firmware (Shengyi DWG22 custom variant/Aventon focus).

- `open-firmware/` — our app source + build outputs; includes Renode scripts in `open-firmware/renode/`.
- `firmware/` — reference binaries: combined OEM images, OEM app/boot slices, our example builds (open_firmware.bin, hello_app bin, sample OEM app/boot pairs).
- `scripts/` — helper tooling (build + BLE OTA uploader from the parent project).
- Target MCU: **AT32F403AVCT7** (Cortex‑M4F, 256KB flash; 64KB bootloader + 192KB app region).

## Quick start (Codex-ready)
1) cd `/Users/pp/code/open-bc280-firmware`
2) Fast host tests (no Renode): `make -C open-firmware test-host`
3) Full host simulator (fake bike + UART/BLE): `make -C open-firmware sim-host`
4) Build: `./scripts/build_open_firmware.sh` (output in `open-firmware/build/open_firmware.bin`).
5) Run in Renode (integration check only):
   ```bash
   renode/Renode.app/Contents/MacOS/renode --disable-xwt \
     -e "include @open-firmware/renode/bc280_open_firmware.resc"
   ```
   The script loads the OEM combined image from `firmware/BC280_Combined_Firmware_3.3.6_4.2.5.bin`, overlays `open_firmware.bin`, clears BOOTLOADER_MODE, sets PC/SP, and opens a UART1 analyzer.
6) OTA over BLE from host: `python3 scripts/ble_push_open_firmware.py <MAC>` (writes open_firmware.bin, handles bootloader hop, blocks/finalizes).
7) Debug protocol (UART1/2/4 auto-sensed): see `open-firmware/README.md` for commands; streaming telemetry via 0x0D/0x81.

## Host simulation
We provide a host-side peripheral shim + fake bike simulator so most tests can
run without Renode. Track work in beads: `open-bc280-firmware-kbu`.
Bus protocol extraction from OEM firmware: `open-bc280-firmware-h1p`.

## Motor protocol snapshot
See the detailed Shengyi DWG22 config/status map in this README for 0xC0 fields (battery, speed limit, wheel size presets, assist profile, current/voltage thresholds, misc params). The OEM combined images are here for diffing/reference only—open-firmware never links against them.

For agent expectations and IDA notes, see `AGENTS.md`.
