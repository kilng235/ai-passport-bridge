// tools/lib/passport-bridge-state.mjs —— OpenCode 与 ZCode 两个推送桥的共享聚合状态。
// 两个桥读写同一份 ~/.passport-bridge-state.json(可用 PASSPORT_STATE 覆盖),
// 各自事件到达时:读文件 → 更新自己源的会话 → 全源合并 → 推合并快照 → 写回。
// done 超过 DONE_DECAY_MS 的会话在合并时惰性衰减为 idle(不改文件)。
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

export const RANK = { alert: 3, running: 2, done: 1, idle: 0 };
export const DONE_DECAY_MS = 5000;
export const MAX_SESSIONS = 4;      // 与固件 APP_NOTIFY_SESS_MAX 对齐(4 行跑马灯列表)
// 与固件 app_notify_logic.h 的 APP_NOTIFY_NAME_MAX 对齐:放宽至 95 字节(约 31 个汉字),
// 设备端长文本通过 LV_LABEL_LONG_SCROLL_CIRCULAR 跑马灯滚动完整呈现。
export const NAME_MAX_BYTES = 95;

const ELLIPSIS = "…";   // U+2026,UTF-8 占 3 字节(设备字体已含该字形)

export function byteLength(s) {
  try { return Buffer.byteLength(s, "utf8"); } catch { return s.length; }
}

// 按 UTF-8 字符边界截断,超出补省略号;输出总字节数保证 <= NAME_MAX_BYTES。
export function clipName(name) {
  const s = name == null ? "" : String(name).trim();
  if (byteLength(s) <= NAME_MAX_BYTES) return s;
  const budget = NAME_MAX_BYTES - byteLength(ELLIPSIS);   // 预留省略号自身的字节
  let out = "";
  for (const ch of s) {
    if (byteLength(out + ch) > budget) break;
    out += ch;
  }
  return `${out}${ELLIPSIS}`;
}

export function label(kind) {
  switch (kind) {
    case "running": return "任务进行中";
    case "done":    return "任务完成";
    case "alert":   return "需要确认";
    default:        return "空闲";
  }
}

function asStr(value) {
  if (value === null || value === undefined) return "";
  return typeof value === "string" ? value : String(value);
}

// 归一化事件名:SDK 用 session.next.*,运行时还会发 .v2.*(question.v2.asked 等)
// 以及去掉前缀的短名(session.step.started)。统一成短名再匹配。
function normalizeEventType(type) {
  return asStr(type).replace(/\.(v2|next)\./g, ".");
}

// OpenCode 把"正在干活"表达为高频的 session 活动事件,而不是 session.status。
// 不同版本命名还不一致(SDK 写 session.next.step.started,2.0.x 运行时实际发
// session.step.started),故先归一化再匹配,避免版本漂移再次静默失效。
const ACTIVITY_TYPES = new Set([
  "session.step.started",
  "session.tool.called",
  "session.tool.progress",     // 长工具(构建/安装)期间靠它保鲜
  "session.tool.success",
  "session.tool.failed",
  "session.text.started",
  "session.reasoning.started",
  "session.prompt.admitted",
  "session.prompted",
  "session.compacted",
  "session.shell.started",
  "session.synthetic",
]);

// 把 OpenCode 事件映射成"本桥该做什么"。V2 事件负载在 event.data,V1 在
// event.properties —— 两者都支持,避免字段漂移导致静默失效(曾经如此)。
// 返回 null = 与本桥无关;否则 { sid, name, kind, removed }:
//   name    created/updated 的会话标题(其余为空串)
//   kind    "running" | "alert" | "done" | null(不改状态)
//   removed true = 删除该会话
export function eventIntent(event) {
  const p = event?.data ?? event?.properties ?? {};
  const info = p.info ?? p.session;
  const sid = asStr(info?.id ?? p.sessionID ?? p.form?.sessionID);
  switch (normalizeEventType(event?.type)) {
    case "session.created":
    case "session.updated":
      return { sid, name: asStr(info?.title), kind: null, removed: false };

    case "session.status": {
      const st = p.status?.type;   // "idle" | "busy" | "retry"
      const kind = (st === "busy" || st === "retry") ? "running"
                 : (st === "idle" ? "done" : null);
      return { sid, name: "", kind, removed: false };
    }
    case "session.idle":
      return { sid, name: "", kind: "done", removed: false };

    // OpenCode 2.0.x 用"整轮执行结束"代替 session.idle。
    case "session.execution.succeeded":
      return { sid, name: "", kind: "done", removed: false };
    case "session.execution.failed":
      return { sid, name: "", kind: "alert", removed: false };

    // 末步(finish=stop)才是回合结束;中间步(finish=tool-calls)不算。
    case "session.step.ended": {
      const stop = p.finish === "stop" || p.rawFinish === "stop";
      return { sid, name: "", kind: stop ? "done" : null, removed: false };
    }

    case "permission.asked":
    case "question.asked":
    case "session.error":
      return { sid, name: "", kind: "alert", removed: false };
    case "permission.replied":
    case "question.replied":
    case "question.rejected":
      return { sid, name: "", kind: "running", removed: false };

    // OpenCode 2.0.x 的确认框(question/permission 工具)以表单事件表达:
    //   form.created { form: { sessionID, ... } }  -> 需要确认
    //   form.replied { sessionID, answer }         -> 继续
    case "form.created":
      return { sid, name: "", kind: "alert", removed: false };
    case "form.replied":
    case "form.rejected":
      return { sid, name: "", kind: "running", removed: false };

    case "session.deleted":
      return { sid, name: "", kind: null, removed: true };

    default:
      if (sid && ACTIVITY_TYPES.has(normalizeEventType(event?.type))) {
        return { sid, name: "", kind: "running", removed: false };
      }
      return null;
  }
}

