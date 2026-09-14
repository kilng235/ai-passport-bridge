<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Made the walkie-talkie reliable end-to-end. Device: the voice task now initializes
  the ES8311 codec itself (previously it depended on a notification beep having played
  first, so recording failed right after a reboot with a mic-init error); the beep
  task fully yields the codec while recording; a ~400 ms warm-up discards the ADC
  startup transient (huge DC offset + clipping) that made ASR return empty text at
  random; and validity is now judged by streamed bytes (>= 0.3 s) instead of wall time.
  PC: an empty transcript no longer injects a placeholder prompt into the session —
  the commit endpoint replies 422 so the device shows a clear failure for re-record.

- Revamped the **Low Power setting page** into a real, functional **Standby & Screen Timeout configuration** (`demo_low_power.c`):
  users can choose between **30s (Rapid Save)**, **1min (Balanced, default)**, **3min (Extended)**, or **Never (Always-on Clock)**.
  The selected timeout is persisted to NVS (`idle_sec`) and dynamically applied to the `power_idle` timer.
  The settings menu item is renamed to Standby and Timeout.

- Raised the walkie-talkie maximum single-recording duration from 10s to 30s
  (`app_voice.c` and `demo_status.c`), providing ample speaking time for complex
  prompts while chunked streaming keeps RAM overhead flat (~1 KB).

- Reverted Wi-Fi Modem-Sleep power saving to keep Wi-Fi at full speed (`WIFI_PS_NONE`):
  the RF sleep window broke the first chunked audio write of a recording session,
  which surfaced on the device as a too-short-recording error. Wi-Fi standby stays
  powered for zero-latency streaming; battery savings remain with the screen-timeout
  configuration.

- Expanded the status carousel into a three-page flow: **Notification (0) <-> Desk Pet (1) <-> Voice Walkie-Talkie (2)**.
  The walkie-talkie now has its own full-screen 8-bit page inside the main status carousel instead of a modal overlay,
  featuring dedicated status indicators, real-time pixel audio visualizer, transcript box, and clear key hints.
  Recording and voice controls are strictly isolated to the voice walkie-talkie page (page 2), avoiding accidental triggers from other pages.

- Voice injection target is now workspace-correct under multi-workspace setups: it
  prefers OpenCode SDK's `ctx.client.session.list()` (the current OpenCode instance's
  own session list, already sorted by most-recently-updated) over the global merged
  bridge state, so a session ID from another workspace no longer gets injected into
  the wrong workspace and fails. The global bridge state is kept as a fallback.

- Hardened the voice path: the inbound 8090 rule is now scoped to **Private networks +
  LocalSubnet only**, the WLAN is classified Private, and the `opencode.exe` Public block
  rules are re-enabled. The voice server additionally only accepts requests **from the
  device's IP** (source allow-list), plus an optional shared token
  (`PASSPORT_VOICE_TOKEN` on the PC, "Voice Token" in the device setup portal, sent as
  `X-Passport-Token`). Previously `POST /api/voice-commit` was unauthenticated, so anyone
  on the LAN could inject a prompt into OpenCode (effectively remote execution).

- Added a "review before send" step to the walkie-talkie: recognition results are no
  longer injected automatically. The device shows the transcript and waits — **OK =
  send / UP = keep talking (audio appended per session on the PC side, then
  re-recognized) / DOWN = discard / double-OK = re-record**. The PC plugin now splits
  into `/api/voice-prompt` (transcribe only), `/api/voice-commit` and
  `/api/voice-cancel`; the voice server is a **process-level singleton** (hot reload
  only swaps the handler), removing the reload port gap that caused "connection to
  the voice service failed". Audio is de-DC'd + peak-normalized and tagged
  `language=zh`; injection now uses OpenCode V2's `PromptInput.text` (the old
  `prompt` key returned HTTP 500) and targets the **most recently active session**
  (the old code injected into the first array entry).

