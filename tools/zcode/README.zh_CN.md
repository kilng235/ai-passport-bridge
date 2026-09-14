<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# ZCode → FoloToy AI Passport 通知转发

一组 ZCode 钩子配置,把 ZCode 的**关键会话事件**经局域网转发到设备,让 Passport
与 OpenCode 桥一起显示 ZCode agent 的状态(进行中/完成/需要确认)。固件侧为
`app_notify` 服务与**通知**页——固件零改动。

## 转发哪些事件

| ZCode 钩子事件 | 设备显示 |
| -------------- | ------------ |
| `SessionStart` | 不显示(会话打开但空闲——只记账) |
| `UserPromptSubmit` | 任务进行中(running) |
| `PreToolUse` | 任务进行中(running;权限批准后清掉残留的 alert) |
| `PermissionRequest` | 需要确认(alert) |
| `Stop` | 任务完成(done;设备自行衰减回空闲) |

状态未变化的钩子会整条去重:kind 与名称都没变就跳过推送——否则高频的
`PreToolUse` 会不停刷设备(每条推送都会响提示音)。

只发送状态与固定名(`ZCode <项目名>`)——**不含提示词、代码、消息正文**。

## 与 OpenCode 桥共享聚合

两个桥读写同一份状态文件(`~/.passport-bridge-state.json`,可用 `PASSPORT_STATE`
覆盖)。任一事件都会更新自己的源并推送**合并**后的快照,设备上 OpenCode 与
ZCode 的会话同屏显示(聚合优先级 `alert > running > done > idle`,最多 3 行会话)。

## 安装

1. 把下面的 hooks 块复制进 `~/.zcode/cli/config.json`(顶层,与其他键并列)。
   配置文件钩子必须显式 `"hooks.enabled": true`。
2. 把 args 里的路径指向本仓库脚本(绝对路径)——脚本按相对路径引用
   `tools/lib/passport-bridge-state.mjs`,仓库需保持在原位。
3. 新开一个 ZCode 会话(钩子在启动时读取)。

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

需要 Node ≥ 18(内置 fetch)。脚本永远 exit 0 且无 stdout,阻塞会话的时间
不会超过 1 秒的发送超时。

## 环境变量

- `PASSPORT_URL` — 完整 URL,如 `http://192.168.0.109/notify`(最高优先)
- `PASSPORT_HOST` — 仅主机名,如 `folopassport.local`
- `PASSPORT_FALLBACK` — 兜底 URL(默认 `http://192.168.0.109/notify`)
- `PASSPORT_STATE` — 覆盖共享状态文件路径

## 行为说明

- 钩子**内联执行**:设备不可达时发送 1 秒超时,并写 60 秒冷却(存于状态文件),
  期间跳过发送——离线的设备不会拖慢 agent。
- `done` 的衰减由设备端负责;另有一个 detached 衰减进程补推衰减后的状态,
  保证会话列表不失真。
- ZCode 钩子拿不到会话标题,显示名固定为 `ZCode <项目目录名>`。
- 跨 agent 的多会话聚合依赖两个桥在同一台 PC 上共享状态文件;第二台机器
  需要自己的副本。
