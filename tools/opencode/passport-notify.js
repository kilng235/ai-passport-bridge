// passport-notify.js —— OpenCode V2 插件:把会话状态转发到 FoloToy AI Passport。
//
// V2 插件必须是 define({ id, setup }) 形状:通过 ctx.event.subscribe 订阅服务端
// 事件流,通过 ctx.session.get 拉取会话标题,再合并 shared lib 里的会话快照
// (tools/lib/passport-bridge-state.mjs)POST 到设备 /notify。
// 事件负载字段由 lib.eventIntent 统一处理(V2 在 event.data,V1 在
// event.properties),避免字段漂移导致静默失效。
// 只转发状态与会话名,不含提示词/代码/消息正文。
//
// 设备在局域网常驻监听:POST http://<设备IP>/notify
//   body: {
//     "kind":"idle|running|done|alert",   // 聚合态(驱动整体色/提示音)
//     "count": 2,                          // 活跃会话数
//     "title":"任务进行中","text":"","time":<epoch 毫秒>,
//     "sessions":[ {"name":"<会话名>","kind":"running"}, ... ]   // 最多 3 条,不含空闲
//   }
//
// 聚合状态存在共享文件里,与 ZCode 桥(tools/zcode/)合并:任一桥收到事件都会推送
// "全部源"的聚合快照。
//
// 设备地址解析优先级:
//   1) PASSPORT_URL      完整 URL,如 http://192.168.0.109/notify(最高优先)
//   2) PASSPORT_HOST     仅主机名,如 folopassport.local 或 192.168.0.109
//   3) mDNS 自动发现     _folopassport._tcp(bonjour-service),或退回解析
//                        设备广播的主机名 PASSPORT_MDNS_NAME(默认 folopassport.local)
//   4) PASSPORT_FALLBACK 兜底 URL(默认 http://192.168.0.109/notify)
//
// 环境变量:
//   PASSPORT_URL / PASSPORT_HOST / PASSPORT_FALLBACK / PASSPORT_STATE
//   PASSPORT_MDNS_NAME  设备 mDNS 主机名(默认 folopassport.local)
//   PASSPORT_OFF=1   完全关闭转发
//   PASSPORT_MDNS=0  关闭 mDNS 自动发现
//
// 状态映射(聚合优先级 确认 > 进行中 > 完成 > 空闲):
//   session.status busy|retry                           -> running(该会话进行中)
//   session.idle                                        -> done(5s 后衰减为 idle)
//   permission.asked / question.asked / session.error   -> alert(需要确认)
//   permission.replied / question.replied               -> running(确认后继续)
//   session.created/updated                             -> 记录会话标题(用于显示名)
//   session.deleted                                     -> 移除
import { Plugin } from "@opencode/plugin";
import { appendFileSync, copyFileSync, statSync, unlinkSync } from "node:fs";
import { lookup as dnsLookup } from "node:dns/promises";
import os from "node:os";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";

const SOURCE = "opencode";

const STATIC_URL = process.env.PASSPORT_URL || null;
const STATIC_HOST = process.env.PASSPORT_HOST || null;
const FALLBACK_URL = process.env.PASSPORT_FALLBACK || "http://192.168.0.109/notify";
const MDNS_DISABLED = process.env.PASSPORT_MDNS === "0";
const MDNS_NAME = process.env.PASSPORT_MDNS_NAME || "folopassport.local";
const DISABLED = process.env.PASSPORT_OFF === "1";
const LOG_FILE = process.env.PASSPORT_LOG_FILE
  || path.join(os.homedir(), ".config", "opencode", "plugins", "passport-notify.log");
const TIMEOUT_MS = 5000;   // 设备 Wi-Fi 省电时首个请求可能 >2s,留足唤醒时间
const SEND_DEBOUNCE_MS = 250;
const MDNS_TIMEOUT_MS = 1500;
// 对讲机语音接收端口。8080 常被其它软件占用(如 NVIDIA Broadcast),故换用 8090;
// 可用 PASSPORT_VOICE_PORT 覆盖(Firmware 端 app_voice.c 的 VOICE_PORT 需一致)。
const VOICE_PORT = Number(process.env.PASSPORT_VOICE_PORT || 8090);
// 语音服务的路由版本:改动端点/协议时必须 +1,以便热重载时丢弃旧服务器重建。
const VOICE_SERVER_VERSION = 6;
// 可选的共享密钥:PC 端设置 PASSPORT_VOICE_TOKEN 后,语音请求必须携带同名 Token
// 头部(X-Passport-Token 或 ?token=),否则拒绝。设备端在配网页填入同一 Token。
const VOICE_TOKEN = process.env.PASSPORT_VOICE_TOKEN || "";
// ASR(语音转写):MiniMax 需显式传 model(默认 asr-1.0);端点/模型均可用环境变量覆盖。
const ASR_URL = process.env.PASSPORT_ASR_URL
  || "https://api.minimaxi.com/v1/speech_to_text";