- Reworked the walkie-talkie (voice prompt) flow: it is now **tap OK to start
  recording, tap OK again to stop** (long-press OK goes back to the menu again and
  no longer conflicts with recording). Uploads now use a **streaming chunked POST**
  instead of pre-allocating a 256 KB whole-clip buffer (which can never succeed on
  the PSRAM-less ESP32-C3), and the target host is taken from the source IP of the
  most recent `/notify` (the PC-side bridge) instead of a hardcoded address. The voice
  port now defaults to **8090** (8080 is commonly taken by e.g. NVIDIA Broadcast) and
  can be overridden with `PASSPORT_VOICE_PORT`. The PC-side ASR request now carries a
  `model` field (default **`asr-1.0`**, override with `PASSPORT_ASR_MODEL`) and posts to
  **`https://api.minimaxi.com/v1/speech_to_text`** (override with `PASSPORT_ASR_URL`),
  wrapping the device's raw PCM in a WAV container (that endpoint rejects raw PCM).

- Fixed Chinese/symbol "tofu boxes" in the device UI: added `·` (U+00B7) and `▶`
  (U+25B6) to the glyph subset (`font/symbols.txt`) and regenerated
  `main/lv_font_hansans_14_cjk.c` (the notification page's status tags were missing
  those glyphs). Also fixed pages that created empty labels and set Chinese text
  later, which left them on Montserrat: the sound page's on/off, volume and quiet
  labels and the brightness page's save-result label now re-select the CJK font when
  their text is assigned.

- Fixed the OpenCode bridge **falling back to a stale hardcoded device IP when the
  optional `bonjour-service` dependency is missing**, which left the device stuck on
  "waiting" and receiving no pushes: it now falls back to resolving the device's
  advertised mDNS hostname (`PASSPORT_MDNS_NAME`, default `folopassport.local`) to an
  IPv4 address before connecting, so a DHCP address change no longer breaks it
  (letting `fetch` resolve `.local` itself times out on Windows, so the name is
  resolved to a numeric address first).

- Fixed a regression where the Token Quota page's MiniMax panel always showed
  `login fail` and `NO DATA`: the HTTP `Authorization` header buffer is restored
  to hold the longest key (`APP_CFG_KEY_MAX + 16`) instead of 128 bytes, so long
  `sk-cp-` subscription keys are no longer truncated (which made MiniMax reject
  the login); `parse_minimax` again parses the real `model_remains[]` response
  (`current_interval_remaining_percent` / `current_weekly_remaining_percent` /
  `remains_time` / `weekly_remains_time`) instead of the non-existent
  `model_plans` / `plans` shape.

- Migrated the **OpenCode bridge** to the OpenCode V2 plugin API
  (`Plugin.define({ id, setup })` + `ctx.event.subscribe`) and fixed the reason
  the device received no OpenCode notifications: V2 event payloads live under
  `event.data`, but the bridge read the V1 `event.properties`, so every handler
  silently no-op'd. Field handling now lives in the host-tested `eventIntent`
  helper in `tools/lib/passport-bridge-state.mjs`.

- Taught the OpenCode bridge the events OpenCode 2.0.x actually emits: the live
  stream signals "running" with `session.step.*` / `session.tool.*` /
  `session.text.*` / `session.reasoning.*` activity events (`session.status` is
  not sent by this build), and turn end with `session.execution.succeeded` or a
  final `session.step.ended` (`finish: "stop"`). `eventIntent` normalizes the
  SDK `session.next.*` and runtime `session.*` spellings so either works.

- Fixed the "needs confirmation" state not reaching the device: OpenCode 2.0.x
  surfaces the question/permission prompt as a **form** event (`form.created`,
  session id nested under `data.form.sessionID`), not `question.asked`, so the
  bridge never raised `alert` while a prompt was pending. `eventIntent` now maps
  `form.created` → alert and `form.replied` / `form.rejected` → running, and
  resolves the nested session id. The event-name normalizer also strips the
  `.v2` segment (`question.v2.asked`, `permission.v2.asked`) in addition to
  `.next`, and `question.rejected` resumes.

