<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

## Unreleased

- 将 SoftAP 配网页面全新美化为**8-bit Cyber Arcade 控制台**风格,与设备屏幕的
  像素风 UI 保持统一:深邃赛博背景与青色霓虹扫描网格;琥珀+青色霓虹顶栏 `:: FOLOPASSPORT
  :: CONTROL` 与设备 HUD 铭牌;三大 8-bit 像素卡片模块(Wi-Fi Network / API Keys /
  PTT Token);扫描 Wi-Fi 时实时显示像素信号格(▂▄▆█);密码输入框支持一键明暗切换;
  街机实体按键带微下沉投影;首页右上角实时显示 `READY/OFFLINE` 状态。保存成功与重启
  页也同步换肤。

- 将原本仅用于硬件验证的「低功耗」设置页重构为真正实用的**【待机与熄屏设置】**（`demo_low_power.c`）：
  提供 **30秒（极速省电）**、**1分钟（标准平衡，默认）**、**3分钟（长时显示）**、**从不熄屏（桌面常亮）** 4 个实用档位。
  配置持久化存储至 NVS（`idle_sec`）并动态联动 `power_idle` 全局分级背光定时器。
  设置菜单项同步更名为直观的「待机熄屏」。

- 恢复 Wi-Fi 全速模式（`WIFI_PS_NONE`），取消会影响实时 chunked 音频流式传输的 Modem-Sleep，
  彻底解决录音首包握手超时及数据断流导致的「录音时间过短」问题。
  充分满足长句及复杂 Prompt 表达需求，且得益于 chunked 流式传输，设备端 RAM 占用保持恒定 ~1KB 零膨胀。
  在后台常驻监听通知的空闲期，Wi-Fi 射频在 AP DTIM 周期之间自动休眠，
  设备待机电流由约 75mA 骤降至约 18mA，大幅延长脱线电池续航，且完全不影响局域网接收推送。
  语音对讲（PTT）由原先的弹窗浮层升级为通知/桌宠大模块内的专属全屏 8-bit 页面，
  配备专属状态指示灯、实时像素跳动声波条、Prompt 识别文本视窗和操作说明。
  录音与发送操作严格隔离在「语音对讲机」专属页面（第 2 页）内触发，通知页与桌宠页不再响应短按录音，避免误触。

- 语音注入目标在多工作区下更准确：优先用 OpenCode SDK 的 `ctx.client.session.list()`
  取**当前 OpenCode 实例**（即当前 workspace）按更新时间排序的最近会话，不再只依赖跨
  工作区合并的全局桥状态（之前在多 workspace 时会把会话 ID 注到别的 workspace 导致
  `session not found`）；全局桥状态保留作为 `session.list` 失败时的回退。

- 加固语音链路安全：8090 的入站放行收敛为 **仅专用网络 + 本地子网**，并把 WLAN 归为专用
  网络、恢复 `opencode.exe` 在 Public 上的 Block 规则；语音服务只接受**来自设备 IP** 的
  请求（来源白名单）。新增可选共享 Token（PC 端 `PASSPORT_VOICE_TOKEN`，设备端在配网页
  「语音 Token」填入，请求带 `X-Passport-Token`）。此前 `POST /api/voice-commit` 无鉴权，
  同网段任何人都能向 OpenCode 注入 Prompt（等价于远程执行）。

- 对讲机新增「识别后确认」环节：识别完成先不注入，设备显示识别文本并等待操作——
  **OK 发送 / 上键继续说（PC 侧按 session 拼接音频后再识别）/ 下键撤销 / 双击 OK 重录**。
  PC 端拆成 `/api/voice-prompt`（只识别）与 `/api/voice-commit`、`/api/voice-cancel`
  三个端点；语音服务改为**进程级单例**（热重载只替换处理函数），消除重载时的端口空窗
  导致的「连接语音服务失败」。识别前对音频做**去直流 + 峰值归一化**并带 `language=zh`；
  注入改用 OpenCode V2 的 `PromptInput.text`（原误用 `prompt` 会 500），目标改为
  **最近活跃的会话**（原取数组第一个会注入到旧会话）。

