# NRF-BLE-tick-counter

Autonomous ultra-low-power rainfall counter for a **tipping-bucket rain meter**
with a **reed switch or hall sensor**, based on Nordic **nRF52840**.
Each bucket tip is counted, converted to millimeters of rain using the funnel
calibration, and broadcast over **BLE advertising** (non-connectable) every
5 seconds — no connection, no phone app pairing, no gateway handshake needed.
The count survives power loss (LittleFS) and runs **2–3 years on 2×AA lithium
batteries**, reporting low-battery state in the same packet.

Board: nRF52840 clone of Nice!Nano / nRF Pro Micro (Adafruit UF2 bootloader 0.6.0,
`NICENANO` drive, `Board-ID: nRF52840-nicenano`, SoftDevice S140 6.1.1).
Firmware: PlatformIO + Arduino (Adafruit nRF52 core, target
`adafruit_feather_nrf52840`) + Bluefruit52.

## Features

- Counts tips on pad **`017`** (= nRF P0.17 = Arduino 29) pulled to GND —
  reed switch or `DRV5032FADBZR` hall sensor (open-drain), active LOW,
  internal pull-up, FALLING edge. Debounce: hall 5 ms / reed 40 ms
  (`SENSOR_IS_REED`).
- Rain calibration: catchment 8220 mm² (66.6×129 mm minus r=20.8 corner radii),
  tip volume 8.65 ml (500 ml / 57.8 tips) → **1 tip ≈ 1 mm** (exactly 1.0523).
  `rain_mm = raw * 865 * 1000 / (100 * 8220)`, integer math only (uint64).
  Tune the set screw for exactly 8.22 ml/tip to get 1.00 mm/tip;
  re-calibrate with a syringe by editing `TIP_VOLUME_ML_*`.
- BLE advertising every 5 s: flags + manufacturer data
  (company `0xFFFF`, magic `0x5443`, rain total u32 LE in **whole mm**,
  battery mV u16 LE, **last-24h rain u8**, flags) + name `"TC-01"`.
  The 24 h window is a 256-stamp RAM ring (reboot resets the window only;
  totals survive in flash). See packet section below.
- Persistence: RAM + LittleFS `/tickcount.bin` (magic+CRC). Saves every 1 mm
  (`SAVE_STEP_UNITS=1`), every 24 h heartbeat, urgently on low battery.
  u32 wrap-around keeps counting.
- Power: `2×AA Energizer Ultimate Lithium L91`. Internal VDD measurement only
  (SAADC, `raw*3600/4096` at 12 bit); the on-board divider is unused.
  Thresholds `warn 2.4 V / urgent 2.2 V`, 3 consecutive readings.
  See **Battery wiring** below — wrong wiring is a fire hazard.
- LEDs: on recent SuperMini revisions red/blue are swapped vs the original:
  **red = GPIO P0.15 (Arduino 24)** carries all indication
  (tick 30 ms, adv 80 ms); **blue = charger STAT output, hardware**,
  blinks with no battery on BAT, not software-controllable, off on batteries.
  `autoConnLed(false)`.

## Sensor wiring

- Sensor: `SIG → 017`, `GND → GND`; hall sensor additionally `VDD → 3V3`.
- Hall sensor to order: `TI DRV5032FADBZR` (SOT-23, open-drain, ~1 µA),
  second source `AH1808-W-7`. Do NOT use `A3144/US1881/KY-003` modules
  (3–5 mA continuous — kills the batteries in weeks).

## Battery wiring (read carefully)

Power the **2×AA L91** pack **past the charger, straight into the 3.3 V rail**:

```
AA holder (+) ──[Schottky BAT54/1N5819]──┬──> 3V3 (VCC)
                                         └──||──> GND (100 µF bulk cap 3V3–GND)
AA holder (−) ───────────────────────────> GND
BAT / B+ / B- pads — leave EMPTY
```

- **Never connect primary (non-rechargeable) AA cells to `BAT / B+ / B-`.**
  Those pads feed the LiPo charger (`LTH7R`): with USB plugged it would try
  to charge your primary lithium cells at 4.2 V — fire hazard.
  `BAT` is only for a rechargeable 3.7 V LiPo (the board's native scenario,
  not this project).
