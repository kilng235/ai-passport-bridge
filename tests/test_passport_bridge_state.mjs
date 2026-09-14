// tests/test_passport_bridge_state.mjs —— OpenCode/ZCode 共享聚合库的宿主测试。
// 纯 Node(>=18),无第三方依赖,不碰真实状态文件。跑法:
//   node tests/test_passport_bridge_state.mjs
// 由 tools/validate.sh --static 调用。
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

import {
  clipName, byteLength, mergeSnapshot, sourceSessions, loadState, saveState,
  eventIntent, RANK, DONE_DECAY_MS, MAX_SESSIONS, NAME_MAX_BYTES,
} from "../tools/lib/passport-bridge-state.mjs";

let failures = 0;
function check(cond, msg) {
  if (!cond) {
    failures++;
    console.error(`FAIL: ${msg}`);
  }
}

// ---------------------------------------------------------------- 与固件的契约
// 设备端 copy_value 写入 char name[APP_NOTIFY_NAME_MAX + 1],即最多
// APP_NOTIFY_NAME_MAX 字节 + '\0'。lib 的输出上限不得超过它。
const header = fs.readFileSync(
    new URL("../main/app_notify_logic.h", import.meta.url), "utf8");
const m = header.match(/#define\s+APP_NOTIFY_NAME_MAX\s+(\d+)/);
check(m !== null, "main/app_notify_logic.h: APP_NOTIFY_NAME_MAX not found");
const FW_NAME_MAX = m ? Number(m[1]) : NaN;
check(NAME_MAX_BYTES <= FW_NAME_MAX,
      `lib NAME_MAX_BYTES=${NAME_MAX_BYTES} > firmware APP_NOTIFY_NAME_MAX=${FW_NAME_MAX}`);
check(MAX_SESSIONS === 4, `lib MAX_SESSIONS=${MAX_SESSIONS}, expected firmware APP_NOTIFY_SESS_MAX=4`);

// 合法 UTF-8:重新编码后应与原串一致(能发现孤立代理项/被截断的多字节序列)。
function isValidUtf8(s) {
  return Buffer.from(s, "utf8").toString("utf8") === s;
}

// ---------------------------------------------------------------- clipName 边界
const samples = [
  "", "   ", "a", "hello world",
  "x".repeat(Math.max(0, FW_NAME_MAX - 1)),
  "x".repeat(FW_NAME_MAX),
  "x".repeat(FW_NAME_MAX + 1),
  "x".repeat(200),
  "测".repeat(Math.floor(FW_NAME_MAX / 3)),       // 45B,刚好放得下
  "测".repeat(Math.floor(FW_NAME_MAX / 3) + 1),   // 48B,必须裁
  "测".repeat(60),
  "Hello Hello Kitty风格角色多表情图",
  "\u{1F680}".repeat(20),
  "a测\u{1F680}".repeat(30),
  "会话 " + "x".repeat(100),
];
for (const s of samples) {
  const out = clipName(s);
  const bytes = byteLength(out);
  const label = JSON.stringify(s.length > 24 ? `${s.slice(0, 24)}…` : s);
  check(bytes <= FW_NAME_MAX, `clipName(${label}) → ${bytes}B > firmware ${FW_NAME_MAX}B`);
  check(isValidUtf8(out), `clipName(${label}) → invalid UTF-8`);
  check(!out.includes("\uFFFD"), `clipName(${label}) → contains U+FFFD replacement char`);
  // 未超上限的输入必须原样返回(只是 trim);超上限的必须带省略号。
  const trimmed = s.trim();
  if (byteLength(trimmed) <= NAME_MAX_BYTES) {
    check(out === trimmed, `clipName(${label}) should pass through unchanged`);
  } else {
    check(out.endsWith("…"), `clipName(${label}) should end with an ellipsis`);
    check(bytes > 0 && bytes <= NAME_MAX_BYTES, `clipName(${label}) clipped size out of range`);
  }
}

// 回归用例:旧的 45 字节预算 + 省略号会输出 48B,超过固件 47B。
const regression = clipName("测".repeat(16));
check(byteLength(regression) <= FW_NAME_MAX,
      `regression: clipped CJK name is ${byteLength(regression)}B, firmware allows ${FW_NAME_MAX}B`);

// ---------------------------------------------------------------- mergeSnapshot
const now = Date.now();
const agg = mergeSnapshot({
  sources: {
    opencode: { sessions: { oc1: { kind: "running", name: "fix wifi", at: now } } },
    zcode: { sessions: { zc1: { kind: "alert", name: "rm -rf", at: now } } },
  },
}, now);
check(agg.kind === "alert", `aggregate kind expected alert, got ${agg.kind}`);
check(agg.count === 2, `aggregate count expected 2, got ${agg.count}`);
check(agg.sessions?.length === 2, `aggregate sessions expected 2, got ${agg.sessions?.length}`);
check(agg.sessions[0].kind === "alert", "alert session must rank first");
check(agg.title === "需要确认", `aggregate title expected 需要确认, got ${agg.title}`);

const decayed = mergeSnapshot({
  sources: {
    opencode: { sessions: { oc1: { kind: "running", name: "fix wifi", at: now } } },
    zcode: { sessions: { zc1: { kind: "done", name: "rm -rf", at: now - DONE_DECAY_MS - 1 } } },
  },
}, now);
check(decayed.kind === "running", `stale done must decay to idle, got ${decayed.kind}`);
check(decayed.count === 1, `decayed count expected 1, got ${decayed.count}`);

const many = {};
for (let i = 0; i < 10; i++) many[`s${i}`] = { kind: "running", name: `n${i}`, at: now };
const capped = mergeSnapshot({ sources: { x: { sessions: many } } }, now);
check(capped.sessions.length === MAX_SESSIONS, `session list capped at ${MAX_SESSIONS}`);
check(capped.sessions.every((s) => byteLength(s.name) <= FW_NAME_MAX), "merged names exceed firmware limit");

check(RANK.alert === 3 && RANK.running === 2 && RANK.done === 1 && RANK.idle === 0,
      "RANK must match firmware app_notify_kind_rank");

// ---------------------------------------------------------------- eventIntent
// V2 事件负载在 data(V1 在 properties)。映射必须两者都认,否则插件会静默失效
// (设备收不到推送且不报错)—— 这正是线上遇到的故障。
const v2 = (type, data) => ({ type, data });
const v1 = (type, properties) => ({ type, properties });

check(eventIntent(v2("session.status", { sessionID: "s1", status: { type: "busy" } })).kind === "running",
      "V2 session.status busy -> running");
check(eventIntent(v2("session.status", { sessionID: "s1", status: { type: "retry" } })).kind === "running",
      "V2 session.status retry -> running");
check(eventIntent(v2("session.status", { sessionID: "s1", status: { type: "idle" } })).kind === "done",
      "V2 session.status idle -> done");
check(eventIntent(v2("session.idle", { sessionID: "s1" })).kind === "done", "V2 session.idle -> done");
check(eventIntent(v2("permission.asked", { sessionID: "s1" })).kind === "alert", "V2 permission.asked -> alert");
check(eventIntent(v2("permission.replied", { sessionID: "s1" })).kind === "running", "V2 permission.replied -> running");
check(eventIntent(v2("session.error", { sessionID: "s1" })).kind === "alert", "V2 session.error -> alert");
check(eventIntent(v2("question.asked", { sessionID: "s1" })).kind === "alert", "V2 question.asked -> alert");
check(eventIntent(v2("question.replied", { sessionID: "s1" })).kind === "running", "V2 question.replied -> running");

// 运行时还会发 .v2 变体(question.v2.asked 等):必须归一化后同样识别。
check(eventIntent(v2("question.v2.asked", { sessionID: "s1" })).kind === "alert",
      "question.v2.asked -> alert");
check(eventIntent(v2("permission.v2.asked", { sessionID: "s1" })).kind === "alert",
      "permission.v2.asked -> alert");
check(eventIntent(v2("question.v2.replied", { sessionID: "s1" })).kind === "running",
      "question.v2.replied -> running");
check(eventIntent(v2("question.v2.rejected", { sessionID: "s1" })).kind === "running",
      "question.v2.rejected -> running");
check(eventIntent(v2("permission.v2.replied", { sessionID: "s1" })).kind === "running",
      "permission.v2.replied -> running");

// 2.0.x 的确认框是 form 事件;form.created 的 sessionID 嵌在 data.form 里。
check(eventIntent(v2("form.created", { form: { id: "frm_1", sessionID: "s3", title: "Questions" } })).kind === "alert",
      "form.created -> alert");
check(eventIntent(v2("form.created", { form: { id: "frm_1", sessionID: "s3" } })).sid === "s3",
      "form.created resolves sessionID from data.form.sessionID");
check(eventIntent(v2("form.replied", { id: "frm_1", sessionID: "s3", answer: {} })).kind === "running",
      "form.replied -> running");
check(eventIntent(v2("form.rejected", { sessionID: "s3" })).kind === "running",
      "form.rejected -> running");

const created = eventIntent(v2("session.created", { sessionID: "s1", info: { id: "s1", title: "Fix bug" } }));
check(created.sid === "s1" && created.name === "Fix bug" && created.kind === null,
      "V2 session.created records the title");
check(eventIntent(v2("session.deleted", { sessionID: "s1" })).removed === true, "V2 session.deleted -> removed");
check(eventIntent(v2("message.updated", { sessionID: "s1" })) === null, "unrelated event -> null");

// OpenCode 2.0.x 把"正在干活"表达为短名活动事件;SDK 规范名是 session.next.*。
// 两套都要认,否则设备又收不到 running。
check(eventIntent(v2("session.step.started", { sessionID: "s1" })).kind === "running",
      "session.step.started -> running");
check(eventIntent(v2("session.next.step.started", { sessionID: "s1" })).kind === "running",
      "session.next.step.started -> running (normalized)");
check(eventIntent(v2("session.tool.progress", { sessionID: "s1" })).kind === "running",
      "session.tool.progress -> running (keeps long tools fresh)");
check(eventIntent(v2("session.next.tool.called", { sessionID: "s1" })).kind === "running",
      "session.next.tool.called -> running");
check(eventIntent(v2("session.execution.succeeded", { sessionID: "s1" })).kind === "done",
      "session.execution.succeeded -> done");
check(eventIntent(v2("session.execution.failed", { sessionID: "s1" })).kind === "alert",
      "session.execution.failed -> alert");
check(eventIntent(v2("session.step.ended", { sessionID: "s1", finish: "tool-calls" })).kind === null,
      "mid-turn step.ended -> no state change");
check(eventIntent(v2("session.step.ended", { sessionID: "s1", finish: "stop" })).kind === "done",
      "final step.ended -> done");

// 旧 V1 形状(properties)必须行为一致。
check(eventIntent(v1("session.status", { sessionID: "s2", status: { type: "busy" } })).kind === "running",
      "V1 session.status busy -> running");
check(eventIntent(v1("session.idle", { sessionID: "s2" })).kind === "done", "V1 session.idle -> done");
check(eventIntent(v1("session.updated", { sessionID: "s2", info: { id: "s2", title: "Legacy" } })).name === "Legacy",
      "V1 session.updated records the title");

// ---------------------------------------------------------------- 状态文件往返
const tmp = path.join(os.tmpdir(), `passport-bridge-test-${process.pid}.json`);
const previousState = process.env.PASSPORT_STATE;
process.env.PASSPORT_STATE = tmp;
try {
  const empty = loadState();
  const own = sourceSessions(empty, "opencode");
  own["s1"] = { kind: "running", name: "task", at: now };
  empty._cooldownUntil = now + 60_000;
  saveState(empty);

  const reloaded = loadState();
  check(reloaded.sources.opencode.sessions.s1.kind === "running", "state round-trip lost the session");
  check(reloaded.sources.opencode.sessions.s1.name === "task", "state round-trip lost the name");
  check(reloaded._cooldownUntil === now + 60_000, "state round-trip lost the cooldown stamp");

  // ---------------------------------------------------------------- 命名空间安全写
  // 模拟两个并发写者:writer1 只写 opencode 源,writer2 只写 zcode 源。
  // writer1 写时,不应抹掉 writer2 在磁盘上新加的 zcode 会话。
  const state1 = loadState();
  const state2 = loadState();
  sourceSessions(state1, "opencode")["s_oc_new"] = { kind: "running", name: "oc task", at: now };
  sourceSessions(state2, "zcode")["s_zc_new"] = { kind: "running", name: "zc task", at: now };

  // state2 先写盘
  saveState(state2, "zcode");
  // state1(读的时候还没有 state2 的内容)后写盘,必须自动合入 state2 的 zcode 源
  saveState(state1, "opencode");

  const mergedRoundTrip = loadState();
  check(mergedRoundTrip.sources.opencode.sessions.s_oc_new?.kind === "running",
        "merge-on-save: own source session missing");
  check(mergedRoundTrip.sources.zcode.sessions.s_zc_new?.kind === "running",
        "merge-on-save: concurrent other source session was overwritten!");
} finally {
  if (previousState === undefined) delete process.env.PASSPORT_STATE;
  else process.env.PASSPORT_STATE = previousState;
  fs.rmSync(tmp, { force: true });
}

if (failures > 0) {
  console.error(`\nBridge host tests: FAIL (${failures} check(s))`);
  process.exit(1);
}
console.log("Bridge host tests: PASS");