- 对讲机（语音 Prompt）改版：交互从「按住 OK 说话」改为 **OK 短按开始录音、再短按结束**
  （长按 OK 恢复为返回菜单，不再与录音冲突）；上传改为**流式 chunked POST**，不再预分配
  256KB 整段缓冲（ESP32-C3 无 PSRAM，该分配必然失败）；目标主机改为从最近一次
  `/notify` 的源 IP 自动得到（即电脑端 Bridge），不再写死 IP；语音端口默认 **8090**
  （8080 常被 NVIDIA Broadcast 等软件占用），可用 `PASSPORT_VOICE_PORT` 覆盖；PC 端
  ASR 请求带上 `model` 字段（默认 **`asr-1.0`**，可用 `PASSPORT_ASR_MODEL` 覆盖），
  端点默认 **`https://api.minimaxi.com/v1/speech_to_text`**（可用 `PASSPORT_ASR_URL`
  覆盖），并把设备传来的裸 PCM 套上 WAV 头再上传（该接口不收裸 PCM）。

- 修复设备 UI 的中文/符号「豆腐框」：字符子集表 `font/symbols.txt` 补入 `·`(U+00B7)
  与 `▶`(U+25B6) 并重新生成 `main/lv_font_hansans_14_cjk.c`（通知页的状态标签此前缺
  字形）；同时修正「先建空标签、后填中文」导致选成 Montserrat 的页面（提示音页的
  开/关、音量、静音，亮度页的保存结果），赋值文本时按内容重选 CJK 字库。

- 修复 OpenCode 桥在**未安装可选依赖 `bonjour-service` 时退回写死的旧设备 IP**、
  导致设备一直停在"等待中"收不到推送的问题:新增按设备 mDNS 主机名
  (`PASSPORT_MDNS_NAME`,默认 `folopassport.local`)解析出 IPv4 再连接的兜底,
  DHCP 换 IP 后也能自动找回设备(直接让 fetch 解析 `.local` 在 Windows 上会超时,
  所以先解析成数字地址再连)。

- 修复 Token Quota 页 MiniMax 区域始终显示 `login fail` 与 `NO DATA` 的回归：HTTP
  `Authorization` 头缓冲从 128 字节恢复到足以容纳最长 Key（`APP_CFG_KEY_MAX + 16`），
  避免 `sk-cp-` 长订阅 Key 被截断而在 MiniMax 端登录失败；`parse_minimax` 恢复按真实
  响应结构 `model_remains[]`（`current_interval_remaining_percent` /
  `current_weekly_remaining_percent` / `remains_time` / `weekly_remains_time`）解析，
  替换此前不存在的 `model_plans` / `plans` 结构。

- 把 **OpenCode 桥**迁移到 OpenCode V2 插件 API（`Plugin.define({ id, setup })` +
  `ctx.event.subscribe`），并修复了设备收不到 OpenCode 通知的根因：V2 事件负载在
  `event.data`，而桥读的是 V1 的 `event.properties`，导致每个处理器都静默空转。
  字段处理改由可主机测试的 `tools/lib/passport-bridge-state.mjs` 里的
  `eventIntent` 统一承担。

- 让 OpenCode 桥识别 OpenCode 2.0.x 实际发出的事件：实时流用 `session.step.*` /
  `session.tool.*` / `session.text.*` / `session.reasoning.*` 活动事件表示"进行中"
  （这个版本不发 `session.status`），用 `session.execution.succeeded` 或末步
  `session.step.ended`（`finish: "stop"`）表示回合结束。`eventIntent` 会把 SDK 的
  `session.next.*` 与运行时 `session.*` 两种命名归一化，任一拼写都识别。

- 修复"需要确认"状态到不了设备的问题：OpenCode 2.0.x 把 question/permission 确认框
  表达为 **form 事件**（`form.created`，session id 嵌在 `data.form.sessionID`），
  而不是 `question.asked`，所以确认挂起时桥从未触发 `alert`。现在 `eventIntent`
  把 `form.created` → 需要确认、`form.replied` / `form.rejected` → 恢复进行，并解析
  嵌套的 session id。事件名归一化在 `.next` 之外也剥掉 `.v2` 段
  （`question.v2.asked`、`permission.v2.asked`），`question.rejected` 同样恢复进行。