- Hardened the OpenCode bridge against the runtime's module cache and a slow
  first packet: it imports the shared library from a fresh temporary copy (the
  service caches ESM by path, so `?v=` cache-busting alone was ignored) and waits
  5 s instead of 2 s for the device, which needs longer to answer the first
  request after Wi-Fi power save. Startup logs `eventIntent=ok` / `MISSING`, and
  the first successful send is logged. It also collapses OpenCode's per-location
  plugin instances to a single active one, so each change is pushed once instead
  of once per open location.

- Fixed a **session-name overflow** between the notification bridges and the
  firmware: the shared library clipped a name to a byte budget and then appended
  a `…`, so the result could reach 48 bytes while the device buffer holds only
  47; the parser then cut the ellipsis mid-UTF-8 and rendered a stray glyph. The
  library now caps the **returned** name (ellipsis included) at the firmware's
  `APP_NOTIFY_NAME_MAX`, and a new Node host test
  (`tests/test_passport_bridge_state.mjs`) locks the two limits together.

- Fixed the device staying stuck on "task in progress" when a ZCode session is
  interrupted (ESC) or exits mid-turn — ZCode fires no termination event for
  interrupts, so the bridge never pushes a final state. A **freshness window**
  now applies to `running`: a running message not refreshed for 5 minutes is
  displayed as idle (implemented as `app_notify_kind_effective` in the
  host-tested `app_notify_logic.c`, evaluated on the device against its own
  receive timestamp). To support the window, same-kind pushes are stored
  **silently** (no chime, no screen wake, no re-render beep) and bump a store
  version the Notify page uses to re-render; the ZCode bridge now sends every
  `PreToolUse` as a freshness refresh instead of deduplicating them.

- Added a **ZCode notification bridge** (`tools/zcode/`): five ZCode hooks
  (`SessionStart`=idle bookkeeping only, `UserPromptSubmit`/`PreToolUse`=running,
  `PermissionRequest`=alert, `Stop`=done) forward the
  agent state to the same `POST /notify` endpoint as the
  OpenCode bridge. Both bridges share a PC-side state file
  (`~/.passport-bridge-state.json`, extracted into
  `tools/lib/passport-bridge-state.mjs`), and every event pushes the **merged**
  snapshot — OpenCode and ZCode sessions appear together on the device with the
  aggregate priority `alert > running > done > idle`. The hook script fails fast
  (1 s send timeout, 60 s offline cooldown) because ZCode hooks run inline, and
  a detached decayer keeps the session list honest after `done`. Unchanged
  states are deduplicated so `PreToolUse` never spams the device; an opened but
  idle ZCode session is never shown as running. The OpenCode
  plugin now requires copying `tools/lib/` alongside it. Firmware unchanged.

- Added **quiet hours for notification chimes**: the Sound settings page gains a mute-window preset cycled by long-pressing Up/Down (Off / 22:00-08:00 / 23:00-07:00), persisted in NVS. The window is enforced inside `app_beep_play` — every chime caller is covered, while screen wake, rendering and the desk-pet animation stay untouched (only the sound is muted). The window logic lives in host-testable `main/app_beep_logic.c` (supports cross-midnight windows; clock unsynced = never muted, courtesy of the SNTP time from `app_net`), and the on-page preview now bypasses the window via `app_beep_preview` so volume/toggle feedback still works at night.

- New **desk-pet page** joined to the Notify page as a **Notify ↔ Desk-pet horizontal-slide carousel** (Up/Down switch pages via an LVGL strip x-animation; long-press OK returns). The pet plays one of four 96×96 `lv_animimg` loops matching the aggregate notify state (`idle`/`running`/`done`/`alert`), driven live by background notifications. The green-screen sprite sheet is sliced, chroma-keyed/despilled and packed to LVGL `RGB565A8` C arrays by the new `tools/pet2lvgl` converter (16 frames, ~432 KB; app now 13% free). The main menu gains a Desk-pet entry.

