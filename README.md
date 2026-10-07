# NRF-BLE-tick-counter

Плата: nRF52840, клон Nice!Nano / nRF Pro Micro (Adafruit UF2-bootloader 0.6.0,
диск `NICENANO`, `Board-ID: nRF52840-nicenano`, SoftDevice S140 6.1.1).
Прошивка: PlatformIO + Arduino (Adafruit nRF52, таргет `adafruit_feather_nrf52840`) + Bluefruit52.

## Что делает
- Считает тики: площадка **`017` (= P0.17 = Arduino 29)** на GND,
  геркон либо холл `DRV5032FADBZR` (open-drain), активный LOW, pull-up, FALLING.
  Антидребезг: холл 5мс / геркон 40мс (`SENSOR_IS_REED`).
- `scaled = raw / TICKS_PER_UNIT` (`50` = литры, `200` = км).
- Non-connectable advertising раз в 5 сек, пакет:
  `flags + mfg (company 0xFFFF, magic 0x5443, scaled u32 LE, batt_mV u16 LE, flags) + name "TC-01"`.
  flags: bit0 warn, bit1 urgent, bit2 reed_mode.
- Память: RAM + LittleFS `/tickcount.bin` (magic+CRC). Сейв при `+SAVE_STEP_UNITS`
  scaled, раз в 24ч, срочно при `VDD < BAT_URGENT_MV`. Переполнение u32 = wrap.
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
3. Замыкание `017-GND` = тик (красный + `TICK`), 50 тиков = +1 scaled.
4. Ребут с выдергиванием питания -> значение восстановилось (после сейва).

## Настройки при сборке — `include/config.h`
`TICKS_PER_UNIT`, `SAVE_STEP_UNITS`, `SENSOR_IS_REED`, `ADV_TX_POWER_DBM`,
`BAT_*`, `PIN_LED_ADV`, `PIN_LED_TICK`.