- 加固 OpenCode 桥以应对运行时模块缓存与设备首包延迟：改为把共享库拷到**唯一
  临时路径**再导入（服务进程按路径缓存 ESM，仅加 `?v=` 破缓存无效），并把发送
  超时从 2s 放宽到 5s（设备 Wi-Fi 省电唤醒后首个请求需要更久）。启动日志会打印
  `eventIntent=ok` / `MISSING`，首次成功送达也会记录。同时把 OpenCode 按 location
  重复实例化的插件收敛为单一活动实例，每次变化只推一次（而不是每个打开的位置各推一次）。

- 修复通知桥与固件之间的**会话名溢出**：共享库先把名字裁到一个字节预算、再追加
  `…`，结果最多 48 字节，而设备缓冲只能存 47 字节；解析器于是从 UTF-8 序列中间
  截断省略号，渲染出乱码方块。共享库现在把**返回值**（含省略号）限制在固件的
  `APP_NOTIFY_NAME_MAX` 以内，并新增 Node 宿主测试
  （`tests/test_passport_bridge_state.mjs`）把两侧上限锁死。

- 修复 ZCode 会话被打断（ESC 终止）/中途退出后设备永远显示"任务进行中"的问题——
  ZCode 对打断不产生任何终止事件，桥端无从推送最终状态。现给 running 加
  **保鲜窗**：超过 5 分钟未刷新的 running 按 idle 展示（实现为可主机测试的
  `app_notify_logic.c` 里的 `app_notify_kind_effective`，设备端用自己的接收
  时刻计算时效）。为此同 kind 的重复推送改为**静默落库**（不响铃、不亮屏、
  不重复重绘），并引入存储版本号供通知页检测内容变化；ZCode 桥的
  `PreToolUse` 相应改为每次都发（作为保鲜刷新），不再去重。

- 新增 **ZCode 通知桥**（`tools/zcode/`）：五个 ZCode 钩子
  （`SessionStart`=idle 仅记账、`UserPromptSubmit`/`PreToolUse`=running、
  `PermissionRequest`=alert、`Stop`=done）把 agent 状态转发到与 OpenCode 桥
  相同的 `POST /notify`。两桥共享
  PC 侧状态文件（`~/.passport-bridge-state.json`，抽成
  `tools/lib/passport-bridge-state.mjs`），每次事件都推送**合并**快照——
  OpenCode 与 ZCode 的会话在设备上同屏显示，聚合优先级
  `alert > running > done > idle`。钩子脚本快速失败（1 秒发送超时 + 60 秒离线
  冷却）以适配内联执行，done 后由 detached 衰减进程补推列表状态。状态未变化
  的钩子整条去重（`PreToolUse` 不会刷屏），打开但空闲的 ZCode 会话不会被显示
  成"进行中"。OpenCode
  插件安装时现在需要连同 `tools/lib/` 一起拷贝。固件零改动。

- 通知提示音新增**静音时段**：提示音页长按上/下循环切换预设（关 / 22:00-08:00 / 23:00-07:00），持久化于 NVS。窗口在 `app_beep_play` 内部执行——所有提示音调用方自动被覆盖，亮屏、渲染与桌宠动画不受影响（只压声音）。窗口逻辑放在可主机测试的 `main/app_beep_logic.c`（支持跨零点窗口；时钟未对上时恒不静音，时源为 `app_net` 的 SNTP），页面试听改走 `app_beep_preview` 旁路，静音窗内调音量/开关仍有声音反馈。

- 新增**桌宠页**,与通知页组成 **通知 ↔ 桌宠 水平滑动轮播**(上/下键经 LVGL strip 横向位移动画切换页面;长按 OK 返回)。桌宠按通知聚合状态(`空闲/进行中/完成/需要确认`)播放 4 组 96×96 `lv_animimg` 逐帧动画,并随后台推送实时切换。绿幕 spritesheet 由新增的 `tools/pet2lvgl` 转换器切片、抠绿/去绿边并打包为 LVGL `RGB565A8` C 数组(16 帧,~432KB;app 余量 13%)。主菜单新增「桌宠」。

