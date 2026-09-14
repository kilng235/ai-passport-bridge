# AI Passport Bridge（FoloToy AI Passport 固件）

<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

本项目是基于 ESP32-C3 的 **FoloToy AI Passport** 硬件之上的 **AI Passport Bridge 固件与设计思路**。

仓库命名为 **ai-passport-bridge**，强调本仓库提供的是连接 **OpenCode / ZCode 等桌面 Agent 与 FoloToy AI Passport 硬件设备的桥接固件**，而非 FoloToy AI Passport 硬件或固件产品本身。

---

## 🌟 核心功能特性

1. **OpenCode & ZCode 双 Agent 联动桥接**
   - **OpenCode 插件（适配 OpenCode V2）**：`tools/opencode/passport-notify.js`，通过 `Plugin.define` 规范与局域网内 OpenCode V2 实例通信。
   - **ZCode 钩子桥接**：`tools/zcode/passport-zcode.mjs`，通过 ZCode CLI Hooks 捕获会话生命周期事件。
   - **多源状态聚合同屏显示**：OpenCode 与 ZCode 会话共享状态聚合（`~/.passport-bridge-state.json`），同屏展示两端任务状态（空闲 / 进行中 / 完成 / 需要确认），聚合优先级调度。
   - **桌面宠物（Desk Pet）**：与通知状态实时联动的 96×96 LVGL 像素动画轮播（Idle/Running/Done/Alert）。
   - **无线对讲机（Voice Prompt）**：硬件按键录音上传 -> PC 端 ASR 语音转文字 -> 设备端确认/撤销/追加后注入 Agent 会话。
   - **安全机制**：局域网白名单过滤 + 可选 `PASSPORT_VOICE_TOKEN` 双向鉴权。

2. **🚨 关于语音对讲（Voice Prompt）的兼容性说明**
   - **当前仅适配 OpenCode V2 版本**：语音注入端点 `/api/voice-commit` 基于 OpenCode V2 的 SDK（`ctx.client.session.list()`、`ctx.client.session.prompt()`）实现会话检索与指令注入，V1 旧版架构不兼容。
   - **依赖外部 ASR 服务**：设备端固件只负责**录音 + 传输裸 PCM 音频**（通过 `POST /api/voice-prompt` 上传），**本身不内置 ASR 模型**。语音转写（ASR）由 PC 端 OpenCode 插件调用大模型厂商的语音识别 API 完成，默认使用 **MiniMax 的 `asr-1.0` 模型**，端点为 `https://api.minimaxi.com/v1/speech_to_text`。
     - ASR 模型可由环境变量 `PASSPORT_ASR_MODEL`、`PASSPORT_ASR_URL`、`PASSPORT_ASR_LANG` 自定义（详见 `tools/opencode/README.zh_CN.md`）。
     - 使用前需要确保对应 ASR 服务可用并已配置相应 API Key。
   - **接入流程（OpenCode V2）**：
     1. 将 `tools/opencode/passport-notify.js` 与 `tools/lib/passport-bridge-state.mjs` 拷贝到 OpenCode 插件目录：
        - 全局：`~/.config/opencode/plugins/passport-notify.js` + `~/.config/opencode/plugins/lib/passport-bridge-state.mjs`
        - 或项目级：`.opencode/plugins/`（保持目录结构一致）
     2. 重启 OpenCode（插件在启动时加载）。
     3. 设置环境变量：
        ```bash
        export PASSPORT_URL=http://<设备IP>/notify  # 或使用 PASSPORT_HOST=folopassport.local
        export PASSPORT_VOICE_TOKEN=<自定义Token>   # 可选,设备配网页填入同值
        export PASSPORT_ASR_MODEL=asr-1.0           # 可选,默认 MiniMax ASR 模型
        export PASSPORT_ASR_URL=https://api.minimaxi.com/v1/speech_to_text
        export PASSPORT_ASR_LANG=zh                 # 语言提示,默认中文
        ```
     4. 设备端：进入**通知/桌宠**页，`OK` 短按开始录音 -> 再次短按结束 -> PC 端调用 ASR 服务转写 -> 设备显示文本并等待确认（`OK` 发送 / `上` 继续说 / `下` 撤销 / 双击 `OK` 重录）。
   - **协议端点**：`POST /api/voice-prompt`（上传音频 + 调用 ASR，返回 `{text}`）、`POST /api/voice-commit`（注入）、`POST /api/voice-cancel`（丢弃）。详细字段与返回格式见 `tools/opencode/README.zh_CN.md`。
