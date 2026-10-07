# NRF-BLE-tick-counter

Плата: nRF52840, клон Nice!Nano / nRF Pro Micro (Adafruit UF2-bootloader 0.6.0,
диск `NICENANO`, `Board-ID: nRF52840-nicenano`, SoftDevice S140 6.1.1).
Прошивка: PlatformIO + Arduino (Adafruit nRF52, таргет `adafruit_feather_nrf52840`) + Bluefruit52.

## Что делает
- Считает тики: площадка **`017` (= P0.17 = Arduino 29)** на GND,
  геркон либо холл `DRV5032FADBZR` (open-drain), активный LOW, pull-up, FALLING.
  Антидребезг: холл 5мс / геркон 40мс (`SENSOR_IS_REED`).
- Калибровка осадкомера: водосбор 8220 мм² (66.6x129 минус скругления r=20.8),
  тип 8.65 мл (500 мл / 57.8 типов) => **1 тип ≈ 1 мм** (точно 1.0523).
  `rain_mm = raw * 865 * 1000 / (100 * 8220)`, только целая арифметика (uint64).
  Винт под 8.22 мл/тип даст ровно 1.00 мм/тип; шприц-тест — правим `TIP_VOLUME_ML_*`.
- Non-connectable advertising раз в 5 сек, пакет:
  `flags + mfg (company 0xFFFF, magic 0x5443, rain u32 LE в сотых мм, batt_mV u16 LE, flags) + name "TC-01"`.
  Декодер: мм = rain / 100.0. flags: bit0 warn, bit1 urgent, bit2 reed_mode.
- Память: RAM + LittleFS `/tickcount.bin` (magic+CRC). Сейв каждый 1 мм
  (`SAVE_STEP_UNITS=1`), раз в 24ч, срочно при `VDD < BAT_URGENT_MV`. Переполнение u32 = wrap.
- Питание `2xAA L91`: `AA+ -> Шоттки -> 3V3/VCC`, `BAT` в воздухе, `+100мкФ`.
  Замер только `internal VDD` (SAADC, `raw*3600/4096` при 12 битах),
  внешний делитель платы не используется. Пороги `warn 2.4В / urgent 2.2В`, 3 подряд.
- LED (карта Feather не совпадает с разводкой клона, пины явные):
  `PIN_LED_ADV = 4 (P1.10)` — вспышка в момент adv,
  `PIN_LED_TICK = 24 (P0.15)` — вспышка на тик. `autoConnLed(false)`.

## Подключение
- Датчик: `SIG -> 017`, `GND -> GND`, холлу еще `VDD -> 3V3`.
- Батарейки: holder `2xAA` -> диод -> `3V3`, минус -> `GND`, `BAT` свободен.
  USB втыкать только со снятыми АА (для прошивки/отладки).
- Холл заказать: `TI DRV5032FADBZR` SOT-23 open-drain, дубль `AH1808-W-7`.

## BLE-пакет (advertising)

Тип: **non-connectable, non-scannable** (`ADV_NONCONN_IND`) — только рассылка,
подключиться нельзя. Интервал **5 сек** (`8000 × 0.625 мс`), Tx **0 dBm**.
Имя устройства: **`TC-01`**. Всего 24 байта из лимита 31.

Структура пакета (AD structures, как видит сканер):

| # | Поле | Байты (hex) | Разбор |
|---|------|-------------|--------|
| 1 | Flags | `02 01 06` | len=2, type `0x01`, значение `0x06` = LE General Discoverable, BR/EDR not supported |
| 2 | Manufacturer Specific | `0C FF FF FF 43 54 01 00 00 00 FD 0C 00` | len=12, type `0xFF`, дальше 11 байт MFG (см. ниже) |
| 3 | Complete Local Name | `06 09 54 43 2D 30 31` | len=6, type `0x09`, `"TC-01"` в ASCII |

MFG payload, 11 байт (порядок — **little-endian**):

| Смещение | Длина | Поле | Формат | Пример |
|----------|-------|------|--------|--------|
| 0 | 2 | Company ID | u16 LE, тестовый `0xFFFF` | `FF FF` |
| 2 | 2 | Magic `"TC"` | `0x5443`, проверка «это наш пакет» | `43 54` |
| 4 | 4 | Осадки | u32 LE, **целые мм** (округление вниз) | `01 00 00 00` = 1 мм |
| 8 | 2 | Питание | u16 LE, **милливольты** VDD | `FD 0C` = 3325 мВ |
| 10 | 1 | Флаги | бит0 — батарея warn, бит1 — батарея urgent, бит2 — режим геркона | `04` = reed_mode |

Пример целиком (1 тип после перезагрузки, питание от USB, режим холла):
`02 01 06 0C FF FF FF 43 54 01 00 00 00 FD 0C 00 06 09 54 43 2D 30 31`

Замечания:
- Company `0xFFFF` — зарезервирован Bluetooth SIG для тестов. Для серии нужен свой Company ID.
- Счетчик осадков u32 с wrap: после `4294967295` (42 млн мм) переходит в 0 и считает дальше.
- В `nRF Connect`: устройство `TC-01` -> раскрыть `Manufacturer Data` -> сверять magic `0x5443`.

Как парсить (Python + bleak — `manufacturer_data` уже без Company ID, ключ словаря и есть `0xFFFF`):

```python
import struct

def parse_tc(manufacturer_data: dict) -> dict | None:
    payload = manufacturer_data.get(0xFFFF)
    if not payload or len(payload) < 9:
        return None
    if payload[0:2] != b'\x43\x54':  # magic "TC"
        return None
    rain_mm, batt_mv, flags = struct.unpack('<IHB', bytes(payload[2:9]))
    return {
        'rain_mm': rain_mm,
        'batt_v': batt_mv / 1000.0,
        'low_warn': bool(flags & 0x01),
        'low_urgent': bool(flags & 0x02),
        'reed_mode': bool(flags & 0x04),
    }
```

## Сборка/прошивка (кнопок нет — только UF2)
```
pio run
python3 <uf2conv> .pio/build/feather_nrf52840/firmware.hex -f 0xADA52840 -o .pio/build/feather_nrf52840/firmware.uf2
```
Дважды коротко замкнуть `RST-GND` -> диск `NICENANO` -> скопировать `firmware.uf2`.
Серийный `nrfutil` DFU не работает с этим bootloader из приложения — только UF2.
Заводская прошивка сохранена в `factory-backup-CURRENT.UF2`.

## Проверка
1. Serial 115200: `== TC-01 boot ==`, `TICK raw=N`, `ADV scaled/batt/flags`, `SAVE`.
2. `nRF Connect`: устройство `TC-01`, company `0xFFFF`.
3. Замыкание `017-GND` = тик (красный + `TICK`), 1 тик ≈ +1 мм.
4. Ребут с выдергиванием питания -> значение восстановилось (после сейва).

## Настройки при сборке — `include/config.h`
`CATCHMENT_AREA_MM2`, `TIP_VOLUME_ML_NUM/DEN`, `SAVE_STEP_UNITS`, `SENSOR_IS_REED`, `ADV_TX_POWER_DBM`,
`BAT_*`, `PIN_LED_ADV`, `PIN_LED_TICK`.
