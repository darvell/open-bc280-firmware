# AT32F403AVCT7 MCU Notes (BC280 target)

This repo targets **AT32F403AVCT7** (ArteryTek AT32F403A series):
- **Core**: ARM Cortex‑M4F (FPU), up to 240 MHz
- **Package**: LQFP100 ("V")
- **Flash**: **256 KB** internal ("C" code)
- **SRAM**: **96 KB default** (this project **does not use SRAM extension**)
- **Temp range**: −40 °C to +105 °C

## Memory map (family-wide)
- Internal Flash Bank1: `0x0800_0000 – 0x0807_FFFF` (device physically provides 256 KB)
- Internal Flash Bank2: `0x0808_0000 – 0x080F_FFFF` (not present on 256 KB device)
- SRAM window: `0x2000_0000 – 0x2003_7FFF` (224 KB window; default 96 KB enabled)
- Factory bootloader: `0x1FFF_B000 – 0x1FFF_F7FF`
- User system data: `0x1FFF_F800 – 0x1FFF_F82F`
- SPIM (external SPI flash) map: `0x0840_0000 – 0x093F_FFFF` (16 MB)

## Project-specific memory layout
- OEM bootloader at `0x0800_0000` (first 64 KB)
- Open firmware app at `0x0801_0000` (192 KB app region)
- Bootloader mode flag in **external SPI flash**: `0x003FF080` (`g_bootloader_mode_flag` in IDA, compared against `0xAA`)

## Flash/SRAM usage guidance
- **App flash budget**: 192 KB (256 KB − 64 KB bootloader)
- **Default SRAM budget**: 96 KB (do not assume extended SRAM)
- SRAM extension (to 224 KB) exists on this MCU family but is intentionally **not** enabled/assumed here

## References (vendor docs)
```text
Datasheet (AT32F403A Series, EN, v2.04 mirror):
https://datasheet4u.com/pdf-down/A/T/3/AT32F403A-ARTERY.pdf

Reference Manual (AT32F403A/407 series, v2.02):
https://www.arterytek.com/download/RM/RM_AT32F403Axx_407xx_v2.02.pdf

AN0026 Extending SRAM in User's Program (EN):
https://www.arterytek.com/download/APNOTE/AN0026_Extending_SRAM_in_User%27s_Program_EN_V2.0.0.pdf

AN0042 AT32 SPIM Application Note (EN):
https://www.arterytek.com/download/APNOTE/AN0042_AT32_SPIM_Application_Note_EN_V2.0.0.pdf
```