2. **网络与配网**
   - 内置 SoftAP Web 配网门户（`192.168.4.1`），支持 Wi-Fi 与大模型 API Key 动态设置。
   - 全局后台驻留网络服务（`app_net`），开机自动连网并广播 mDNS（`folopassport.local`）。
3. **网络收音机与多媒体**
   - 支持国内多城市节点切换、多流媒体解析与 ES8311 音频硬件解码。
4. **安全与隐私设计**
   - 绝无硬编码 API Key 或 Wi-Fi 密码，全部凭证保存在本地 NVS 加密分区。
   - TLS 证书链校验防止中间人拦截。

---

## 📁 目录结构

```text
├── components/bsp/      # 板级支持包（ST7789P3 屏幕、ES8311 音频、CW2017 电量计、ADC 按键）
├── main/                # 固件业务逻辑、LVGL 页面、状态机与网络服务
├── docs/                # 完整设计文档、架构思路、硬件规范与参考案例
│   ├── hardware-design/ # 硬件设计规范与低功耗分析
│   ├── reference/       # 架构权衡、网络流媒体、屏幕刷新等经验沉淀
│   └── development/     # AI 协同开发指南与编译规范
├── tools/               # 辅助工具套件
│   ├── opencode/        # OpenCode V2 伴侣插件（状态推送与语音注入）
│   ├── zcode/           # ZCode 联动桥接
│   ├── pet2lvgl/        # 像素帧精灵图转换与打包工具
│   └── check_repo.py    # 仓库安全与合规性静态扫描脚本
├── tests/               # 脱离硬件的主机逻辑单元测试
├── partitions.csv       # 8 MB Flash 分区表（保留受保护 cardid 区域）
└── sdkconfig.defaults   # 默认工程配置
```

---

## 📦 预编译固件下载（仅一体化镜像）

> 直接下载使用，无需自行编译 ESP-IDF 工程。
> 合并镜像（bootloader + 分区表 + 应用程序），**从 `0x0` 偏移一键烧录**。
> **仅供个人学习与参考，请勿二次分发。**

| 文件 | 大小 | SHA-256 |
| --- | --- | --- |
| `ai-passport-bridge-full.bin` | 2.66 MB | `c2a0178e1760ccde00a629a2d18bdcfb2ec70c1e25a9f8e2b135ad5aad803e3f` |

**烧录命令**：

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 ai-passport-bridge-full.bin
```

**完整说明**：[`build/firmware/README.md`](build/firmware/README.md)

**下载入口**：[GitHub Releases → v1.0.0-bridge](https://github.com/kilng235/ai-passport-bridge/releases/tag/v1.0.0-bridge)

> 也可通过 Git 直接下载：
>
> ```bash
> curl -L -o ai-passport-bridge-full.bin \
>   https://raw.githubusercontent.com/kilng235/ai-passport-bridge/main/build/firmware/ai-passport-bridge-full.bin
> ```

### ⚠️ 桌宠（Desk-pet）资源版权声明

`main/pet_frames.c` 与 `tools/pet2lvgl/desk-pet-spritesheet.png` 中的 **桌宠像素素材为第三方作品，仅供个人学习与参考，**不允许**二次修改、二次分发或移植到衍生固件中**。若基于本仓库开发衍生固件，请在发布前**移除桌宠相关素材**。其余源码遵循 [MIT 许可证](LICENSE)。

---

## 🛠️ 编译与开发

- **芯片目标**：ESP32-C3（8 MB Flash，无 PSRAM）
- **开发框架**：ESP-IDF v5.5.3

### 运行主机测试与合规检查

```bash
python tools/check_repo.py
python -m unittest discover -s tests
```

---

## 📄 开源许可

本项目遵循 [MIT 许可证](LICENSE)。