- Notify page now renders a **multi-session list** (up to 3 rows: state colour + session name) when the payload carries `sessions`; only running/done/alert sessions are listed (idle omitted), aggregate priority `alert > running > done > idle`. Payload gains `count` + `sessions[]`; `app_notify_logic` parses the array with top-level keys scoped by brace depth, session names re-select the CJK/Latin font per render, and the HTTP body limit is raised to 1280 B. The OpenCode plugin tracks sessions (name from `Session.title`), aggregates and debounces them, decays `done`→`idle` after 5 s, and handles `session.created/updated/deleted` and `permission.replied`.

- Fixed the Notify state after an approval: `alert`/`done` are transient overlays that previously always fell back to idle; they now return to the underlying persistent state (`running`/`idle`), and the plugin re-sends `running` on `permission.replied`/`question.replied`. Work that continues after approval no longer shows as idle.

- **P1 — unattended (background) notifications.** New `app_net` service is the app's single owner of the Wi-Fi STA (started at boot, auto-reconnect, SNTP); the quota and Wi-Fi-scan pages now reuse it instead of each running their own `esp_wifi_init`/`deinit`, so Wi-Fi no longer conflicts between pages. `app_notify` now starts **on boot and stays online in the background** — the Notify page no longer disables it on exit, so notifications arrive on any page. The device advertises `folopassport.local` / `_folopassport._tcp:80` via mDNS (`espressif/mdns`). The OpenCode plugin resolves the endpoint as `PASSPORT_URL` → `PASSPORT_HOST` → mDNS (`bonjour-service`) → `PASSPORT_FALLBACK`. The setup portal temporarily pauses the resident STA + notify server and resumes them on exit.

- Notify page: merged the duplicated state text into a **single kind-driven line** (colored), with the message `text` shown below as detail; `done`/`alert` fall back to idle after 5 s. Previously the kind label and the message title both rendered the state word (e.g., the idle word and the task title at once).

- Fixed a placeholder box on the Notify page: the status separator used `·` (U+00B7), which the CJK subset does not contain; switched to `・` (U+30FB), which the font covers.

- Lowered the button **long-press threshold** from 1.5 s to 0.8 s (`CONFIG_BUTTON_LONG_PRESS_TIME_MS=800`) and debounced repeated `LONG` events in `main.c`. Previously a press shorter than the threshold was treated as a click (on the home menu that enters the selected item, which looked like a "refresh" instead of returning); the debounce also prevents one long press from being handled twice.

- Fixed "failed to connect with the device" when flashing/monitoring: with automatic light sleep enabled, the ESP32-C3 USB-Serial-JTAG stops responding while the chip sleeps, so `esptool`/the web flasher timed out with "No serial data received". `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y` now keeps the device awake while USB is connected and only light-sleeps once unplugged.

- Reworked the notification **state model** to four states aligned with OpenCode's `session.status` (`idle`/`busy`/`retry`): `idle` (default; unknown kinds), `running` (`busy`/`retry`), `done` (`session.idle`), and `alert` (`permission.asked` / `question.asked` / `session.error`). The plugin now subscribes to `session.status` (emitting `running` only on the busy transition) and maps `question.asked`/`session.error` to `alert`. The Notify page falls back from `done`/`alert` to `idle` after 5 s; the old `info`/`error` kinds were removed (legacy `"error"` still parses to `alert`).

- Added a **Sound** settings page: OK toggles the notification chime on/off and UP/DOWN sets the volume (10–100%, step 10). Changes preview immediately and are persisted to NVS (`pcfg`, keys `beep_on` / `beep_vol`); saving runs on a worker so key callbacks never block.

- New notifications play a short **chime** per state: a **descending** two-note for `running`, a **rising** two-note for `done`, two high beeps for `alert`, and a single note for `idle`. Implemented in `main/app_beep.c` with an on-demand ES8311/I2S open and an immediate `bsp_audio_suspend()` afterwards to save power, using an integer sine table (no libm on the FPU-less C3).