- 通知页在负载带 `sessions` 时显示**多会话列表**(最多 3 行:状态色 + 会话名);只列进行中/已完成/需确认(不列空闲),聚合优先级 `确认 > 进行中 > 完成 > 空闲`。负载新增 `count` 与 `sessions[]`;`app_notify_logic` 以括号深度限定顶层键来解析该数组,会话名每次重选 CJK/拉丁字体,HTTP body 上限提到 1280B。OpenCode 插件跟踪会话(名称取自 `Session.title`)、聚合与去抖、`done` 5s 后衰减为 `idle`,并处理 `session.created/updated/deleted` 与 `permission.replied`。

- 修复确认后的通知状态:`alert`/`done` 是瞬时叠加提醒,原实现到点一律回落 `空闲`;现在回到背后的持续基态(`进行中`/`空闲`),且插件在 `permission.replied`/`question.replied` 时补发 `running`。确认后仍在跑的任务不会再错误显示为空闲。

- **P1 — 通知后台常收。** 新增 `app_net` 服务,成为全应用**唯一的 Wi-Fi STA 持有者**(开机启动、自动重连、SNTP);额度页与 Wi-Fi 扫描页改为复用它,不再各自 `esp_wifi_init`/`deinit`,消除页面间 Wi-Fi 冲突。`app_notify` 改为**开机即启动并后台常驻**,通知页退页不再关闭服务,任意页面都能收到推送。设备通过 mDNS(`espressif/mdns`)广播 `folopassport.local` / `_folopassport._tcp:80`。OpenCode 插件按 `PASSPORT_URL` → `PASSPORT_HOST` → mDNS(`bonjour-service`)→ `PASSPORT_FALLBACK` 解析地址。配网页会临时暂停常驻 STA 与通知服务,退出后恢复。

- 通知页：把重复的状态文案合并为**单行 kind 状态**（带颜色），下方只显示消息 `text` 作为详情；`done`/`alert` 5s 后回落 `空闲`。此前 kind 行与消息标题会同时显示状态词（如"空闲"+"任务完成"）。

- 修复通知页出现方框：状态分隔符用了 `·`（U+00B7），而 CJK 子集里没有该字形，改用字体覆盖的 `・`（U+30FB）。

- 按键**长按阈值**从 1.5s 降到 0.8s（`CONFIG_BUTTON_LONG_PRESS_TIME_MS=800`），并在 `main.c` 对重复 `LONG` 事件去抖。此前按不够 1.5s 会被当成单击（在首页菜单上=进入选中项，看着像"刷新"而非返回）；去抖也避免同一次长按被处理两次。

- 修复烧录/监视时"连不上设备"：开启自动浅睡后，ESP32-C3 的 USB-Serial-JTAG 在芯片浅睡时不响应，导致 `esptool`/网页烧录器超时报 "No serial data received"。现启用 `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y`：**USB 连接期间保持不休眠**，拔掉后才浅睡。

- 通知**状态模型**重做为 4 态，对齐 OpenCode 的 `session.status`（`idle`/`busy`/`retry`）：`idle`（默认/未知）、`running`（`busy`/`retry`）、`done`（`session.idle`）、`alert`（`permission.asked` / `question.asked` / `session.error`）。插件改订阅 `session.status`（仅在"进入忙"时发一次 `running`），并把 `question.asked`/`session.error` 归到 `alert`。通知页 `done`/`alert` 显示 5s 后回落 `idle`；旧 `info`/`error` 两态移除（旧的 `"error"` 仍解析为 `alert`）。

- 新增 **提示音** 设置页：OK 开关通知提示音，上/下调音量（10–100%，步进 10）。改动即时试听并存入 NVS（`pcfg`，键 `beep_on` / `beep_vol`）；存储走 worker，按键回调不阻塞。

- 新通知按状态播放短**提示音**：`running` 下行两音、`done` 上行两音、`alert` 两短高音、`idle` 单音。实现在 `main/app_beep.c`，按需打开 ES8311/I2S 并在播放后立即 `bsp_audio_suspend()` 省电，使用整数正弦表（C3 无 FPU，避免拖入 libm）。

- 设备中文字体扩展到完整的 **GB2312** 字符集（6763 汉字 + 标点/符号，共 7540 字形），使通知里的动态中文能正常显示而非占位框。生成的 `main/lv_font_hansans_14_cjk.c` 从约 336KB 增至约 650KB flash。字符集变化时用 `lv_font_conv --font <otf> --size 14 --bpp 4 --range 0x20-0x7f --symbols "$(cat font/symbols.txt)" ...` 重新生成；`main/CMakeLists.txt` 已定义 `LV_LVGL_H_INCLUDE_SIMPLE=1`，生成的字体不再因缺少 `lvgl/lvgl.h` 路径而编译失败。