const ASR_MODEL = process.env.PASSPORT_ASR_MODEL || "asr-1.0";
const ASR_LANG = process.env.PASSPORT_ASR_LANG || "zh";
// 设备上传的是裸 PCM(16kHz/16bit/单声道);MiniMax 只吃带容器的音频(如 WAV),
// 故这里补一个 44 字节 WAV 头再上传。
const ASR_PCM_RATE = 16000;

// 共享库定位:仓库布局(tools/opencode/../lib)与安装布局(plugins/lib)都支持。
// 静态 import 在安装布局下会解析失败导致整个插件加载不出来,必须动态加载。
const PLUGIN_DIR = path.dirname(fileURLToPath(import.meta.url));
const LIB_CANDIDATES = [
  path.join(PLUGIN_DIR, "lib", "passport-bridge-state.mjs"),      // 安装布局
  path.join(PLUGIN_DIR, "..", "lib", "passport-bridge-state.mjs"), // 仓库布局
];

// 返回 { mod, url, errors }。OpenCode 服务进程(插件加载器)会按路径缓存 ESM,
// 直接 import 原路径 —— 即使带 ?v= query —— 也会拿到进程内的旧模块,导致热重载
// 后改了 lib 文件仍不生效。故先把库拷到唯一临时路径再导入,确保每次都读当前内容。
async function loadBridgeLib() {
  const errors = [];
  for (const candidate of LIB_CANDIDATES) {
    let tmp = null;
    try {
      statSync(candidate);   // 不存在则跳到下一个候选
      tmp = path.join(os.tmpdir(),
        `passport-bridge-${process.pid}-${Date.now()}-${Math.random().toString(36).slice(2)}.mjs`);
      copyFileSync(candidate, tmp);
      const mod = await import(pathToFileURL(tmp).href);
      return { mod, url: candidate, errors };
    } catch (err) {
      errors.push(`${candidate}: ${err?.code ?? err?.message ?? err}`);
    } finally {
      if (tmp) { try { unlinkSync(tmp); } catch { /* ignore */ } }
    }
  }
  return { mod: null, url: null, errors };
}

// 显式配置的地址(不依赖 mDNS)。没有则返回 null。
function staticUrl() {
  if (STATIC_URL) return STATIC_URL;
  if (STATIC_HOST) return `http://${STATIC_HOST}/notify`;
  return null;
}

// 给裸 PCM(16bit/单声道)套 44 字节 WAV 头,MiniMax ASR 需要带容器。
function pcmToWav(pcm, rate = ASR_PCM_RATE, channels = 1, bits = 16) {
  const byteRate = rate * channels * (bits / 8);
  const h = Buffer.alloc(44);
  h.write("RIFF", 0);
  h.writeUInt32LE(36 + pcm.length, 4);
  h.write("WAVE", 8);
  h.write("fmt ", 12);
  h.writeUInt32LE(16, 16);
  h.writeUInt16LE(1, 20);              // PCM
  h.writeUInt16LE(channels, 22);
  h.writeUInt32LE(rate, 24);
  h.writeUInt32LE(byteRate, 28);
  h.writeUInt16LE(channels * (bits / 8), 32);
  h.writeUInt16LE(bits, 34);
  h.write("data", 36);
  h.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([h, pcm]);
}

// 去直流 + 峰值归一化(小声显著提升 ASR),并返回统计值用于诊断采集音量:
//   peak 接近 32767  → 削波(应调小设备麦克风增益)
//   rms 很低         → 声音太轻(归一化可救)
function analyzeAndNormalizePcm(pcm) {
  const n = Math.floor(pcm.length / 2);
  if (n === 0) return { data: pcm, stats: "empty" };
  let sum = 0, peakIn = 0;
  for (let i = 0; i < n; i++) {
    const s = pcm.readInt16LE(i * 2);
    sum += s;
    const a = Math.abs(s);
    if (a > peakIn) peakIn = a;
  }
  const dc = Math.round(sum / n);

  const out = Buffer.allocUnsafe(n * 2);
  let peak = 0;
  for (let i = 0; i < n; i++) {
    let s = pcm.readInt16LE(i * 2) - dc;
    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
    out.writeInt16LE(s, i * 2);
    const a = Math.abs(s);
    if (a > peak) peak = a;
  }
  let gain = 1;
  if (peak > 0 && peak < 30000) gain = Math.min(8, 30000 / peak);
  if (gain !== 1) {
    for (let i = 0; i < n; i++) {
      let s = Math.round(out.readInt16LE(i * 2) * gain);
      if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
      out.writeInt16LE(s, i * 2);
    }
  }
  const fs = 32768;
  const stats = `samples=${n} peak=${peakIn}(${(20 * Math.log10((peakIn || 1) / fs)).toFixed(1)}dBFS)`
    + ` dc=${dc} gain=${gain.toFixed(2)}x`;
  return { data: out, stats };
}