- The diode lets USB and batteries coexist (USB powers the rail while flashing);
  otherwise disconnect the AAs while USB is plugged in.
- The 100 µF cap covers TX current peaks (AA cells have high ESR).
- Fresh cells give ~3.0 V on the rail (after the diode), depleted ~1.8 V;
  the nRF52840 works down to 1.7 V, and our 2.4/2.2 V thresholds save
  the counter with margin.
- Keep batteries and metal away from the ceramic antenna.

## BLE packet (advertising)

Type: **non-connectable, non-scannable** (`ADV_NONCONN_IND`) — broadcast only,
cannot connect. Interval **5 s** (`8000 × 0.625 ms`), Tx **0 dBm**,
name **`TC-01`**. 25 of 31 bytes used.

AD structures as seen by a scanner:

| # | Field | Bytes (hex) | Meaning |
|---|-------|-------------|---------|
| 1 | Flags | `02 01 06` | len=2, type `0x01`, `0x06` = LE General Discoverable, BR/EDR not supported |
| 2 | Manufacturer Specific | `0D FF FF FF 43 54 01 00 00 00 FD 0C 01 04` | len=13, type `0xFF`, then 12-byte MFG payload (below) |
| 3 | Complete Local Name | `06 09 54 43 2D 30 31` | len=6, type `0x09`, `"TC-01"` ASCII |

MFG payload, 12 bytes, **little-endian**:

| Offset | Len | Field | Format | Example |
|--------|-----|-------|--------|---------|
| 0 | 2 | Company ID | u16 LE, test value `0xFFFF` | `FF FF` |
| 2 | 2 | Magic `"TC"` | `0x5443`, "this is our packet" marker | `43 54` |
| 4 | 4 | Rainfall total | u32 LE, **whole mm**, rounds down | `01 00 00 00` = 1 mm |
| 8 | 2 | Supply | u16 LE, **millivolts** of VDD | `FD 0C` = 3325 mV |
| 10 | 1 | Last 24 h | u8, mm in the trailing 24 h window (max 255) | `01` = 1 mm |
| 11 | 1 | Flags | bit0 batt warn, bit1 batt urgent, bit2 reed mode | `04` = reed mode |

Full example (1 tip after reboot, USB power, reed mode):
`02 01 06 0D FF FF FF 43 54 01 00 00 00 FD 0C 01 04 06 09 54 43 2D 30 31`

Notes:
- Company `0xFFFF` is reserved by Bluetooth SIG for testing. Get your own
  Company ID for production.
- Rain counter max is `0xFFFFFFFF` = 4,294,967,295 mm, then wraps to 0
  and keeps counting.
- In `nRF Connect`: find `TC-01` → open `Manufacturer Data` → check magic `0x5443`.

Parser (Python + bleak — `manufacturer_data` arrives without the Company ID;
the dict key *is* `0xFFFF`):

```python
import struct

def parse_tc(manufacturer_data: dict) -> dict | None:
    payload = manufacturer_data.get(0xFFFF)
    if not payload or len(payload) < 10:
        return None
    if payload[0:2] != b'\x43\x54':  # magic "TC"
        return None
    rain_mm, batt_mv, rain24_mm, flags = struct.unpack('<IHBB', bytes(payload[2:10]))
    return {
        'rain_mm': rain_mm,
        'rain24_mm': rain24_mm,
        'batt_v': batt_mv / 1000.0,
        'low_warn': bool(flags & 0x01),
        'low_urgent': bool(flags & 0x02),
        'reed_mode': bool(flags & 0x04),
    }
```

Same in C — walks raw advertising bytes (AD structures), no BLE-stack
dependency, endianness handled explicitly (works on big-endian hosts too):

