# AGENTS.md — notes for AI coding agents working on this repo

Read `README.md` first for what the project is. This file lists the
non-obvious, hardware-specific facts discovered while debugging on real
hardware. They are not guesses — each one cost a flash cycle.

## Toolchain

- `pio run` builds; board target is `adafruit_feather_nrf52840`
  (there is no `nicenano` board in `platformio/nordicnrf52`, Feather is used
  as a proxy — same MCU/SoftDevice, different pin mapping, see below).
- `pio device monitor` needs a real TTY; in headless shells read serial with
  `python3 -c` + pyserial instead.
- Flashing is **UF2 mass storage only**: double-tap `RST`→`GND` (no buttons on
  the clone), `NICENANO` drive appears, copy the `.uf2`. Serial `nrfutil` DFU
  from the app fails (`No data received`) — don't try.
- `.hex` → `.uf2` conversion MUST pass the family ID or the bootloader
  silently ignores the file:
  `uf2conv.py firmware.hex -f 0xADA52840 -o firmware.uf2`
  (without `-f` the family is `0x0` and the file just sits on the drive).
- Bootloader: Adafruit UF2 0.6.0, `INFO_UF2.TXT` shows
  `Board-ID: nRF52840-nicenano`, SoftDevice S140 6.1.1, app origin `0x26000`.
- Factory firmware backup: `factory-backup-CURRENT.UF2` (rolled with the same
  procedure, restorable by copying it to the drive).

## Pin mapping (Feather Arduino numbers → nRF ports, from the variant files)

The firmware uses **Feather** numbering, the PCB is wired as **Nice!Nano /
nRF Pro Micro**. Never trust a silkscreen `Dn` label — always translate via
the nRF port. Pins actually used:

| Function | Arduino # | nRF port | Board marking |
|----------|-----------|----------|---------------|
| Tip input | 29 | P0.17 | pad `017` |
| Red LED (indicator) | 24 | P0.15 | — |
| Blue LED | — | — | charger STAT, hardware, NOT a GPIO |

- Extra GPIO pads on the clone are labeled with raw port names:
  `017` = P0.17, `100` = P1.00, `104` = P1.04.
- Red/blue are swapped vs genuine Nice!Nano on recent SuperMini revisions:
  red = Bluetooth GPIO (P0.15), blue = charger STAT (blinks with no battery
  on BAT, off when on batteries). See wiki `joric/nrfmicro`, Alternatives page.
- `ENABLE_VCC_CUT` stays 0: with the Feather map, Arduino 13 is P1.09, not the
  P0.13 rail switch — do not touch it.

## Bluefruit52 / SoftDevice gotchas (all verified)

- `Bluefruit.begin(1, 0)` — NEVER `(0, 0)`. With zero peripheral roles the
  SoftDevice skips the `CONN_CFG_PERIPHERAL` GAP config and
  `sd_ble_gap_adv_start()` fails silently: the Serial log happily prints
  `ADV ...` while nothing is transmitted. Always log
  `Bluefruit.Advertising.isRunning()`.
- Advertising type must be set explicitly, otherwise it defaults to connectable:
  `Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED)`.
- `analogReadVDD()` returns **raw SAADC counts**, not mV. Core defaults are
  ref Internal 0.6 V, gain 1/6 (= 3.6 V full scale), 10-bit resolution.
  This project forces 12-bit and converts: `mV = raw * 3600 / 4096`.
- `Bluefruit.autoConnLed(false)` is required, otherwise the stack blinks the
  conn LED on its own (2 Hz while advertising).
- ISR does minimum work: debounce + counter + flag. LED flashing and Serial
  printing happen in `loop()`, never in the ISR.
- Apple Silicon Macs: `bleak` BLE scans return zero devices unless the Python
  binary has Bluetooth permission — verify RF with the nRF Connect phone app.

## What "done" looks like

`pio run` SUCCESS → UF2 with `-f 0xADA52840` → copy to `NICENANO` → Serial
shows `== TC-01 boot ==` and `adv_running=YES` → phone finds `TC-01` →
shorting `017`–`GND` prints `TICK` and increments mm in the next packet.
