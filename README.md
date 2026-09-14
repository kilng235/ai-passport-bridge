# FoloToy AI Passport Firmware & Design Architecture

<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

This repository contains the firmware and complete architecture for the **FoloToy AI Passport**, an open-source wearable AI hardware device based on the ESP32-C3.

---

## 🌟 Key Features

1. **OpenCode & ZCode Dual-Agent Integration**
   - **OpenCode Companion (OpenCode V2)**: `tools/opencode/passport-notify.js`, communicates with local OpenCode V2 instances via `Plugin.define`.
   - **ZCode Bridge**: `tools/zcode/passport-zcode.mjs`, captures lifecycle events via ZCode CLI hooks.
   - **Multi-Source Aggregation**: Shares state (`~/.passport-bridge-state.json`) between OpenCode and ZCode, rendering unified agent states (idle / running / done / requires confirmation) with priority scheduling.
   - **Desk Pet**: 96×96 animated pixel companion synchronized with live agent states (Idle/Running/Done/Alert).
   - **Voice Walkie-Talkie (Voice Prompt)**: Hardware button recording -> PC-side ASR transcription -> on-device confirmation before injection into active sessions.
   - **Security**: Device IP allowlist + optional shared `PASSPORT_VOICE_TOKEN` authentication.

2. **🚨 Voice Prompt Compatibility Notice**
   - **Currently only compatible with OpenCode V2**: The voice injection endpoint `/api/voice-commit` relies on the OpenCode V2 SDK (`ctx.client.session.list()`, `ctx.client.session.prompt()`) for session lookup and prompt injection. The legacy V1 architecture is **not** supported.
   - **Integration Steps (OpenCode V2)**:
     1. Copy `tools/opencode/passport-notify.js` and `tools/lib/passport-bridge-state.mjs` to the OpenCode plugins directory:
        - Global: `~/.config/opencode/plugins/passport-notify.js` + `~/.config/opencode/plugins/lib/passport-bridge-state.mjs`
        - Or per-project: `.opencode/plugins/` (keep the directory layout)
     2. Restart OpenCode (the plugin is loaded on startup).
     3. Configure environment variables:
        ```bash
        export PASSPORT_URL=http://<device-IP>/notify   # or PASSPORT_HOST=folopassport.local
        export PASSPORT_VOICE_TOKEN=<shared-token>      # optional; set the same value in the device setup portal
        ```
     4. On the device: open the **Notify / Desk-pet** page. Short-press `OK` to start recording, short-press again to stop — the PC runs ASR, the device shows the transcript and waits for confirmation (`OK` send / `UP` continue / `DOWN` discard / double-`OK` re-record).
   - **Protocol Endpoints**: `POST /api/voice-prompt` (ASR only, returns `{text}`), `POST /api/voice-commit` (inject), `POST /api/voice-cancel` (discard). See `tools/opencode/README.md` for full payload and response schema.
2. **Connectivity & Web Portal**
   - Built-in SoftAP Web Portal (`192.168.4.1`) for dynamic Wi-Fi and LLM API Key configuration.
   - Background network daemon (`app_net`) with auto-reconnect and mDNS advertising (`folopassport.local`).
3. **Internet Radio & Multimedia**
   - City station catalog with HTTPS node failover, stream parsing, and ES8311 hardware audio playback.
4. **Security & Privacy**
   - Zero hardcoded credentials or API keys; all secrets are persisted locally in NVS.
   - TLS CA bundle validation to prevent MITM tampering.

---

## 📁 Repository Structure

```text
├── components/bsp/      # Board Support Package (ST7789P3 LCD, ES8311 Audio, CW2017 Battery, ADC Keys)
├── main/                # Application logic, LVGL screens, state machines, and network services
├── docs/                # Architectural design notes, hardware specs, and engineering guides
│   ├── hardware-design/ # Hardware specifications and power analysis
│   ├── reference/       # Architectural trade-offs, streaming, display refresh optimizations
│   └── development/     # AI collaboration guide and engineering standards
├── tools/               # Tooling & companion plugins
│   ├── opencode/        # OpenCode V2 companion plugin (notifications & voice prompt)
│   ├── zcode/           # ZCode bridge integration
│   ├── pet2lvgl/        # Chroma-keying and sprite packager for LVGL
│   └── check_repo.py    # Static security and compliance checker
├── tests/               # Host-based unit tests
├── partitions.csv       # 8 MB Flash partition table (with protected cardid layout)
└── sdkconfig.defaults   # Baseline project configuration
```

---

## 📦 Pre-built Firmware Download

> Ready-to-flash merged image. No ESP-IDF toolchain required.
> Merged image (bootloader + partition table + application), **flash at offset `0x0`** with a single command.
> **For personal study and reference only — please do not redistribute.**

| File | Size | SHA-256 |
| --- | --- | --- |
| `FoloToy-AI-Passport-full.bin` | 2.66 MB | `c2a0178e1760ccde00a629a2d18bdcfb2ec70c1e25a9f8e2b135ad5aad803e3f` |

**Flash command**:

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 FoloToy-AI-Passport-full.bin
```

**Full instructions**: [`build/firmware/README.md`](build/firmware/README.md)

**Download entry**: [GitHub Releases → v1.0.0-main](https://github.com/kilng235/folotoy-ai-passport/releases/tag/v1.0.0-main)

> Or download directly from the repo:
>
> ```bash
> curl -L -o FoloToy-AI-Passport-full.bin \
>   https://raw.githubusercontent.com/kilng235/folotoy-ai-passport/main/build/firmware/FoloToy-AI-Passport-full.bin
> ```

### ⚠️ Desk-pet Asset Copyright Notice

The **Desk-pet pixel sprite assets** packaged inside the firmware (see
`main/pet_frames.c` / `tools/pet2lvgl/desk-pet-spritesheet.png`) are
**third-party artwork provided for personal study and reference only**.
**Modification, redistribution, repackaging, or reuse in derivative works
(including forks and re-releases) is NOT permitted.** If you publish a
derivative firmware based on this codebase, please **remove the desk-pet
assets before release**. All other source code is released under the
[MIT License](LICENSE).

---

## 🛠️ Build & Development

- **Target**: ESP32-C3 (8 MB Flash, No PSRAM)
- **Framework**: ESP-IDF v5.5.3

### Running Host Tests & Compliance Check

```bash
python tools/check_repo.py
python -m unittest discover -s tests
```

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