- Expanded the on-device CJK font to the full **GB2312** set (6763 Hanzi + punctuation/symbols, 7540 glyphs total) so dynamic Chinese in notifications renders instead of placeholder boxes. The generated `main/lv_font_hansans_14_cjk.c` grew from ~336 KB to ~650 KB of flash. Regenerate with `lv_font_conv --font <otf> --size 14 --bpp 4 --range 0x20-0x7f --symbols "$(cat font/symbols.txt)" ...`; `LV_LVGL_H_INCLUDE_SIMPLE=1` is now defined in `main/CMakeLists.txt` so the generated font compiles without the missing `lvgl/lvgl.h` path.

- Added an **OpenCode notification bridge**: a new Notify page joins the home Wi-Fi as a STA and runs `POST /notify`, displaying the latest message pushed by the companion OpenCode plugin (`tools/opencode/passport-notify.js`) for `permission.asked` / `session.idle` / `session.error`. The flat-JSON payload parser lives in the host-tested `main/app_notify_logic.c`; the message store and HTTP service live in `main/app_notify.c`. Only key events are forwarded (no message content). Wi-Fi is an exclusive resource, so the link is active only while the Notify page is open.

- Enabled **power management (DFS + automatic light sleep)**: `esp_pm_configure()` runs the CPU at 160 MHz under load and drops to 80 MHz when idle, and light-sleeps when nothing holds a lock. The BSP holds an `ESP_PM_NO_LIGHT_SLEEP` lock while the backlight is on (the digital LEDC peripheral would otherwise stop the PWM), so light sleep engages only after the screen turns off — cutting idle draw from ~25 mA toward single-digit mA. `CONFIG_PM_ENABLE`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, `CONFIG_PM_SLP_IRAM_OPT`, and `CONFIG_PM_RTOS_IDLE_OPT` are set in `sdkconfig.defaults`.

- **Removed the network radio** feature: the radio page, city selector, C++ stream player (`radio_player.cc`), catalog (`radio_catalog.cc`), and the city database are gone, along with the `espressif/esp_audio_codec` (MP3) dependency. `demo_radio.c/.h` is kept as the shared NVS/netif/event-loop helper still used by the provisioning portal, Token Quota, and Wi-Fi pages. The home menu is now `Token Quota` / `Settings`, and settings is `Wi-Fi` / `Low power` / `Brightness` / `Network`. Firmware size dropped about 690 KB (2.55 MB → 1.84 MB).

- Fixed radio catalog fetch failures (city switching reporting a network error): the directory query originally hit a single server (de1.api.radio-browser.info), which is frequently unreachable or rate-limited from mainland networks. Testing showed plain-HTTP requests to the overseas nodes get disrupted, so queries now run over **HTTPS** against a de1/at1/nl1 node pool (mirroring what RadioBunny proves works from Chinese networks), retrying the next node on empty results; the city page shows staged status (connecting / fetching / network down / fetch failed), and the connect and catalog workers grew to 8 KB stacks.

- Fixed missing-glyph "boxes" on device: the subset font's symbol table previously held only Han characters, so ASCII inside mixed-language strings (e.g. "OK" in a hint line) and marks like the middle dot had no glyphs once the string was routed to the Chinese font. The subset now covers **GB2312 level-1 common characters (3755) + project vocabulary + common punctuation + ASCII (0x20-0x7F)**, regenerated via lv_font_conv, so any typical Chinese station name from the network directory renders without per-feature font updates.

