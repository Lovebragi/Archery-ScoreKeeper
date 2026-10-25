<p align="right">
  <a href="archery-scorekeeper.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Archery Scorekeeper (Half and Full Targets)

A score keeper for 40 cm half targets, three-spot faces and 40/80/122 cm full
targets. It runs on FoloToy AI Passport, records a configurable number of groups
and arrows, stores the last 300 sessions with timestamps in NVS, and shows them
again from the history screen with an all / today / last-7-days filter. An
optional Wi-Fi page connects to a router for NTP time sync, with phone-based
provisioning or on-device password entry. The
application owns the display and the three buttons; it never uses the baseline
test menu and needs no network, audio, or cloud account.

## Scoring model

The app supports five target faces, chosen in Settings:

| Target | Printed rings | Entry values |
| --- | --- | --- |
| 40 cm half target | 6…10 plus the inner ten | X, 10, 9, 8, 7, 6, miss |
| Three-spot 40 cm half target | three half faces, same ring values | X, 10, 9, 8, 7, 6, miss |
| 40 cm full target | 0…10 plus the inner ten | X, 10, 9 … 1, 0 |
| 80 cm full target | 0…10 plus the inner ten | X, 10, 9 … 1, 0 |
| 122 cm full target | 0…10 plus the inner ten | X, 10, 9 … 1, 0 |

On the full faces the 0 ring is the miss. The three-spot target keeps three
separate faces: the setup screen asks for the group size plus how many arrows go
to the upper, middle and lower face (default 2 / 2 / 2, and their sum must equal
the group size), and the entry screen then records one face at a time before
moving to the next. Every arrow is encoded as one of:

| Code | Meaning | Points |
| --- | --- | --- |
| `0` / `M` | Miss / zero ring | 0 |
| `1` … `9` | Ring value | 1 … 9 |
| `10` | Ten ring | 10 |
| `X` | Inner ten | 10, counted separately for tie-breaks |

Totals, per-group totals, the X count, the miss count, the best group, and the
average per arrow are derived from these codes. Average is shown with one
decimal and rounded half-up. The chosen target is stored with each session, so
history shows the same ring list that was used for entry.

## Screens and controls

The screen can be inverted or left in the panel's natural orientation. Settings
offers both, defaulting to inverted; `BSP_LCD_ROTATE_180` in `bsp_pins.h` is the
boot default and the application re-applies the saved choice right after display
initialisation. The three buttons form a vertical column on the side of the
case, so the button roles follow the chosen orientation: when the screen is
inverted the roles rotate 180 degrees (physical up acts as OK, physical down
stays down, physical OK acts as up); in the normal orientation they keep their
natural meaning. Switching the option flips the screen and the key mapping
immediately.

| Screen | Shows | Up / Down | OK | Long OK |
| --- | --- | --- | --- | --- |
| Home | Target rings, four entries, clock and network status, battery | Select entry | Enter | – |
| Setup | Group count, arrows per group, per-face arrows for the three-spot target, start | Adjust the focused value | Next field / start | Previous field (back to home on the first row) |
| Settings | Target type, screen orientation, screen brightness, screen-off time, date/time, back | Adjust the focused value | Next field / open date-time / save and back | Save and back |
| Entry | Two-column ring tally for the current group and a commit row (one face at a time for the three-spot target) | Change the highlighted ring's arrow count | Next ring / commit row | Previous ring (abandon confirmation on the first row) |
| Summary | Finished group, running total, group dots | – | Next group / summary | Abandon confirmation |
| Finish | Session total, average, X/M, save state | – | Back to home | Back to home |
| History | Timestamped sessions (newest 300), all / today / last-7-days filter, clear and back rows (an empty list still shows the back row) | Move cursor | Open detail / clear / home | Cycle filter |
| Wi-Fi | Connection state, scan / phone provisioning / sync time / disconnect / back | Move cursor | Run action | Back to home |
| Wi-Fi password | SSID, password being typed, current character | Change character (hold to repeat) | Next character / connect | Delete / back |
| Detail | One group of one session, shown as the same ring tally | Previous / next group | Back | Back |
| Confirmation | Abandon session or clear history | Select option | Execute | Cancel |
| Date/time | Year, month, day, hour, minute | Adjust the focused field | Next field / save | Save |

A group is recorded as "how many arrows landed on each ring" instead of
arrow-by-arrow: the entry screen lists X, 10, 9, 8, 7, 6 and miss, each with a
count. The counts start at zero and must add up to the arrows per group before
the commit row accepts OK, so the running total accumulates from zero instead of
starting from a full-house maximum. The commit row shows either the remaining
arrow count or the group's ring total. As soon as the counts add up, OK jumps
straight from the current ring row to the commit row, so a full group takes one
more OK instead of walking through the remaining empty rows.

## Persistence