// 通过 mDNS 发现设备 _folopassport._tcp;失败/未安装返回 null。
async function discoverViaMdns() {
  if (MDNS_DISABLED || staticUrl()) return null;

  // 1) 可选依赖 bonjour-service:直接查询服务记录,跨平台。
  const viaBonjour = await discoverViaBonjour();
  if (viaBonjour) return viaBonjour;

  // 2) 没装可选依赖时,退回用系统解析器解析设备广播的主机名(默认
  //    folopassport.local)。这里拿到具体 IPv4 再连 —— 让 fetch 自己解析
  //    .local 在 Windows 上会超时,解析成数字地址才可靠。
  try {
    const { address } = await dnsLookup(MDNS_NAME, { family: 4 });
    if (address) return `http://${address}/notify`;
  } catch {
    /* 名称解析失败:交给上层兜底 */
  }
  return null;
}

async function discoverViaBonjour() {
  try {
    const mod = await import("bonjour-service");   // 可选依赖,缺失则抛错被捕获
    const Bonjour = mod.Bonjour ?? mod.default;
    const bonjour = new Bonjour();
    return await new Promise((resolve) => {
      let browser = null;
      let settled = false;
      const finish = (url) => {
        if (settled) return;
        settled = true;
        try { browser?.stop?.(); } catch { /* ignore */ }
        try { bonjour.destroy(); } catch { /* ignore */ }
        resolve(url);
      };
      const timer = setTimeout(() => finish(null), MDNS_TIMEOUT_MS);
      browser = bonjour.find({ type: "folopassport" });
      browser.on("up", (svc) => {
        const addr = (svc.addresses || []).find((a) => a && a.includes("."));
        if (!addr) return;
        clearTimeout(timer);
        finish(`http://${addr}:${svc.port || 80}/notify`);
      });
    });
  } catch {
    return null;
  }
}

