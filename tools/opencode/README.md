<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# OpenCode → FoloToy AI Passport notifications

An OpenCode plugin that forwards a few **key events** from OpenCode to the device
over the LAN, so the Passport can show the task state (idle / running / done /
needs approval) without any cloud relay. The device firmware side is the `app_notify` service and
the **Notify** page.

## What is forwarded

| OpenCode event | Device shows |
| -------------- | ------------ |
| `session.status` = `busy`/`retry` | Task in progress (running) |
| `session.idle` | Task done (done, debounced 3s) |
| `permission.asked` / `question.asked` / `session.error` | Needs confirmation (alert) |
| `permission.replied` / `question.replied` | Task in progress (running) |
| `session.created` / `session.updated` | record the session name |
| `session.deleted` | drop the session |

`idle` is the resting state. `done` and `alert` are **transient overlays**: after
5 s the device returns to the underlying persistent state (`running` or `idle`),
so an approval does not leave the display stuck on `idle` while work continues.

When several sessions are active at once, the device shows a **session list** (up
to 3 rows, name + state colour) with aggregate priority
`alert > running > done > idle`; idle sessions are omitted. Only these events are
sent — **no prompt text, no code, no message content**.

## Install

1. Copy the plugin **and its shared library** into your OpenCode plugin
   directory (the plugin imports `../lib/passport-bridge-state.mjs` by relative
   path, so the directory layout must be preserved):
   - Global: `~/.config/opencode/plugins/passport-notify.js` **plus**
     `~/.config/opencode/plugins/lib/passport-bridge-state.mjs`
   - Or project: `.opencode/plugins/…` with the same layout
2. Restart OpenCode (plugins load at startup).

Requires **OpenCode V2**: the plugin is a `Plugin.define({ id, setup })` module
that subscribes with `ctx.event.subscribe` and reads session events from
`event.data`. Plugins in `~/.config/opencode/plugins/` are auto-discovered; no
`opencode.json` entry is required.

> The plugin imports `lib/passport-bridge-state.mjs` from a **fresh temporary
> copy**, so a plugin reload picks up library changes even though the OpenCode
> server caches ESM by path. If the startup log shows `eventIntent=MISSING` the
> running library is stale — restart the background service
> (`opencode service restart`). The first request after the device's Wi-Fi power
> save can take a few seconds, so the send timeout is 5 s.

The plugin shares its aggregation state with the **ZCode bridge**
(`tools/zcode/`): both write `~/.passport-bridge-state.json` and push the merged
snapshot, so OpenCode and ZCode sessions appear together on the device.

## Configure

Environment variables (set them before starting OpenCode):

| Variable            | Default                       | Meaning |
| ------------------- | ----------------------------- | ------- |
| `PASSPORT_URL`      | unset                         | Full endpoint URL (highest priority) |
| `PASSPORT_HOST`     | unset                         | Host only, e.g. `folopassport.local` |
| `PASSPORT_FALLBACK` | `http://192.168.0.109/notify` | Last-resort URL |
| `PASSPORT_MDNS`     | unset                         | `0` disables mDNS discovery |
| `PASSPORT_OFF`      | unset                         | `1` disables forwarding |
| `PASSPORT_VOICE_PORT` | `8090`                      | Walkie-talkie voice port (must match `VOICE_PORT` in the device `app_voice.c`) |
| `PASSPORT_ASR_MODEL`  | `asr-1.0`                   | MiniMax ASR model (the endpoint only supports `asr-1.0`) |
| `PASSPORT_ASR_URL`    | `https://api.minimaxi.com/v1/speech_to_text` | ASR endpoint |
| `PASSPORT_ASR_LANG`   | `zh`                        | Language hint |
| `PASSPORT_VOICE_TOKEN`| unset                       | When set, voice requests must carry `X-Passport-Token` (put the same value in the device portal) |

### Walkie-talkie (voice prompt)

On the device's Notify page, tap `OK` to start recording and tap `OK` again to stop; the
PC transcribes and the device shows the transcript for confirmation (`OK` send /
`UP` keep talking / `DOWN` discard / double-`OK` re-record). Protocol:
`POST /api/voice-prompt` (transcribe only, returns `{text}`), `/api/voice-commit`
(inject), `/api/voice-cancel` (discard).

Security: the voice server only accepts requests **from the device's IP**; for stronger
protection set `PASSPORT_VOICE_TOKEN` on the PC and the same token in the device portal.
The device sends raw PCM; the plugin de-DC's + peak-normalizes it and wraps it in a WAV
container before submitting.

The endpoint is resolved in order: `PASSPORT_URL` → `PASSPORT_HOST` → mDNS
(`_folopassport._tcp`) → `PASSPORT_FALLBACK`. For zero-config discovery install
the optional dependency once:

```bash
npm i -g bonjour-service
```

Without it, set `PASSPORT_URL` (or `PASSPORT_HOST`) to the device's LAN address.
The device prints its IP on the serial console at boot; a DHCP reservation keeps
it stable.

## Device side

From firmware P1 the device brings up Wi-Fi and `POST /notify` **on boot and keeps
them online in the background**, so notifications arrive on any page — you open
the **Notify** page only to *look* at the latest one. The status line on
that page shows whether the service is online. The device also advertises
`folopassport.local` via mDNS.

Payload the device accepts:

```json
{
  "kind": "running",
  "count": 2,
  "title": "Task in progress",
  "text": "",
  "time": 1731234567890,
  "sessions": [
    { "name": "FoloToy-AI-Passport-full.bin build", "kind": "running" },
    { "name": "kitty multi-expression pack", "kind": "done" }
  ]
}
```

`kind` is one of `idle` / `running` / `done` / `alert`; missing fields default
sensibly (`idle`). `sessions` is optional (max 3); when present the device shows
the list, otherwise the single `title`/`text` line. A non-JSON body is shown as
plain text.

## Test without OpenCode

With the device reachable (any page):

```bash
curl -X POST http://<device-ip>/notify \
  -H "content-type: application/json" \
  -d '{"kind":"done","title":"test","text":"hello"}'
```

The page should show the message and wake the screen.
