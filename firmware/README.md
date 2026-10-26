<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Prebuilt firmware

`FoloToy-AI-Passport-full.bin` is the merged image (bootloader + partition table
+ application) for the archery scorekeeper v1.2.1.

- Target: ESP32-C3, 8 MB Flash, ESP-IDF 5.5.3
- SHA-256: `24d9361ce8a73ef4f4cbf729c6b6116304bf1a0e90d906164b5d62e82b0796aa`
- Flash offset: `0x0`

```bash
python -m esptool --chip esp32c3 -p <PORT> -b 460800 \
  write_flash 0x0 FoloToy-AI-Passport-full.bin
```

The merged image covers the NVS region as well, so after flashing the settings,
the clock and the history start from their defaults. Build it yourself with
`./tools/validate.sh --firmware`; the source is in the repository root.
