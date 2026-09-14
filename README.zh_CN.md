# FoloToy AI Passport 固件与设计思路

<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

本项目是基于 ESP32-C3 的 **FoloToy AI Passport** 开放式智能硬件固件及完整开发体系。

---

## 🌟 核心功能特性

1. **OpenCode & ZCode 双 Agent 联动桥接**
   - **OpenCode 插件（适配 OpenCode V2）**：`tools/opencode/passport-notify.js`，通过 `Plugin.define` 规范与局域网内 OpenCode V2 实例通信。
   - **ZCode 钩子桥接**：`tools/zcode/passport-zcode.mjs`，通过 ZCode CLI Hooks 捕获会话生命周期事件。
   - **多源状态聚合同屏显示**：OpenCode 与 ZCode 会话共享状态聚合（`~/.passport-bridge-state.json`），同屏展示两端任务状态（空闲 / 进行中 / 完成 / 需要确认），聚合优先级调度。
   - **桌面宠物（Desk Pet）**：与通知状态实时联动的 96×96 LVGL 像素动画轮播（Idle/Running/Done/Alert）。
   - **无线对讲机（Voice Prompt）**：硬件按键录音上传 -> PC 端 ASR 语音转文字 -> 设备端确认/撤销/追加后注入 Agent 会话。
   - **安全机制**：局域网白名单过滤 + 可选 `PASSPORT_VOICE_TOKEN` 双向鉴权。
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
