<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# ZCode → FoloToy AI Passport notifications

A ZCode hooks setup that forwards **key session events** from ZCode to the device
over the LAN, so the Passport shows the ZCode agent's state (running / done /
needs confirmation) alongside the OpenCode bridge. The device firmware side is
the `app_notify` service and the **Notify** page — no firmware changes needed.

## What is forwarded

| ZCode hook event | Device shows |
| -------------- | ------------ |
| `SessionStart` | Nothing (session opened but idle — bookkeeping only) |
| `UserPromptSubmit` | Task in progress (running) |
| `PreToolUse` | Task in progress (running; clears a stale alert once an approval goes through) |
| `PermissionRequest` | Needs confirmation (alert) |
| `Stop` | Task done (done; the device decays it back to idle itself) |

Unchanged states are deduplicated: a hook whose kind and name already match the
stored state skips the push entirely, so the high-frequency `PreToolUse` events
never spam the device (each push would otherwise ring the chime).

Only the state and a fixed name (`ZCode <project>`) are sent — **no prompt text,
no code, no message content**.

## Shared aggregation with the OpenCode bridge

Both bridges read/write the same state file
(`~/.passport-bridge-state.json`, override with `PASSPORT_STATE`). Every event
updates its own source and pushes the **merged** snapshot, so the device shows
OpenCode and ZCode sessions together (aggregate priority
`alert > running > done > idle`, up to 3 session rows).

## Install

1. Copy the hooks block below into `~/.zcode/cli/config.json` (top level, next
   to other keys). Configuration-file hooks must set `hooks.enabled: true`.
2. Point the args at this repo's script (absolute path) — the script imports
   `tools/lib/passport-bridge-state.mjs` by relative path, so the repo must stay
   where it is.
3. Start a new ZCode session (hooks are read at startup).

```json
"hooks": {
  "enabled": true,
  "events": {
    "SessionStart": [
      { "hooks": [ { "type": "process", "command": "node",
        "args": ["<repo>/tools/zcode/passport-zcode.mjs", "idle", "${CLAUDE_SESSION_ID}", "${CLAUDE_PROJECT_DIR}"],
        "timeoutMs": 4000 } ] }
    ],
    "PreToolUse": [
      { "hooks": [ { "type": "process", "command": "node",
        "args": ["<repo>/tools/zcode/passport-zcode.mjs", "running", "${CLAUDE_SESSION_ID}", "${CLAUDE_PROJECT_DIR}"],
        "timeoutMs": 4000 } ] }
    ],
    "UserPromptSubmit": [
      { "hooks": [ { "type": "process", "command": "node",
        "args": ["<repo>/tools/zcode/passport-zcode.mjs", "running", "${CLAUDE_SESSION_ID}", "${CLAUDE_PROJECT_DIR}"],
        "timeoutMs": 4000 } ] }
    ],
    "PermissionRequest": [
      { "hooks": [ { "type": "process", "command": "node",
        "args": ["<repo>/tools/zcode/passport-zcode.mjs", "alert", "${CLAUDE_SESSION_ID}", "${CLAUDE_PROJECT_DIR}"],
        "timeoutMs": 4000 } ] }
    ],
    "Stop": [
      { "hooks": [ { "type": "process", "command": "node",
        "args": ["<repo>/tools/zcode/passport-zcode.mjs", "done", "${CLAUDE_SESSION_ID}", "${CLAUDE_PROJECT_DIR}"],
        "timeoutMs": 4000 } ] }
    ]
  }
}
```

Requires Node ≥ 18 (built-in fetch). The script always exits 0 with no stdout,
and it never blocks the session longer than the 1 s send timeout.

## Environment variables

- `PASSPORT_URL` — full URL, e.g. `http://192.168.0.109/notify` (highest priority)
- `PASSPORT_HOST` — host only, e.g. `folopassport.local`
- `PASSPORT_FALLBACK` — fallback URL (default `http://192.168.0.109/notify`)
- `PASSPORT_STATE` — override the shared state file path

## Behavior notes

- Hooks run **inline**: if the device is unreachable the send times out after 1 s
  and a 60 s cooldown (stored in the state file) skips further sends, so an
  offline device never slows the agent down.
- `done` decays on the device itself; a detached decayer process additionally
  pushes the decayed state so the session list stays honest.
- The ZCode hook cannot read the session title, so the display name is fixed to
  `ZCode <project dir name>`.
- Multi-session aggregation across agents works because both bridges share the
  state file on the same PC; a second machine would need its own copy.
