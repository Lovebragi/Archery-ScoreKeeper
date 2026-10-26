<p align="right">
  <a href="archery-scorekeeper-changelog.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Archery scorekeeper changelog

Release history for the archery scorekeeper application. The repository-wide
[changelog](../CHANGELOG.md) tracks the AI Passport project itself; this file
follows the application's firmware versions.

## 1.2.1 — 2026-10-26

### Fixed

- Short-pressing OK on the "Phone setup / export" QR page now stops the setup
  hotspot and returns to the Network menu. Previously the press did nothing and
  only a long press could leave the page.

## 1.2.0 — 2026-10-25

### Added

- Download the full training history as a UTF-8 CSV from the phone setup page at
  `192.168.4.1`. One row per session covers date, target, groups, arrows,
  score, average, inner tens, misses and per-ring totals, ready for Excel.
- Show the firmware version in the top-left corner.

### Changed

- The Wi-Fi menu entry is now "Phone setup / export"; the setup page offers the
  CSV download next to the network configuration.

## 1.1.0 — 2026-10-25

### Added

- Network page: scan nearby Wi-Fi, type the password on the device, or provision
  from a phone through the open `Archery-Setup` hotspot and the `192.168.4.1`
  web page.
- NTP time sync after connecting (UTC+8), with the clock persisted to NVS.
- Top status bar with the current time (HH:MM), a network indicator dot and the
  battery gauge.

### Changed

- Default screen brightness is 20% and screen-off time is 15 s.
- The setup hotspot is open (no password) and exists only while the Network page
  is shown.

### Fixed

- The Network menu could not move the cursor before the first scan.
- Password repeat used the physical key direction without the 180-degree role
  rotation.
- The provisioning page stayed on "scanning" because scans ran while the access
  point was active; it now uses a pre-scan cache and a Refresh button.

## 1.0.0 — first public release

- 40 cm half target, three-spot target and 40/80/122 cm full targets.
- Per-ring arrow counts, group/total/average/inner-ten/miss statistics.
- 300 sessions with timestamps and all/today/last-7-days filters.