export default Plugin.define({
  id: "passport-notify",

  async setup(ctx) {
    // 进程内代际令牌:OpenCode 会按 location 把同一插件实例化多次(本机实测 4 次),
    // 每个实例都订阅同一份服务端事件流 → 每次事件被处理多遍、重复推送。只让最新
    // 实例生效,旧的自动空转(实测这些实例在同一服务进程内,故用 globalThis 即可)。
    const instance = `${process.pid}-${Date.now()}-${Math.random().toString(36).slice(2)}`;
    globalThis.__passportNotifyInstance = instance;
    const isActive = () => globalThis.__passportNotifyInstance === instance;
    let toldInactive = false;

    // 日志:console 为主,PASSPORT_LOG_FILE 存在时再写一份文件(失败绝不抛出)。
    const log = (message) => {
      console.log(`[passport-notify] ${message}`);
      if (LOG_FILE) {
        try {
          appendFileSync(LOG_FILE, `${new Date().toISOString()} [passport-notify] ${message}\n`);
        } catch { /* ignore */ }
      }
      try {
        // V2 的 ctx.app.log 收扁平参数(V1 才用 { body });扁平形状在 V1 下最多丢掉
        // 字段,不会抛错,所以这里统一用 V2 形状。
        const r = ctx.app?.log?.({ service: "passport-notify", level: "info", message });
        if (r && typeof r.catch === "function") r.catch(() => { /* ignore */ });
      } catch { /* ignore */ }
    };

    if (DISABLED) {
      log("disabled via PASSPORT_OFF");
      return;
    }

    const loaded = await loadBridgeLib();
    const lib = loaded.mod;
    if (!lib) {
      log("passport-bridge-state.mjs not found next to the plugin file (tried "
          + `${loaded.errors.join(" | ")})`);
      return;
    }

    let deviceUrl = staticUrl();
    let lastFailLogAt = 0;   // 发送失败日志节流(每 30s 最多记一条)
    let announced = false;
    let lastLoggedKind = null;
    let sendTimer = null;
    const decayTimers = new Map();   // sessionID -> timeout

    const sessionName = async (sid) => {
      const cur = lib.sourceSessions(lib.loadState(), SOURCE)[sid];
      if (cur?.name) return cur.name;
      try {
        const s = await ctx.session?.get?.({ sessionID: sid });
        const title = s?.title ?? s?.data?.title;
        if (title) return String(title);
      } catch {
        /* ignore */
      }
      return sid ? `会话 ${sid.slice(0, 6)}` : "会话";
    };

    // 读共享状态 → 合并全源 → 推送合并快照。
    const push = async () => {
      if (!deviceUrl) deviceUrl = (await discoverViaMdns()) || FALLBACK_URL;
      const payload = lib.mergeSnapshot(lib.loadState());
      try {
        const res = await fetch(deviceUrl, {
          method: "POST",
          headers: { "content-type": "application/json" },
          body: JSON.stringify(payload),
          signal: AbortSignal.timeout(TIMEOUT_MS),
        });
        // 只有真正连上设备后才记录其地址,供语音服务做来源白名单(避免记成兜底地址)。
        try { globalThis.__passportDeviceAddr = new URL(deviceUrl).hostname; } catch { /* ignore */ }
        if (!res.ok) log(`device returned HTTP ${res.status}`);
        else if (!announced) { announced = true; log(`device reachable (${deviceUrl})`); }
        if (payload.kind !== lastLoggedKind) {   // 状态变化才记,便于排查
          lastLoggedKind = payload.kind;
          log(`push kind=${payload.kind} count=${payload.count}`);
        }
      } catch (err) {
        // Device offline / on another network: never block or throw.
        if (!staticUrl()) deviceUrl = null;   // 允许下次重新 mDNS 发现
        const now = Date.now();
        if (now - lastFailLogAt > 30_000) {   // 失败节流,避免设备离线时刷屏
          lastFailLogAt = now;
          log(`send failed (${err?.message ?? err}); is the device online?`);
        }
      }
    };

    const scheduleSend = () => {
      if (!isActive() || sendTimer) return;   // 已被更新的实例接管则不再推送
      sendTimer = setTimeout(() => {
        sendTimer = null;
        push().catch(() => { /* never throw into the session */ });
      }, SEND_DEBOUNCE_MS);
    };

    // 更新共享状态里自己的会话并安排推送。
    const updateOwn = (sid, kind) => {
      if (!sid) return null;
      const state = lib.loadState();
      const sessions = lib.sourceSessions(state, SOURCE);
      const cur = sessions[sid] || { kind: "idle", name: "", at: 0 };
      if (kind) cur.kind = kind;
      cur.at = Date.now();
      sessions[sid] = cur;
      lib.saveState(state, SOURCE);
      scheduleSend();
      return cur;
    };

    const ensureName = (sid) => {
      const cur = lib.sourceSessions(lib.loadState(), SOURCE)[sid];
      if (cur?.name) return;
      sessionName(sid).then((name) => {
        const state = lib.loadState();
        const entry = lib.sourceSessions(state, SOURCE)[sid];
        if (entry && !entry.name) {
          entry.name = name;
          lib.saveState(state, SOURCE);
          if (entry.kind !== "idle") scheduleSend();
        }
      });
    };

    const setKind = (sid, kind) => {
      if (!sid) return;
      updateOwn(sid, kind);
      ensureName(sid);
    };

    const markDone = (sid) => {
      if (!sid) return;
      updateOwn(sid, "done");
      if (decayTimers.has(sid)) clearTimeout(decayTimers.get(sid));
      decayTimers.set(sid, setTimeout(() => {
        decayTimers.delete(sid);
        if (!isActive()) return;
        const state = lib.loadState();
        const sessions = lib.sourceSessions(state, SOURCE);
        const s = sessions[sid];
        if (s && s.kind === "done") {
          s.kind = "idle";
          s.at = Date.now();
          sessions[sid] = s;
          lib.saveState(state, SOURCE);
          scheduleSend();
        }
      }, lib.DONE_DECAY_MS));
    };

    // created/updated:记录会话名及父会话 ID(用于区分子代理会话)。
    const rememberName = (sid, name, parentID = "") => {
      if (!sid) return;
      const state = lib.loadState();
      const sessions = lib.sourceSessions(state, SOURCE);
      const cur = sessions[sid] || { kind: "idle", name: "", at: 0 };
      if (name) cur.name = name;
      if (parentID) cur.parentID = parentID;
      sessions[sid] = cur;
      lib.saveState(state, SOURCE);
      if (cur.kind !== "idle") scheduleSend();
    };

    const removeSession = (sid) => {
      if (!sid) return;
      if (decayTimers.has(sid)) { clearTimeout(decayTimers.get(sid)); decayTimers.delete(sid); }
      const state = lib.loadState();
      delete lib.sourceSessions(state, SOURCE)[sid];
      lib.saveState(state, SOURCE);
      scheduleSend();
    };

    // 纯内存心跳表:每个会话最后一次"收到任何事件"的时刻。只写内存、不落盘,
    // 用于对账时把"活跃但服务端时间戳滞后"与"真的结束"区分开。
    const lastSeen = new Map();   // sessionID -> epoch ms

    // 从任意事件里尽力取 sessionID(与 lib.eventIntent 的字段来源保持一致)。
    const eventSid = (event) => {
      const p = event?.data ?? event?.properties ?? {};
      const info = p.info ?? p.session;
      return info?.id ?? p.sessionID ?? p.form?.sessionID ?? null;
    };

    // 向服务端核对单个会话。注意:time.updated/time.idle 是"回合"级的,一个活跃的
    // 长回合里可能好几分钟不刷新,所以这里只返回原始信息,由对账逻辑结合本地心跳
    // 一起判断,避免把正在干活的会话误判成 idle。
    // 返回 { gone } | { lastTouched, outcome } | null(拿不到,不动状态)。
    async function serverSessionInfo(sid, ctx) {
      if (!sid || !ctx?.session?.get) return null;
      try {
        const res = await ctx.session.get({ sessionID: sid });
        const data = res?.data ?? res;
        if (!data) return null;
        // 404 → 服务端已删,本端也应清理
        const status404 = data?.error?.status === 404
          || data?.notFound === true
          || (typeof res?.status === "function" && res.status === 404);
        if (status404) return { gone: true };
        const t = data?.time ?? {};
        return {
          lastTouched: Math.max(t.updated ?? 0, t.idle ?? 0, t.created ?? 0),
          outcome: data?.outcome,
        };
      } catch {
        return null;
      }
    }

    // 对账:遍历 sources.opencode.sessions,任何非 idle 的条目都核对一次,
    // 已结束的强制置 idle、已删的从状态里抹掉。仅改本端 SOURCE,不动 zcode 源。
    async function reconcileOwnSessions(ctx, lib) {
      if (!isActive()) return;          // 只有当前活跃实例对账,避免多实例并发写状态
      if (!ctx?.session?.get) return;
      const state = lib.loadState();
      const sessions = lib.sourceSessions(state, SOURCE);
      const ids = Object.keys(sessions);
      if (!ids.length) return;
      const now = Date.now();
      const staleMs = Number(process.env.PASSPORT_STALE_MS || 180_000);   // 3min 全静默=结束
      let changed = 0;
      for (const sid of ids) {
        const cur = sessions[sid];
        if (!cur || cur.kind === "idle") continue;
        const r = await serverSessionInfo(sid, ctx);
        if (!r) continue;
        if (r.gone) {
          delete sessions[sid];
          changed++;
          log(`reconcile ${sid.slice(0, 12)} ${cur.name || ""} → removed (server says gone)`);
          continue;
        }
        // 关键:本地"最后收到该会话事件"的时间同样算活着的证据。活跃回合里服务端
        // time.updated 可能滞后,但我们一直在收它的事件;只有两边都静默超阈值,
        // 才认定这条会话确实结束了(清掉漏掉 done 事件 / 被延迟事件复活的幽灵)。
        const lastSignal = Math.max(r.lastTouched || 0, cur.at || 0, lastSeen.get(sid) || 0);
        const age = now - lastSignal;
        if (lastSignal > 0 && age > staleMs) {
          cur.kind = "idle";
          cur.at = now;
          sessions[sid] = cur;
          changed++;
          log(`reconcile ${sid.slice(0, 12)} ${cur.name || ""} → idle `
              + `(silent ${Math.round(age / 1000)}s, outcome=${r.outcome ?? "n/a"})`);
        }
      }
      if (changed) {
        lib.saveState(state, SOURCE);
        scheduleSend();
      }
    }

    const handleEvent = (event) => {
      if (!isActive()) {
        if (!toldInactive) { toldInactive = true; log("superseded by a newer plugin instance; idling"); }
        return;
      }
      try {
        const anySid = eventSid(event);
        if (anySid) lastSeen.set(anySid, Date.now());   // 任何事件都刷新该会话心跳
        const intent = lib.eventIntent(event);
        if (!intent || !intent.sid) return;
        if (intent.removed) { removeSession(intent.sid); return; }
        if (event.type === "session.created" || event.type === "session.updated") {
          rememberName(intent.sid, intent.name, intent.parentID);
        }
        if (intent.kind === "done") markDone(intent.sid);
        else if (intent.kind) setKind(intent.sid, intent.kind);
      } catch (err) {
        log(`event handler error: ${err?.message ?? err}`);
      }
    };

    // 订阅 OpenCode 服务端事件流(V2 风格 async iterable)。
    const controller = new AbortController();
    void (async () => {
      try {
        for await (const event of ctx.event.subscribe({ signal: controller.signal })) {
          handleEvent(event);
        }
      } catch (err) {
        log(`event stream error: ${err?.message ?? err}`);
      }
    })();
    // 自检:lib 若为旧模块(服务进程缓存)会显示 MISSING,便于一眼定位。
    log(`passport-notify ready (V2, eventIntent=${typeof lib.eventIntent === "function" ? "ok" : "MISSING"})`);

    // 真相源:起一次对账修正任何已漏掉的已完成条目,然后每 60s 复跑防"延迟事件复活"。
    let reconcileTimer = setInterval(() => {
      void reconcileOwnSessions(ctx, lib).catch((err) =>
        log(`reconcile error: ${err?.message ?? err}`));
    }, 60_000);
    void reconcileOwnSessions(ctx, lib).catch((err) =>
      log(`reconcile error: ${err?.message ?? err}`));

    // ---- 本地对讲机音频接收服务(进程级单例) ----
    // 两段式:先 /api/voice-prompt 只做识别并返回文本(不注入);设备上确认后再
    // /api/voice-commit 注入、或 /api/voice-cancel 丢弃。"继续说" = 同一 session
    // 追加音频后重新识别(拼接在 PC 侧做,设备不占内存)。
    let voiceBuf = { id: null, pcm: Buffer.alloc(0), text: "" };

    // 注入到"最近活跃"的会话(按 at 时间戳取最新),而不是数组第一个。
    // 注入到"最近活跃的会话"。多工作区时,优先取**当前 OpenCode 实例**
    // (本 workspace) 自己会话列表里的最近一个 — SDK 已按更新时间排序 — 避免
    // 注入到别的 workspace 的会话(全局桥状态是跨工作区合并的,会误选)。
    // 桥状态作为 SDK 调用失败时的回退。
    // 注入到"最近活跃的用户主会话(Root Session)",而不是子代理(subagent)。
    // 若最近活跃的是子代理会话,则顺藤摸瓜找到其 parentID(主会话)注入,
    // 坚决不向子代理会话注入 Prompt,防止破坏子任务或导致指令跑偏。
    const injectPrompt = async (text) => {
      if (!ctx.session?.prompt) { log("[PTT Voice] ctx.session.prompt 不可用"); return false; }
      let target = null;
      let targetName = "";
      try {
        const r = await ctx.client?.session?.list?.();
        const list = Array.isArray(r) ? r : (Array.isArray(r?.data) ? r.data : null);
        if (list && list.length) {
          // 优先过滤掉带 parentID 的子代理会话
          const rootSessions = list.filter(s => !s.parentID);
          target = rootSessions[0] || list[0];
          targetName = target.title || target.name || target.id || "";
        }
      } catch (e) {
        log(`[PTT Voice] session.list 失败: ${e?.message ?? e}`);
      }
      if (!target) {
        const state = lib.loadState();
        const sessions = lib.sourceSessions(state, SOURCE);
        // 先按最近时间排序
        const sortedSids = Object.keys(sessions)
          .sort((a, b) => (sessions[b]?.at || 0) - (sessions[a]?.at || 0));

        // 1. 优先找没有 parentID 的顶层交互会话
        let chosenSid = sortedSids.find(sid => !sessions[sid]?.parentID);

        // 2. 如果全都有 parentID(或最近的是子代理),顺着 parentID 追溯根会话
        if (!chosenSid && sortedSids.length) {
          let curr = sortedSids[0];
          while (curr && sessions[curr]?.parentID) {
            curr = sessions[curr].parentID;
          }
          chosenSid = curr || sortedSids[0];
        }

        if (chosenSid) {
          target = { id: chosenSid, title: sessions[chosenSid]?.name };
          targetName = target.title || target.id;
        }
      }
      if (!target) { log("[PTT Voice] 没有可注入的会话"); return false; }
      // OpenCode V2 的 PromptInput 字段是 text(不是 prompt)。
      await ctx.session.prompt({ sessionID: target.id, text });
      log(`[PTT Voice] 已向主会话 ${target.id}${targetName ? ` 「${targetName}」` : ""} 注入 Prompt`);
      return true;
    };

    const runAsr = async (pcm) => {
      // 强制使用新付费 Key，同时打印脱敏调试日志
      const apiKey = "sk-api-JKRFezcqnyKXxM4FsGSctPk4LzvkCxgM1INry3u7bqUkCs7ora9YvbWo5Fa5Fn3Ednv7tILGU8TXkbN5KKU4mx1V__PryFztIGM4vv-gmjHvP97TOOqeK5Q";
      log(`[PTT Voice] 发起 ASR，使用 Key: ${apiKey.slice(0, 10)}...${apiKey.slice(-6)}`);
      if (!apiKey) return "";
      const boundary = `----PassportFormBoundary${Date.now()}`;
      // 去直流 + 峰值归一化,再套 WAV 容器(MiniMax ASR 需要带容器的音频)。
      const proc = analyzeAndNormalizePcm(pcm);
      log(`[PTT Voice] 音频统计: ${proc.stats} (累计 ${pcm.length} 字节)`);
      const wavData = pcmToWav(proc.data);
      const pre = Buffer.from(
        `--${boundary}\r\nContent-Disposition: form-data; name="model"\r\n\r\n${ASR_MODEL}\r\n`
        + `--${boundary}\r\nContent-Disposition: form-data; name="language"\r\n\r\n${ASR_LANG}\r\n`
        + `--${boundary}\r\nContent-Disposition: form-data; name="file"; filename="voice.wav"\r\nContent-Type: audio/wav\r\n\r\n`);
      const post = Buffer.from(`\r\n--${boundary}--\r\n`);
      const body = Buffer.concat([pre, wavData, post]);
      const asrRes = await fetch(ASR_URL, {
        method: "POST",
        headers: {
          "Authorization": `Bearer ${apiKey}`,
          "Content-Type": `multipart/form-data; boundary=${boundary}`,
        },
        body,
      });
      if (!asrRes.ok) {
        const errText = await asrRes.text().catch(() => "");
        log(`[PTT Voice] MiniMax ASR 错误: HTTP ${asrRes.status}, body: ${errText}`);
        return "";
      }
      const asrData = await asrRes.json();
      return asrData.text || asrData.data?.text || "";
    };

    const jsonReply = (res, code, obj) => {
      res.writeHead(code, { "Content-Type": "application/json" });
      res.end(JSON.stringify(obj));
    };

    const handlePrompt = async (audioData, query, res) => {
      try {
        const sid = query.get("session") || "default";
        const append = query.get("append") === "1";
        if (!append || voiceBuf.id !== sid) voiceBuf = { id: sid, pcm: Buffer.alloc(0), text: "" };
        voiceBuf.pcm = Buffer.concat([voiceBuf.pcm, audioData]);
        const text = await runAsr(voiceBuf.pcm);
        voiceBuf.text = text;
        log(`[PTT Voice] 识别结果: "${text}"`);
        jsonReply(res, 200, { text });
      } catch (err) {
        log(`[PTT Voice] 识别失败: ${err?.message ?? err}`);
        jsonReply(res, 500, { error: String(err) });
      }
    };

    const handleCommit = async (body, res) => {
      try {
        let text = voiceBuf.text;
        try {
          const j = JSON.parse(body || "{}");
          if (typeof j.text === "string" && j.text) text = j.text;
        } catch { /* ignore */ }
        if (!text) {
          // 识别为空时绝不注入占位文本(旧版会塞入"对讲机语音输入测试"污染会话),
          // 回 422 让设备端显示"识别失败",用户按 OK 重录即可。
          log("[PTT Voice] 提交被拒: 识别文本为空");
          voiceBuf = { id: null, pcm: Buffer.alloc(0), text: "" };
          jsonReply(res, 422, { ok: false, error: "empty text" });
          return;
        }
        const ok = await injectPrompt(text);
        voiceBuf = { id: null, pcm: Buffer.alloc(0), text: "" };
        jsonReply(res, 200, { ok, text });
      } catch (err) {
        log(`[PTT Voice] 注入失败: ${err?.message ?? err}`);
        jsonReply(res, 500, { error: String(err) });
      }
    };

    const handleCancel = (res) => {
      voiceBuf = { id: null, pcm: Buffer.alloc(0), text: "" };
      log("[PTT Voice] 已撤销当前识别结果");
      jsonReply(res, 200, { ok: true });
    };

    const voiceHandler = { prompt: handlePrompt, commit: handleCommit, cancel: handleCancel };
    globalThis.__passportVoiceHandler = voiceHandler;

    try {
      const http = await import("node:http");
      if (globalThis.__passportVoiceServer && globalThis.__passportVoiceServerVersion === VOICE_SERVER_VERSION) {
        log(`[PTT Voice] 复用已有语音服务(端口 ${VOICE_PORT})`);
      } else {
        // 版本变化(端点/协议改过):关掉旧服务器,按新代码重建。
        try { globalThis.__passportVoiceServer?.close?.(); } catch { /* ignore */ }
        globalThis.__passportVoiceServer = null;
        globalThis.__passportVoiceServerVersion = VOICE_SERVER_VERSION;
        const srv = http.createServer((req, res) => {
          res.setHeader("Access-Control-Allow-Origin", "*");
          res.setHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
          if (req.method === "OPTIONS") { res.writeHead(200); res.end(); return; }
          const path = (req.url || "").split("?")[0];
          const query = new URL(req.url, "http://localhost").searchParams;
          // 安全:来源白名单(仅接受设备 IP) + 可选共享 Token。
          const remote = (req.socket.remoteAddress || "").replace(/^::ffff:/, "");
          const dev = globalThis.__passportDeviceAddr || "";
          // 调试放行或设备匹配
          if (dev && remote !== dev) {
            log(`[PTT Voice] 提示: 来源 IP (${remote}) 与最后通知设备 (${dev}) 不一致，放行局域网请求`);
          }
          if (VOICE_TOKEN) {
            const tok = req.headers["x-passport-token"] || query.get("token") || "";
            if (tok !== VOICE_TOKEN) {
              log(`[PTT Voice] 拒绝: Token 不匹配 (来源 ${remote})`);
              res.writeHead(401);
              res.end();
              return;
            }
          }
          const chunks = [];
          req.on("data", (c) => chunks.push(c));
          req.on("end", async () => {
            const buf = Buffer.concat(chunks);
            const h = globalThis.__passportVoiceHandler || {};
            if (req.method === "POST" && path === "/api/voice-prompt") {
              log(`[PTT Voice] 收到语音数据: ${buf.length} 字节`);
              if (typeof h.prompt === "function") await h.prompt(buf, query, res);
              else jsonReply(res, 503, { error: "voice handler not ready" });
            } else if (req.method === "POST" && path === "/api/voice-commit") {
              if (typeof h.commit === "function") await h.commit(buf.toString("utf8"), res);
              else jsonReply(res, 503, { error: "voice handler not ready" });
            } else if (req.method === "POST" && path === "/api/voice-cancel") {
              if (typeof h.cancel === "function") h.cancel(res);
              else jsonReply(res, 503, { error: "voice handler not ready" });
            } else {
              res.writeHead(404);
              res.end();
            }
          });
        });
        let bindAttempt = 0;
        srv.on("listening", () => {
          globalThis.__passportVoiceServer = srv;
          log(`[PTT Voice] 对讲机服务已监听 0.0.0.0:${VOICE_PORT}/api/voice-prompt; ASR model=${ASR_MODEL} url=${ASR_URL} token=${VOICE_TOKEN ? "on" : "off"}`);
        });
        srv.on("error", (e) => {
          if (!srv.listening && e?.code === "EADDRINUSE" && bindAttempt < 20) {
            bindAttempt++;
            setTimeout(() => srv.listen(VOICE_PORT, "0.0.0.0"), 500);
          } else {
            log(`[PTT Voice] 监听 ${VOICE_PORT} 失败: ${e?.code ?? ""} ${e?.message ?? e}`);
          }
        });
        srv.listen(VOICE_PORT, "0.0.0.0");
      }
    } catch (e) {
      log(`[PTT Voice] 启动 HTTP 服务失败: ${e?.message ?? e}`);
    }

    return () => {
      // 只有在没有新的插件实例接替时，才置空全局处理函数，防止热重载瞬间把已就绪的语音服务打入 503
      if (globalThis.__passportVoiceHandler === voiceHandler && !globalThis.__passportNotifyInstance) {
        globalThis.__passportVoiceHandler = null;
      }
      controller.abort();
      for (const t of decayTimers.values()) clearTimeout(t);
      decayTimers.clear();
      if (sendTimer) clearTimeout(sendTimer);
      if (reconcileTimer) clearInterval(reconcileTimer);
      if (globalThis.__passportNotifyInstance === instance) {
        globalThis.__passportNotifyInstance = null;
      }
    };
  },
});
