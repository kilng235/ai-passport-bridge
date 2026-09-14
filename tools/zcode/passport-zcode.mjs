#!/usr/bin/env node
// passport-zcode.mjs —— ZCode 钩子 → FoloToy AI Passport 转发(局域网 HTTP)。
//
// 由 ZCode 的 process 型钩子调用(见 tools/zcode/README.md):
//   node passport-zcode.mjs <kind> [session_id] [project_dir]
//     kind: running | done | alert | idle(非法值按 running 处理)
//     idle = 会话打开但空闲:只记账,设备不显示(空闲会话本就不进列表)
//   内部衰减模式(由本脚本 detached 拉起,勿手工调用):
//     node passport-zcode.mjs __decay <session_id> [project_dir]
//
// 会话名显示为 "ZCode <项目目录名>"。只转发状态,不含提示词/代码/消息正文。
// 与 OpenCode 桥(tools/opencode/)共享 ~/.passport-bridge-state.json:
// 本脚本更新 zcode 源后,推送"全源合并"的快照 —— 两个 agent 的状态同屏。
//
// 钩子是内联执行的:设备连不上会拖慢 agent,所以 fetch 超时 1s、失败后进
// 60s 冷却(记在共享状态文件里),期间直接跳过发送。永远 exit 0、无 stdout
// (ZCode 钩子的 stdout 按严格 JSON 校验,空输出即合法)。
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

import { loadState, saveState, mergeSnapshot, sourceSessions,
         DONE_DECAY_MS } from "../lib/passport-bridge-state.mjs";

const FALLBACK_URL = process.env.PASSPORT_FALLBACK || "http://192.168.0.109/notify";
const SEND_TIMEOUT_MS = 1000;
const COOLDOWN_MS = 60_000;
const DONE_DECAY_PUSH_MS = DONE_DECAY_MS + 300;

const KINDS = { running: "running", done: "done", alert: "alert", idle: "idle" };

// 地址解析(无 mDNS):PASSPORT_URL > PASSPORT_HOST > PASSPORT_FALLBACK。
function deviceUrl() {
  if (process.env.PASSPORT_URL) return process.env.PASSPORT_URL;
  if (process.env.PASSPORT_HOST) return `http://${process.env.PASSPORT_HOST}/notify`;
  return FALLBACK_URL;
}

function sessionName(projectDir) {
  const base = projectDir ? path.basename(projectDir) : "";
  const name = `ZCode ${base}`.trim();
  return name || "ZCode";
}

async function send(payload) {
  try {
    const res = await fetch(deviceUrl(), {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(payload),
      signal: AbortSignal.timeout(SEND_TIMEOUT_MS),
    });
    return res.ok;
  } catch {
    return false;   // 设备离线:由冷却机制兜底,绝不让钩子失败
  }
}

// 更新 zcode 源会话 → 推合并快照 → 落盘。返回 true 表示已送达。
// 两条减负规则:
//   1) running 重复推送照发 —— 它是保鲜刷新(设备端对同 kind 静默处理,
//      不响铃不重绘),PreToolUse 不断刷新才能让"进行中"有据可依;
//   2) idle 只记账不推送 —— 设备不显示空闲会话,推了反而多一次亮屏/覆盖。
async function updateAndPush(kind, sid, projectDir) {
  const state = loadState();
  const sessions = sourceSessions(state, "zcode");
  sessions[sid] = { kind, name: sessionName(projectDir), at: Date.now() };
  const payload = mergeSnapshot(state);

  if (kind === "idle") {
    saveState(state, "zcode");
    return false;
  }

  const now = Date.now();
  if (state._cooldownUntil && now < state._cooldownUntil) return false;
  const ok = await send(payload);
  state._cooldownUntil = ok ? 0 : now + COOLDOWN_MS;
  saveState(state, "zcode");
  return ok;
}

// done 的显示衰减:detached 子进程等设备端覆盖窗过后,把回话降回 idle 再推一次。
// (OpenCode 桥用内存定时器做同样的事;钩子是一次性进程,只能 detached 自启。)
function spawnDecayer(selfPath, sid, projectDir) {
  const child = spawn(process.execPath,
      [selfPath, "__decay", sid, projectDir ?? ""],
      { detached: true, stdio: "ignore" });
  child.unref();
}

async function main() {
  const [kindArg, sessionId, projectDir] = process.argv.slice(2);

  if (kindArg === "__decay") {
    // 衰减模式:等设备端覆盖窗结束,若会话仍是 done 则降为 idle 并推送。
    setTimeout(async () => {
      const state = loadState();
      const sid = sessionId || "default";
      const entry = sourceSessions(state, "zcode")[sid];
      if (entry?.kind === "done" && Date.now() - (entry.at || 0) >= DONE_DECAY_MS - 100) {
        await updateAndPush("idle", sid, projectDir);
      }
    }, DONE_DECAY_PUSH_MS);
    return;
  }

  const kind = KINDS[kindArg] ?? "running";
  const sid = sessionId || "default";
  const ok = await updateAndPush(kind, sid, projectDir);
  if (kind === "done" && ok) {
    spawnDecayer(fileURLToPath(import.meta.url), sid, projectDir);
  }
}

main().catch(() => { /* never fail the hook */ });