```c
#include <stdint.h>
#include <string.h>

#define TC_COMPANY_ID 0xFFFFu
#define TC_MAGIC      0x5443u   /* "TC", little-endian on air */

typedef struct {
    uint32_t rain_mm;    /* total rainfall, whole mm */
    uint16_t batt_mv;    /* supply voltage, millivolts */
    uint8_t  rain24_mm;  /* trailing 24 h window, mm (max 255) */
    uint8_t  flags;      /* bit0 batt warn, bit1 batt urgent, bit2 reed mode */
} tc_data_t;

/* adv: full advertising packet bytes, adv_len: its length.
 * Returns 0 on success (out filled), -1 if not our packet. */
int tc_parse_adv(const uint8_t *adv, uint8_t adv_len, tc_data_t *out)
{
    uint8_t i = 0;
    while (i < adv_len) {
        uint8_t len = adv[i];              /* len covers type + data */
        if (len == 0 || i + len >= adv_len)
            break;                         /* malformed */
        uint8_t type = adv[i + 1];
        /* Manufacturer Specific (0xFF), company 0xFFFF, 10 payload bytes */
        if (type == 0xFF && len >= 1 + 2 + 10 &&
            adv[i + 2] == (TC_COMPANY_ID & 0xFF) &&
            adv[i + 3] == (TC_COMPANY_ID >> 8)) {
            const uint8_t *p = &adv[i + 4]; /* magic at p[0..1] */
            uint16_t magic = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            if (magic != TC_MAGIC)
                return -1;
            out->rain_mm  = (uint32_t)p[2]        | ((uint32_t)p[3] << 8) |
                            ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
            out->batt_mv  = (uint16_t)p[6] | ((uint16_t)p[7] << 8);
            out->rain24_mm = p[8];
            out->flags    = p[9];
            return 0;
        }
        i += (uint8_t)(len + 1);           /* step over len byte + record */
    }
    return -1;
}
```

## Configuration (`include/config.h`)

| Define | Meaning | Default |
|--------|---------|---------|
| `SENSOR_PIN` | tip input (P0.17 pad `017`) | 29 |
| `SENSOR_IS_REED` | 1 = reed 40 ms debounce (+flag), 0 = hall 5 ms | 1 |
| `CATCHMENT_AREA_MM2` | funnel effective area | 8220 |
| `TIP_VOLUME_ML_NUM/DEN` | tip volume, ml | 865/100 |
| `SAVE_STEP_UNITS` | flash save every N mm | 1 |
| `TIP_RING_SIZE` | 24 h window ring size (stamps) | 256 |
| `ADV_TX_POWER_DBM` | BLE TX power | 0 |
| `BAT_WARN_MV / BAT_URGENT_MV` | battery thresholds (mV, rail) | 2400/2200 |
| `PIN_LED_ADV / PIN_LED_TICK` | red LED (P0.15) | 24/24 |

## Build & flash (no buttons — UF2 only)

```
pio run
python3 <framework>/tools/uf2conv/uf2conv.py .pio/build/feather_nrf52840/firmware.hex -f 0xADA52840 -o .pio/build/feather_nrf52840/firmware.uf2
```

Short `RST` to `GND` twice quickly → `NICENANO` drive appears → copy
`firmware.uf2` onto it. Serial `nrfutil` DFU from the app does not work with
this bootloader — UF2 only. Factory firmware is backed up in
`factory-backup-CURRENT.UF2`.

## Verification

1. Serial 115200: `== TC-01 boot ==`, `TICK raw=N`, `ADV ... adv_running=YES`, `SAVE`.
2. `nRF Connect`: device `TC-01`, company `0xFFFF`, magic `0x5443`.
3. Short `017` to `GND` = one tip (red flash + `TICK`), ≈ +1 mm each.
4. Power-cycle with batteries removed → value restored (after a save).

## Power budget (estimate)

Sleep ~4 µA (System ON + DC/DC) + `DRV5032` ~1 µA + one adv/5 s (~10 mA × 3 ms
→ ~6 µA average) ≈ 12–15 µA total. A 3500 mAh L91 pair is limited by
self-discharge, not by the load: 2–3+ years easily. VDD sampling (1/min)
and flash saves (1/mm + daily) average to nanoamps.

## Working with an AI agent

This project is set up to be continued by an AI coding agent: the task history
lives in git (`main` branch), the build is one command (`pio run`), flashing
is drag-and-drop UF2, and verification is Serial log + `nRF Connect`.
Board-specific traps that cost real debugging time (Feather pin map vs clone
wiring, `Bluefruit.begin(1,0)` requirement, raw SAADC readings, UF2 family ID)
are collected in **[AGENTS.md](AGENTS.md)** — read it before changing
anything hardware-related.
