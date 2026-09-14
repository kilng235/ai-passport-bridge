<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# OpenCode → FoloToy AI Passport 通知

一个 OpenCode 插件:把 OpenCode 的**少数关键事件**经局域网转发到设备,让 Passport
显示任务状态(空闲 / 进行中 / 完成 / 需要确认),无需任何云端中转。设备端是
`app_notify` 服务与**通知**页。

## 转发哪些事件

| OpenCode 事件 | 设备显示 |
| ------------- | -------- |
| `session.status` = `busy`/`retry` | 任务进行中 |
| `session.idle` | 任务完成(3 秒去抖) |
| `permission.asked` / `question.asked` / `session.error` | 需要确认 |
| `permission.replied` / `question.replied` | 任务进行中 |
| `session.created` / `session.updated` | 记录会话名 |
| `session.deleted` | 移除会话 |

`idle`(空闲)是静息态。`done`/`alert` 是**瞬时叠加提醒**:5 秒后回到背后的持续基态
(`running` 或 `idle`),因此"确认后任务仍在跑"不会错误地停在空闲。

多个会话同时进行时,设备显示**会话列表**(最多 3 行,会话名 + 状态色),聚合优先级
`确认 > 进行中 > 完成 > 空闲`;空闲会话不列出。只发这些事件——**不含提示词、
不含代码、不含消息正文**。

## 安装

1. 把插件**和共享库**一起拷到 OpenCode 插件目录(插件按相对路径引用
   `../lib/passport-bridge-state.mjs`,目录结构必须保持):
   - 全局:`~/.config/opencode/plugins/passport-notify.js` **以及**
     `~/.config/opencode/plugins/lib/passport-bridge-state.mjs`
   - 或项目级:`.opencode/plugins/…`,布局相同
2. 重启 OpenCode(插件在启动时加载)。

需要 **OpenCode V2**:插件是 `Plugin.define({ id, setup })` 形态,通过
`ctx.event.subscribe` 订阅、从 `event.data` 读取会话事件。放在
`~/.config/opencode/plugins/` 会自动发现,无需在 `opencode.json` 里登记。

> 插件会先把 `lib/passport-bridge-state.mjs` 拷到**唯一临时路径再导入**,因此
> 即使 OpenCode 服务进程按路径缓存 ESM,插件热重载也能拿到最新的库。若启动日志
> 显示 `eventIntent=MISSING`,说明加载到的库是旧的,重启后台服务
> (`opencode service restart`)即可。设备 Wi-Fi 省电唤醒时首次请求可能要几秒,
> 故发送超时设为 5s。

本插件与 **ZCode 桥**(`tools/zcode/`)共享聚合状态:两边都写
`~/.passport-bridge-state.json` 并推送合并快照,OpenCode 与 ZCode 的会话在
设备上同屏显示。

## 配置

环境变量(启动 OpenCode 前设置):

| 变量                | 默认值                        | 含义 |
| ------------------- | ----------------------------- | ---- |
| `PASSPORT_URL`      | 未设置                        | 完整端点 URL(最高优先) |
| `PASSPORT_HOST`     | 未设置                        | 仅主机名,如 `folopassport.local` |
| `PASSPORT_FALLBACK` | `http://192.168.0.109/notify` | 兜底 URL |
| `PASSPORT_MDNS`     | 未设置                        | `0` 关闭 mDNS 发现 |
| `PASSPORT_OFF`      | 未设置                        | `1` 关闭转发 |
| `PASSPORT_VOICE_PORT` | `8090`                      | 对讲机语音接收端口(设备端 `app_voice.c` 的 `VOICE_PORT` 需一致) |
| `PASSPORT_ASR_MODEL`  | `asr-1.0`                   | MiniMax ASR 模型(接口仅支持 `asr-1.0`) |
| `PASSPORT_ASR_URL`    | `https://api.minimaxi.com/v1/speech_to_text` | ASR 端点 |
| `PASSPORT_ASR_LANG`   | `zh`                        | 语言提示 |
| `PASSPORT_VOICE_TOKEN`| 未设置                      | 设置后语音请求必须带 `X-Passport-Token`(设备配网页填同值) |

### 对讲机(语音 Prompt)

设备端：通知/桌宠页 `OK` 短按开始录音、再短按结束 → 电脑端 ASR → 设备显示识别文本并等确认
（`OK` 发送 / `上` 继续说 / `下` 撤销 / 双击 `OK` 重录）。协议：`POST /api/voice-prompt`
（只识别，返回 `{text}`）、`/api/voice-commit`（注入）、`/api/voice-cancel`（丢弃）。

安全：语音服务只接受**来自设备 IP** 的请求（来源白名单）；如需更强防护，PC 端设置
`PASSPORT_VOICE_TOKEN` 并在设备配网页填入同一 Token。设备端音频为裸 PCM，插件会做去直流 +
峰值归一化并套 WAV 容器后提交。

地址解析顺序:`PASSPORT_URL` → `PASSPORT_HOST` → mDNS(`_folopassport._tcp`) →
`PASSPORT_FALLBACK`。想零配置自动发现,安装一次可选依赖:

```bash
npm i -g bonjour-service
```

未安装时请设置 `PASSPORT_URL`(或 `PASSPORT_HOST`)为设备局域网地址。
设备开机时会在串口打印 IP;在路由里做 DHCP 静态保留可让 IP 稳定。

## 设备端

固件 P1 起,设备**开机即拉起 Wi-Fi 与 `POST /notify` 并后台常驻**,因此任意页面
都能收到通知——打开**通知**页只是为了*查看*最新一条。页面上的状态行会显示服务
是否在线。设备同时通过 mDNS 广播 `folopassport.local`。

设备接受的负载:

```json
{
  "kind": "running",
  "count": 2,
  "title": "任务进行中",
  "text": "",
  "time": 1731234567890,
  "sessions": [
    { "name": "FoloToy-AI-Passport-full.bin build", "kind": "running" },
    { "name": "Hello Kitty风格角色多表情图", "kind": "done" }
  ]
}
```

`kind` 取 `idle` / `running` / `done` / `alert`;缺字段用默认值(`idle`)。
`sessions` 可选(最多 3 条);带 `sessions` 时设备显示列表,否则显示单行
`title`/`text`。非 JSON 正文会当作纯文本显示。

## 不用 OpenCode 也能测

设备可达即可(任意页面):

```bash
curl -X POST http://<设备IP>/notify \
  -H "content-type: application/json" \
  -d '{"kind":"done","title":"测试","text":"hello"}'
```

页面应显示该消息并点亮屏幕。
