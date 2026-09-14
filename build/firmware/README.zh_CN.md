<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# 固件产物

本目录包含 FoloToy AI Passport（ESP32-C3 / 8 MB Flash / ESP-IDF 5.5.3）的预编译固件二进制文件，由 `main` 分支最新构建产出，可直接烧录。

## 文件

| 文件 | 大小 | SHA-256 |
| --- | --- | --- |
| `FoloToy-AI-Passport-full.bin` | 2.66 MB | `c2a0178e1760ccde00a629a2d18bdcfb2ec70c1e25a9f8e2b135ad5aad803e3f` |
| `FoloToy-AI-Passport.bin` | 2.61 MB | `fc9e26f1245a249d13216daaf0a00d26f525774b44c013eed33bee09ad815c12` |
| `merged-binary.bin` | 1.77 MB | `6a1d7bed11c1dd2748c102782c52f5cc0af2fb745d2acc86bb44b207a7dd7a56` |
| `partition-table.bin` | 3.0 KB | `a98e0784a39f37da373daf6dcef7a400af3e98b5344e89f414eeebedb1b5e07c` |
| `bootloader.bin` | 20.5 KB | `4a21d2562ee6f4bd605d319391dcdad849df6cb5555fc183acd0d5369092f209` |

## 烧录方法

合并镜像一键烧录：

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 merged-binary.bin
```

按分区分别烧录：

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash \
    0x0       bootloader.bin \
    0x8000    partition-table.bin \
    0x10000   FoloToy-AI-Passport.bin
```

## ⚠️ 桌宠资源版权

固件内置的 **桌宠像素精灵资源**（参见 `main/pet_frames.c` 与 `tools/pet2lvgl/desk-pet-spritesheet.png`）**为第三方作品，仅供个人学习与参考**。

> **不允许二次修改、二次分发、再打包或移植到衍生固件中（含 fork、再发行）。** 若你基于本仓库开发衍生固件，请在发布前**移除桌宠相关素材**。

其余源码遵循 [MIT 许可证](../../LICENSE)。
