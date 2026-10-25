<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Prebuilt firmware

`FoloToy-AI-Passport-full.bin` is the merged image (bootloader + partition table
+ application) for the archery scorekeeper v1.2.0.

- Target: ESP32-C3, 8 MB Flash, ESP-IDF 5.5.3
- SHA-256: `293a44834509de2f8522cb82d3935224e42c2ac4fba0a062d90df06edc4738d5`
- Flash offset: `0x0`

```bash
python -m esptool --chip esp32c3 -p <PORT> -b 460800 \
  write_flash 0x0 FoloToy-AI-Passport-full.bin
```

The merged image covers the NVS region as well, so after flashing the settings,
the clock and the history start from their defaults. Build it yourself with
`./tools/validate.sh --firmware`; the source is in the repository root.
