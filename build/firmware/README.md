<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Firmware Artifacts

This directory contains the pre-built firmware binary for the **AI Passport
Bridge** running on the FoloToy AI Passport hardware (ESP32-C3, 8 MB Flash,
ESP-IDF 5.5.3). It is produced by the latest build on `main` and is intended
for direct flashing.

## Files

| File | Size | SHA-256 |
| --- | --- | --- |
| `ai-passport-bridge-full.bin` | 2.66 MB | `c2a0178e1760ccde00a629a2d18bdcfb2ec70c1e25a9f8e2b135ad5aad803e3f` |

## Usage

The `.bin` is a merged image (bootloader + partition table + application) and
can be flashed at offset `0x0`:

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 ai-passport-bridge-full.bin
```

## ⚠️ Asset Copyright

The **Desk-pet** pixel sprite assets packaged inside the firmware (see
`main/pet_frames.c` / `tools/pet2lvgl/desk-pet-spritesheet.png`) are
**third-party artwork** contributed to this project for reference only.

> The Desk-pet sprite sheet is provided **for personal study and reference**.
> **Redistribution, modification, repackaging, or reuse in derivative works
> (including forks and re-releases) is NOT permitted.** Please remove the
> desk-pet assets if you build a derivative firmware based on this codebase.

All other source code in this repository is released under the
[MIT License](../../LICENSE).
