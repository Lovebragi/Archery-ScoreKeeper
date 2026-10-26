<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 预编译固件

`FoloToy-AI-Passport-full.bin` 是射箭计分器 v1.2.2 的合并镜像（bootloader +
分区表 + 应用）。

- 目标：ESP32-C3，8 MB Flash，ESP-IDF 5.5.3
- SHA-256：`452e1da586766280cec57a3d9b09e1b4511dc27da320ddbdc12880ece97e0bb5`
- 刷写地址：`0x0`

```bash
python -m esptool --chip esp32c3 -p <端口> -b 460800 \
  write_flash 0x0 FoloToy-AI-Passport-full.bin
```

合并镜像会覆盖 NVS 区域，刷写后设置、时钟与历史记录都会回到默认值。需要自行
构建时运行 `./tools/validate.sh --firmware`，源码在仓库根目录。