export function statePath() {
  return process.env.PASSPORT_STATE ||
      path.join(os.homedir(), ".passport-bridge-state.json");
}

// 读取共享状态;缺失/损坏时返回空表(下个事件自动重建)。
export function loadState() {
  try {
    const parsed = JSON.parse(fs.readFileSync(statePath(), "utf8"));
    if (parsed && typeof parsed === "object" && parsed.sources &&
        typeof parsed.sources === "object") {
      return parsed;
    }
  } catch { /* missing or corrupt */ }
  return { sources: {} };
}

// 原子合并写:若指定了 source(如 "opencode" | "zcode"),落盘前重新读取磁盘最新状态,
// 保留磁盘上其他源的最新状态,仅合入自己的源,防止双桥并发时互相覆盖。
export function saveState(state, mySource = null) {
  const file = statePath();
  const tmp = `${file}.${process.pid}.${Date.now()}.${Math.random().toString(36).slice(2)}.tmp`;
  fs.mkdirSync(path.dirname(file), { recursive: true });

  let finalState = state;
  if (mySource && typeof mySource === "string") {
    const onDisk = loadState();
    finalState = {
      ...onDisk,
      ...state,
      sources: {
        ...(onDisk.sources ?? {}),
        [mySource]: state.sources?.[mySource] ?? { sessions: {} },
      },
    };
    if (state && typeof state === "object") {
      state.sources = finalState.sources;
    }
  }

  fs.writeFileSync(tmp, JSON.stringify(finalState));
  try {
    fs.renameSync(tmp, file);
  } catch {
    // Windows 上跨进程原子重命名偶尔遇上防病毒/读锁时重试
    try { fs.rmSync(file, { force: true }); fs.renameSync(tmp, file); } catch { /* ignore */ }
  }
}

// 取/建某个源(opencode|zcode)的会话表。返回的是文件内对象的引用。
export function sourceSessions(state, source) {
  if (!state.sources[source] || typeof state.sources[source] !== "object") {
    state.sources[source] = { sessions: {} };
  }
  const src = state.sources[source];
  if (!src.sessions || typeof src.sessions !== "object") src.sessions = {};
  return src.sessions;
}

// done 超过衰减窗的会话按 idle 参与聚合;未知 kind 按 idle。
function effectiveKind(entry, now) {
  const kind = RANK[entry?.kind] != null ? entry.kind : "idle";
  if (kind === "done" && now - (entry.at || 0) > DONE_DECAY_MS) return "idle";
  return kind;
}

// 把全源会话合并成设备 /notify 载荷。纯计算,不落盘、不发请求。
export function mergeSnapshot(state, now = Date.now()) {
  let agg = "idle";
  let active = 0;
  const list = [];
  for (const sessions of Object.values(state.sources ?? {})) {
    for (const entry of Object.values(sessions?.sessions ?? {})) {
      const kind = effectiveKind(entry, now);
      if (kind !== "idle") active++;
      if (RANK[kind] > RANK[agg]) agg = kind;
      if (kind !== "idle") {
        list.push({ name: clipName(entry.name), kind, at: entry.at || 0 });
      }
    }
  }
  list.sort((a, b) => (RANK[b.kind] - RANK[a.kind]) || (b.at - a.at));
  return {
    kind: agg,
    count: active,
    title: label(agg),
    text: "",
    time: Date.now(),
    ...(list.length
        ? { sessions: list.slice(0, MAX_SESSIONS)
              .map(({ name, kind }) => ({ name, kind })) }
        : {}),
  };
}