- NVS namespace `archery`.
- One blob per record under `s000`…`s299` and one `meta` blob holding the next
  sequence number. The slot is `(seq - 1) % 300`, so the newest 300 sessions are
  always addressable without rewriting older records. The NVS partition is
  256 KB, which leaves room for 300 full 12 × 12 records.
- Records carry a version byte and a CRC-16/CCITT check. A record that fails the
  check is ignored; on boot the application rescans all slots, repairs the meta
  blob, and keeps the newest contiguous run of valid records.
- The record stores group count, arrows per group, target type, duration in
  seconds, a timestamp, and 4-bit packed ring codes (144 arrows fit in 72 bytes).
  Records written before the target or timestamp fields existed still decode and
  fall back to a 40 cm half target and an unknown time.
- Wi-Fi credentials live in the same NVS namespace (`w_ssid`, `w_pass`) and are
  reused on boot.
- Clearing history erases the record blobs and restarts numbering at 1; settings
  and the clock are kept.
- The device has no battery-backed RTC. The clock is a persisted local wall time
  plus a monotonic timer: it keeps running while powered, resumes from the last
  saved value after power loss, and can be set by hand in Settings or by NTP
  after connecting to Wi-Fi. History filters use that clock, and unknown times
  show `--`.

## Fonts

Digits use LVGL's Montserrat 20 and 40. Chinese text uses a Noto Sans SC subset
generated from the UI's own strings:

- `assets/fonts/archery_chars.txt` — the non-ASCII code point inventory.
- `assets/fonts/archery_font_16.c`, `archery_font_20.c` — generated LVGL fonts
  (4 bpp, PLAIN, no kerning, about 79 KB of bitmap data in Flash).
- `assets/fonts/archery_font_16.json`, `archery_font_20.json` — per-glyph
  checksums used by the static gate.

`tools/gen_lvgl_text_font.py` rasterizes the glyphs with Pillow/FreeType and
emits the LVGL format directly. `tools/lvgl_font_tools.py` reads the generated
file back exactly as LVGL would. `tests/test_archery_fonts.py` fails when UI text
contains a character that the fonts or the inventory do not cover, so missing
Chinese text is caught by the static gate instead of appearing as placeholder
boxes on the device. At runtime every label also logs `U+XXXX` for a missing
glyph.

To add UI text, edit the C sources and regenerate both sizes:

```bash
python tools/gen_lvgl_text_font.py \
  --font NotoSansSC-VF.ttf --weight 400 --size 16 --name archery_font_16 \
  --inventory assets/fonts/archery_chars.txt \
  --scan main/archery_ui.c main/archery_score.c --update-inventory \
  --output assets/fonts/archery_font_16.c --manifest assets/fonts/archery_font_16.json
```

Source font and license are recorded in [`assets/README.md`](../../assets/README.md).

## Power behaviour

The home screen has a third entry, Settings, with five controls:

| Setting | Values | Default |
| --- | --- | --- |
| Target type | 40 cm half, three-spot half, 40/80/122 cm full | 40 cm half |
| Screen orientation | Inverted / Normal | Inverted |
| Screen brightness | 20 / 40 / 60 / 80 / 100% | 20% |
| Screen-off time | 0 / 15 / 30 / 60 / 120 s (`0` keeps the screen on) | 15 s |
| Date/time | Local wall clock, set by hand | Build time until calibrated |

After the configured idle time the application turns the backlight off, disables
the display output, hands the shared key pin back to a digital input and enters
light sleep. Any key wakes it in milliseconds: the display comes back at the
configured brightness and the same page is shown, with the session still in RAM.
The key press that wakes the device only wakes it; events are ignored until that
key is released, so a wake press cannot submit or advance anything.

The application deliberately avoids deep sleep: an unfinished session lives only
in RAM, and a deep-sleep wake reboots the chip. There is no intermediate dimming
stage; the brightness setting is the only brightness control. The battery gauge
is read every 10 seconds and shown as a small bar in the top-right corner (visual
coordinates); an unavailable gauge renders an empty shell instead of a fake
percentage.

## Build and test

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
./tools/validate.sh            # repository checks, host tests, firmware gate
```

The firmware gate writes the merged image for offset `0x0` to
`build/FoloToy-AI-Passport-full.bin` and keeps matching debugging artifacts under
`build/firmware/<sha256>/`. Flash only that merged image for a complete refresh;
`idf.py flash` remains the incremental development path.

## Verification status

- Host tests cover ring values, session totals, the page/button state machine,
  record encoding/corruption handling, slot arithmetic, grid layout, and font
  coverage.
- The firmware gate verifies the merged image layout and offsets.
- Not verified here: real on-device rendering (including the 180° orientation and
  Chinese glyphs), the ADC button feel, battery gauge accuracy, and mechanical
  fit of the inverted screen. These require the physical device.