- The radio gains **city switching** (ported from leo-radio's city stations): a new `City` page in the settings menu (Auto / Beijing / Shanghai / Guangzhou / Shenzhen / Changsha / Hangzhou / Chengdu) connects in the background on OK, fetches that city's catalog into the player, and returns to the radio page on the next keypress; the choice persists in NVS `radio/city` and the radio page discovers by the selected city on entry. Wi-Fi STA connect/disconnect was extracted into a shared `demo_radio` helper and `radio_player_init` is now idempotent (both pages may trigger it).

- Stitched the **internet radio** from [leo-radio](https://github.com/leo0183/leo-radio) (MIT-licensed player and station catalog): the home menu gains a Radio page that connects Wi-Fi on entry and starts playing automatically. UP/DOWN short presses switch stations, long presses adjust volume (10–100%, persisted in the NVS `radio` namespace), a short OK toggles playback, and long-press returns to the menu. Ported `radio_player.cc` (HTTP stream → esp_audio_codec MP3 decode → ES8311 output, with the level meter and reconnect handling) and `radio_catalog.cc` (IP geolocation + radio-browser directory, falling back to six built-in stations); the player's UI callbacks are decoupled through `main/radio_bridge.h`, and `radio_player.h` was made C-compatible for direct use from the page. The page UI follows the theme: tuning needle (real frequencies land on the 88–108 scale, frequency-less stations spread by index with a blank readout), an 18-segment level meter, and a playback status row.

- **Bluetooth stack disabled** (`CONFIG_BT_ENABLED=n`, the same trade-off leo-radio made): the ESP32-C3 has no classic Bluetooth and no practical audio use for BLE, so dropping the stack frees runtime memory for MP3 streaming; the Bluetooth demo page is removed from the firmware (source kept) and the Wi-Fi streaming buffer settings now match leo-radio's field-tested values. The CJK font subset grew from 529 to 619 glyphs (`font/symbols.txt` + regenerated via lv_font_conv) to cover radio station names and playback status strings.

- Device-wide UI reskin borrowing the design language of [leo-radio](https://github.com/leo0183/leo-radio), a sibling project for the same hardware: near-black blue background (`0x071017`), amber accent (`0xFFB74D`), hairline grid dividers, and warm-white/muted-teal text tiers. Menus switch to leo's row-card style (selected row = amber fill with dark text) with a consistent bottom "divider + key hints" strip. New `main/ui_theme.h/.c` is the single source for the palette and shared widgets; the legacy light pages (Wi-Fi scan / Bluetooth / Low power / Provisioning) reskin automatically via same-name constant remapping in `ui_pixel.h` plus rewritten `ui_pixel_screen_create`/`ui_pixel_panel_create` internals, which now draw a centered title + divider header. The mascot is kept.

- The Token Quota page reskins to match and gains a leo-style status bar: brand text (amber) + clock + **battery percentage** (via the CW2017 fuel gauge `bsp_battery_soc()`, polled every 30 s, red below 15%, blank when no gauge answers). The Brightness page adopts the leo volume-page layout (centered title + large percentage + wide rounded bar).

- Added a **Brightness** page to the settings menu: UP/DOWN short presses step the backlight by 10% (range 20–100%, floored at the idle dim level), with a live preview bar on-screen; a short OK press persists the value to NVS through a one-shot worker task (separate integer key, leaving the config blob untouched). The saved level is applied at boot and used as the idle screen-saver's restore level; leaving the page without OK discards the preview. The settings menu now has five entries and the card layout was compacted to fit the 240x320 panel; the step/clamp logic lives in host-testable `main/brightness_logic.c` with its own host test.

- Restructured navigation into two levels: the device boots straight into **Token Quota**; long-pressing OK returns to the home menu (`Token Quota` / `Settings`), and `Settings` opens a settings menu (`Wi-Fi` / `Bluetooth` / `Low power` / `Network`). The Display/Button/Audio/Battery demos were removed from the menu (source files kept); long-press walks back up one level, and long-pressing on the home menu returns to Token Quota.

- The **provisioning portal** (on-device label `Network`) now scans nearby 2.4 GHz Wi-Fi: the SoftAP runs in APSTA mode and serves `GET /scan`, returning a de-duplicated, RSSI-sorted JSON list. The web form shows a dropdown plus a rescan button and scans automatically on load, so the user picks a network instead of typing the SSID (manual entry still works). ESP32-C3 is 2.4 GHz-only, so results are inherently 2.4 GHz.

- Re-introduced the Simplified Chinese font: a 14 px SourceHanSansSC subset (`main/lv_font_hansans_14_cjk.c`, generated from the OTF bundled with the LVGL component) now truly falls back behind Montserrat via `main/ui_font.c`, fixing the earlier macro-guard mismatch that had left the fallback compiled out. The Settings menu labels now render in Chinese on-device.

- Fixed fully blank CJK labels on-device: the generated font stores RLE-compressed bitmaps (`.bitmap_format = LV_FONT_FMT_TXT_COMPRESSED`), so `CONFIG_LV_USE_FONT_COMPRESSED=y` is now set in `sdkconfig.defaults`. Without it LVGL's `get_glyph_bitmap` takes the `#else` branch and returns `NULL`, so every Chinese glyph rendered empty while ASCII (Montserrat, plain format) still showed.

- Restored TLS server certificate verification for the Token Quota requests: the HTTP client now validates the server chain and hostname against the built-in CA bundle (`esp_crt_bundle_attach`), replacing the `ESP_TLS_INSECURE` / `SKIP_SERVER_CERT_VERIFY` debug switches and the common-name check bypass that left the stored DeepSeek/MiniMax API keys open to man-in-the-middle interception.

- Added a **Setup SoftAP portal**: the menu entry starts an access point (`FoloPassport-XXXX`, password `folotoy123`) with a minimal HTTP server that serves only `/` (form) and `/save`. Phones join the hotspot and open `http://192.168.4.1` to store the Wi-Fi credentials and DeepSeek/MiniMax API keys, persisted as an NVS blob via the host-testable `main/app_cfg.c` model and `main/app_portal.c`. Saving reboots the device; the Token Quota page now reads its credentials from NVS instead of hardcoded source strings, and a first boot without saved Wi-Fi opens Setup automatically. From the Token Quota page, a short DOWN press reopens Setup to change Wi-Fi or API keys.

- Added a global idle screen-saver: after 30 s without input the backlight dims to 20%, after 60 s it turns off, and any button restores full brightness. The level decision lives in host-tested `main/power_idle_logic.c`; the Display and Low Power pages temporarily disable it while they drive the backlight themselves.

- Boot now always lands on the Token Quota page (or the provisioning portal when no Wi-Fi/keys are configured); the temporary `BOOT_PAGE_INDEX` indirection was removed.

- Added the supplied 80-byte CW2017 profile for the specified 520 mAh cell, including content/update-flag checks, verified writes, the required restart sequence, and bounded SOC-readiness polling.

- Expanded the environment bootstrap document: added Espressif's Git service mirror (`git.espressif.com.cn`) as the preferred mainland-China route for ESP-IDF v5.5.3 and its submodules, documented submodule long-wait/timeout handling, in-place repair, and the pinned-commit shallow fetch for large submodules such as `esp32-wifi-lib`, warned about stale per-repository Jihulab `insteadOf` residue, and added the official offline release archive as a last-resort fallback (learned from `esp-mosaico/esp-mosaico-vibe`).

- Reorganized the documentation by function area with a dual entry point: the root `AGENTS.md` is now a thin router (hard constraints + task routing only) and the detailed AI workflow lives in `docs/development/ai-guide.md`; `agent-guide.md` was folded in. `docs/development/` gained a second level (`engineering/`, `ci/`, `release/`), and the `plays/` application archive and `experiences/` moved into a `docs/reference/` area with a dedicated README. Removed `docs/software-design/` (empty scaffold); folded the three `assets/{fonts,images,music}/README` leaves into the `assets/` README; flattened the six `project-completion` sub-documents into a single file; and unified each directory to a single README, eliminating every `INDEX` file and a duplicated experience index. All cross-references and bibliographic links were updated; no content was dropped.

- Removed the obsolete app/test partition at `0x700000` and its related
  bootloader, validation, and documentation requirements. The fixed protected
  `cardid` partition and its CI checks remain unchanged.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
