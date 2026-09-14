<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# 固件产物

本目录包含 **AI Passport Bridge** 在 FoloToy AI Passport 硬件（ESP32-C3 / 8 MB Flash / ESP-IDF 5.5.3）上的预编译固件二进制文件，由 `main` 分支最新构建产出，可直接烧录。

## 文件

| 文件 | 大小 | SHA-256 |
| --- | --- | --- |
| `ai-passport-bridge-full.bin` | 2.66 MB | `c2a0178e1760ccde00a629a2d18bdcfb2ec70c1e25a9f8e2b135ad5aad803e3f` |

## 烧录方法

该 `.bin` 是合并镜像（bootloader + 分区表 + 应用程序），可直接从偏移地址 `0x0` 烧录：

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 ai-passport-bridge-full.bin
```

## ⚠️ 桌宠资源版权

固件内置的 **桌宠像素精灵资源**（参见 `main/pet_frames.c` 与 `tools/pet2lvgl/desk-pet-spritesheet.png`）**为第三方作品，仅供个人学习与参考**。

> **不允许二次修改、二次分发、再打包或移植到衍生固件中（含 fork、再发行）。** 若你基于本仓库开发衍生固件，请在发布前**移除桌宠相关素材**。

其余源码遵循 [MIT 许可证](../../LICENSE)。
