<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Firmware Artifacts

This directory contains pre-built firmware binaries for the FoloToy AI Passport
(ESP32-C3, 8 MB Flash, ESP-IDF 5.5.3). These artifacts are produced by the
latest build in `main` and are intended for direct flashing.

## Files

| File | Size | SHA-256 |
| --- | --- | --- |
| `FoloToy-AI-Passport-full.bin` | 2.66 MB | `c2a0178e1760ccde00a629a2d18bdcfb2ec70c1e25a9f8e2b135ad5aad803e3f` |
| `FoloToy-AI-Passport.bin` | 2.61 MB | `fc9e26f1245a249d13216daaf0a00d26f525774b44c013eed33bee09ad815c12` |
| `merged-binary.bin` | 1.77 MB | `6a1d7bed11c1dd2748c102782c52f5cc0af2fb745d2acc86bb44b207a7dd7a56` |
| `partition-table.bin` | 3.0 KB | `a98e0784a39f37da373daf6dcef7a400af3e98b5344e89f414eeebedb1b5e07c` |
| `bootloader.bin` | 20.5 KB | `4a21d2562ee6f4bd605d319391dcdad849df6cb5555fc183acd0d5369092f209` |

## Usage

For a single-shot flash of the merged image, use `merged-binary.bin`. For
component-wise flashing (bootloader + partition table + app), use the
individual files.

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash \
    0x0       bootloader.bin \
    0x8000    partition-table.bin \
    0x10000   FoloToy-AI-Passport.bin
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