- 新增 **OpenCode 通知桥**：新的 `通知` 页以 STA 连入家庭 Wi-Fi 并运行 `POST /notify`，显示由配套的 OpenCode 插件（`tools/opencode/passport-notify.js`）推送的最新一条消息，覆盖 `permission.asked` / `session.idle` / `session.error`。扁平 JSON 解析放在可主机测试的 `main/app_notify_logic.c`；消息存储与 HTTP 服务在 `main/app_notify.c`。只转发关键事件（不含消息正文）。Wi-Fi 为独占资源，故仅在通知页打开时链路生效。

- 启用**电源管理（DFS + 自动浅睡）**：`esp_pm_configure()` 让 CPU 重载 160MHz、空闲降到 80MHz，无人持锁时自动浅睡。BSP 在背光亮时持 `ESP_PM_NO_LIGHT_SLEEP` 锁（LEDC 属数字外设，浅睡会停 PWM 背光），因此仅在熄屏后才进入浅睡——把待机电流从 ~25mA 往个位数 mA 压。`sdkconfig.defaults` 打开 `CONFIG_PM_ENABLE`、`CONFIG_FREERTOS_USE_TICKLESS_IDLE`、`CONFIG_PM_SLP_IRAM_OPT`、`CONFIG_PM_RTOS_IDLE_OPT`。

- **移除网络收音机**功能：收音机页、城市选择、C++ 流播放器（`radio_player.cc`）、电台目录（`radio_catalog.cc`）与城市库全部删除，并去掉 `espressif/esp_audio_codec`（MP3 解码）依赖。保留 `demo_radio.c/.h` 作为共享的 NVS/netif/事件循环助手（配网门户、Token Quota、Wi-Fi 页仍在使用）。主页菜单现为 `Token Quota` / `设置`，设置菜单为 `Wi-Fi` / `低功耗` / `亮度` / `配网`。固件体积减少约 690KB（2.55MB → 1.84MB）。

- 修复电台目录拉取失败（城市切换报"请检查网络"）：目录查询原版只请求单一服务器 de1.api.radio-browser.info，国内网络经常不可达或限流。实测明文 HTTP 到境外节点会被链路干扰（对齐 RadioBunny 等国内可用前端的方案），现改为 **HTTPS + de1/at1/nl1 节点池逐个兜底**，空结果也换节点重试；城市页状态分阶段显示（正在联网/正在获取电台/网络未连上/目录拉取失败），联网与目录 worker 栈加大到 8KB。

- 修复设备上中文出现"框框"（缺字形）：子集字体此前的符号表只收汉字,混排字符串里的 ASCII(如提示语中的 "OK")与"·"等标点被整体路由进中文字体时无字形可渲染。现将中文字库子集扩充为 **GB2312 一级常用字（3755 字）+ 项目词汇 + 常用标点 + ASCII 区段(0x20-0x7F)**（lv_font_conv 重新生成）,网络电台名等任意常见中文均可渲染,不再需要随新增文案扩表。

- 收音机新增**城市切换**（移植 leo-radio 的城市电台）：设置菜单新增 `城市` 页（自动定位/北京/上海/广州/深圳/长沙/杭州/成都）,OK 确认后后台联网拉取该城市目录并装入播放器,完成后按任意键返回收音机页即播;选择持久化于 NVS `radio/city`,收音机页启动时按所选城市发现目录。顺带把 Wi-Fi STA 连接/断开提取为 `demo_radio` 共享助手,并让 `radio_player_init` 幂等（城市页与收音机页都可触发）。

