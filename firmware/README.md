<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Prebuilt firmware

`FoloToy-AI-Passport-full.bin` is the merged image (bootloader + partition table
+ application) for the archery scorekeeper v1.2.3.

- Target: ESP32-C3, 8 MB Flash, ESP-IDF 5.5.3
- SHA-256: `e75e992f780ee6199010802c81460ceafe0d28086eaeb1abff50587d42ffb654`
- Flash offset: `0x0`

```bash
python -m esptool --chip esp32c3 -p <PORT> -b 460800 \
  write_flash 0x0 FoloToy-AI-Passport-full.bin
```

The merged image covers the NVS region as well, so after flashing the settings,
the clock and the history start from their defaults. Build it yourself with
`./tools/validate.sh --firmware`; the source is in the repository root.