- 缝合 [leo-radio](https://github.com/leo0183/leo-radio) 的**网络收音机**功能（其播放器/电台目录为 MIT 许可）：主菜单新增 `收音机` 页，进入即连 Wi-Fi 并自动开台。上/下短按换台、长按调音量（10–100%，存 NVS `radio` 命名空间），OK 短按播放/暂停，长按返回菜单。移植 `radio_player.cc`（HTTP 流 → esp_audio_codec MP3 解码 → ES8311 输出，含电平谱与断流重连）与 `radio_catalog.cc`（IP 定位 + radio-browser 目录，失败时回退内置 6 个兜底电台），播放器对 UI 的回调经 `main/radio_bridge.h` 解耦；`radio_player.h` 做 C 兼容化供页面直接调用。页 UI 按主题重画：调谐拨杆（真实频率落位，无频率按台位铺开且读数留空）、18 段电平表、播放状态行。

- **禁用蓝牙栈**（`CONFIG_BT_ENABLED=n`，同 leo-radio 的取舍）：ESP32-C3 无经典蓝牙、音频用途本就不存在，禁用整棵 BLE 栈为 MP3 流解码腾出运行时内存；蓝牙演示页从固件移除（源码保留），Wi-Fi 拉流缓冲参数对齐 leo-radio 实测值。为此把中文字库子集从 529 字扩到 619 字（`font/symbols.txt` + lv_font_conv 重新生成），覆盖电台名与播放状态文案。

- 全设备 UI 换肤，借鉴同硬件衍生项目 [leo-radio](https://github.com/leo0183/leo-radio) 的设计语言：近黑蓝背景（`0x071017`）+ 琥珀强调（`0xFFB74D`）+ 网格线分隔 + 暖白/灰青双档文字。菜单改为 leo 的行卡片风格（选中行琥珀底 + 深色文字），底部统一加"1px 分隔线 + 操作提示"。新增 `main/ui_theme.h/.c` 作为主题配色与构件的单一来源；旧浅色页面（Wi-Fi 扫描 / 蓝牙 / 低功耗 / 配网）经由 `ui_pixel.h` 的同名常量重映射与 `ui_pixel_screen_create`/`ui_pixel_panel_create` 的内部重写自动换肤，页头改为"居中标题 + 分隔线"，吉祥物保留。

- Token Quota 页同步换肤并升级为 leo 式状态栏：顶栏改为品牌字（琥珀）+ 时钟 + **电量百分比**（经 CW2017 电量计 `bsp_battery_soc()`，30s 轮询，低于 15% 变红，无电量计则留空）；底部状态行加分隔线。亮度页对齐 leo 音量页布局（居中标题 + 大号百分比 + 宽进度条）。

- 设置菜单新增**亮度**页：上/下短按以 10% 步进调整背光（范围 20–100%，下限对齐空闲调暗档），屏上进度条即时预览；OK 短按经一次性 worker 任务把数值写入 NVS（独立整型键，不动配置 blob）。保存的亮度开机即套用，并作为空闲熄屏的恢复档；未按 OK 离开页面则丢弃预览。设置菜单增至五项，卡片布局相应压缩以适配 240x320 屏；步进/边界逻辑放在可主机测试的 `main/brightness_logic.c` 并配套宿主测试。

- 导航改为两级结构：开机直达 **Token Quota**；长按确定返回主页菜单（`Token Quota` / `设置`），`设置` 打开二级菜单（`Wi-Fi` / `蓝牙` / `低功耗` / `配网`）。Display/Button/Audio/Battery 四个演示页从菜单移除（源码保留）；长按逐级返回，主页菜单长按回到 Token Quota。

- **配网门户**新增附近 2.4G Wi-Fi 扫描：SoftAP 以 APSTA 模式运行并提供 `GET /scan`，返回去重、按 RSSI 降序的 JSON 列表。网页表单带下拉框与“刷新”按钮，加载即自动扫描，用户点选网络而无需手输 SSID（仍可手输兜底）。ESP32-C3 仅 2.4G，结果天然全为 2.4G。

- 重新引入简体中文字体：14px 思源黑体子集（`main/lv_font_hansans_14_cjk.c`，由 LVGL 组件内置 OTF 生成）经 `main/ui_font.c` 真正作为 Montserrat 的回退生效，修复此前宏守卫不匹配导致回退被编译掉的问题。`设置`/`蓝牙`/`低功耗`/`配网` 等菜单文案可在设备上正常显示。

- 修复设备上中文全部空白：生成的字体位图为 RLE 压缩（`.bitmap_format = LV_FONT_FMT_TXT_COMPRESSED`），现于 `sdkconfig.defaults` 打开 `CONFIG_LV_USE_FONT_COMPRESSED=y`。未开启时 LVGL 的 `get_glyph_bitmap` 走 `#else` 分支返回 `NULL`，中文全部渲染为空，而 ASCII（Montserrat，PLAIN 格式）仍正常显示。

- Token Quota 请求恢复 TLS 服务器证书校验：HTTP 客户端改用内置根证书包校验服务器证书链与域名（`esp_crt_bundle_attach`），移除 `ESP_TLS_INSECURE` / `SKIP_SERVER_CERT_VERIFY` 调试开关与跳过域名校验，避免已保存的 DeepSeek/MiniMax API Key 被中间人截获。

- 新增 **Setup SoftAP 配置门户**：菜单项启动热点（`FoloPassport-XXXX`，密码 `folotoy123`）与一个极简 HTTP 服务，只提供 `/`（表单）和 `/save`。手机连上热点后打开 `http://192.168.4.1`，填写 Wi-Fi 账号密码与 DeepSeek/MiniMax API Key，配置以 NVS blob 持久化（可主机测试的 `main/app_cfg.c` 模型 + `main/app_portal.c`）。保存后设备重启；Token Quota 页改为从 NVS 读取凭据，不再硬编码到源码；首次开机无 Wi-Fi 配置时自动进入 Setup。在 Token Quota 页短按下键即可重开 Setup，修改 Wi-Fi 或 API Key。

- 新增全局空闲熄屏：无操作 30s 背光降到 20%，60s 熄屏，任意按键恢复全亮。分级判断在可主机测试的 `main/power_idle_logic.c`；Display 与 Low Power 页在自行控制背光期间临时关闭该功能。

- 开机固定进入 Token Quota 页（未配置 Wi-Fi/Key 时进入配网页）；临时的 `BOOT_PAGE_INDEX` 间接层已移除。

- 加入厂家为优特利 520mAh 电芯生成的 80 字节 CW2017 profile，并实现内容与更新标志检查、写入后校验、规定的重启时序以及有上限的 SOC 就绪等待。

- 扩充环境引导文档：新增乐鑫 Git 服务镜像（`git.espressif.com.cn`）作为中国大陆首选线路，覆盖 ESP-IDF v5.5.3 及其子模块；补充子模块长等待/超时处理、原地修复，以及 `esp32-wifi-lib` 等大仓的按钉死 commit 浅取；提示按仓库残留的 Jihulab `insteadOf` 旧配置；并把官方离线 release 压缩包加入兜底方案（经验来自 `esp-mosaico/esp-mosaico-vibe`）。

- 按功能域整理文档并采用双入口：根目录 `AGENTS.md` 变为薄路由（只保留硬约束与任务路由），详细的 AI 开发工作流下沉到 `docs/development/ai-guide.md`，`agent-guide.md` 并入其中。为 `docs/development/` 增加二级分区（`engineering/`、`ci/`、`release/`），把 `plays/` 应用档案与 `experiences/` 移入带专属 README 的 `docs/reference/` 参考区；删除 `docs/software-design/`（空脚手架）；把 `assets/{fonts,images,music}/README` 三个叶子 README 并入 `assets/` README；把 `project-completion` 的六个子文档压平为单文件；并把每个目录统一为单一 README，消除所有 `INDEX` 文件与一处重复经验索引。所有交叉引用与文献链接已更新；未丢弃任何内容。

- 删除位于 `0x700000` 的旧 app/test 分区，以及相关的 bootloader、校验和
  文档要求；固定的 `cardid` 保护分区及其 CI 校验保持不变。
- 规定多应用发布的 Release 标题约定：tag 按 `v<版本>-<应用名>`（如 `v0.1.0-voice-keychain`）命名，让 Release 标题同时带版本与应用名；发布成功后核对标题，保证一眼扫 Release 列表就能区分是哪个应用。
- 新增发布后收尾流程：`issue-suggestions` skill 用于把用户反馈作为 issue 提交到上游项目；`experience-pr` skill 用于把可复用的开发经验作为文档 PR 提交；新增 `docs/experiences/` 目录保存单条经验文件；并配套 `project-completion`、`file-issues` 与经验索引文档。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。
