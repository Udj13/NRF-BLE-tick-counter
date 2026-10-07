#pragma once
#include <stdint.h>

// ============ Железо ============
// Универсальный вход: геркон на GND либо холл DRV5032FADBZR (open-drain).
// Активный LOW, внутренняя pull-up.
// Плата-клон разведена как Nice!Nano (Pro Micro), а прошивка собрана под карту
// Feather — поэтому берем ногу по имени nRF-порта: площадка "017" = P0.17 = Arduino 29.
// Датчик: SIG -> "017", GND -> GND, холлу еще VDD -> 3V3.
#define SENSOR_PIN 29
#define SENSOR_ACTIVE_LOW 1

// Тип датчика для антидребезга: 0 = холл DRV5032 (чистый), 1 = геркон (дребезг)
#define SENSOR_IS_REED 0
#define DEBOUNCE_MS_REED 40
#define DEBOUNCE_MS_HALL 5

// Nice!Nano: P0.13 HIGH отключает питание VCC-периферии (светодиоды). На чистом Feather не определен — guard в коде.
#define VCC_CUT_PIN 13
#define ENABLE_VCC_CUT 0 // выкл: карта пинов Feather, P0.13 там = SPI MOSI, не трогаем

// ============ Калибровка осадкомера (tipping bucket) ============
// Водосбор: 66.6 x 129 мм = 8591.4 мм² минус 4 скругления r=20.8
// (4r² - πr² = 371.39) => эффективная площадь 8220 мм².
// 1 мм осадков на 8220 мм² = 8.22 мл.
// Объем опрокидывания: 500 мл / 57.8 типов = 8.65 мл.
// => 1 тип = 8.65 / 8.22 = 1.0523 мм.
// Подстройка винтом под ровно 8.22 мл/тип даст 1.00 мм/тип.
// Перекалибровка шприцем — правим только TIP_VOLUME_ML_*.
// scaled в эфире и во flash — ЦЕЛЫЕ мм (округление вниз): 1 тип = 1 мм.
#define CATCHMENT_AREA_MM2 8220UL
#define TIP_VOLUME_ML_NUM 865UL   // 8.65 мл
#define TIP_VOLUME_ML_DEN 100UL
// Сохранять во Flash при приросте scaled на SAVE_STEP_UNITS мм (1 = каждый мм)
#define SAVE_STEP_UNITS 1UL
// + принудительный heartbeat-save раз в сутки
#define SAVE_HEARTBEAT_MS (24UL * 3600UL * 1000UL)

// Счетчики 32-бит, переполнение = естественный wrap, считаем дальше
typedef uint32_t raw_t;
typedef uint32_t scaled_t;

// ============ BLE Advertising ============
// Non-connectable, только рассылка. Интервал 5 сек = 8000 * 0.625 мс
#define ADV_INTERVAL_625US 8000
#define ADV_TX_POWER_DBM 0  // потом можно -12 для экономии
#define ADV_NAME "TC-01"   // короткое имя, чтобы найти в nRF Connect
#define COMPANY_ID 0xFFFF  // тестовый; свой получить позже
#define MFG_MAGIC 0x5443   // "TC" tick-counter

// Формат MFG payload (little-endian):
// [0..1] company 0xFFFF | [2..3] magic 0x5443 | [4..7] rain u32, целые мм | [8..9] batt_mV u16 | [10] flags
#define MFG_PAYLOAD_LEN 11
#define FLAG_LOW_BATT_WARN (1u << 0)
#define FLAG_LOW_BATT_URGENT (1u << 1)
#define FLAG_REED_MODE (1u << 2)

// ============ Питание 2xAA L91 ============
// Замер только internal VDD, внешний делитель НЕ используем (иначе +20мкА)
#define BAT_WARN_MV 2400   // пора готовить замену
#define BAT_URGENT_MV 2200 // срочный save
#define BAT_HYST_MV 50
#define BAT_LOW_CONSECUTIVE 3  // севшим считаем после 3 подряд ниже порога
#define BAT_MEASURE_EVERY_ADV 12 // мерить VDD раз в 12 adv = раз в минуту

// ============ Светодиод ============
// Клон разведен как nRF Pro Micro / Nice!Nano, а карта пинов у нас Feather,
// поэтому родные LED_BLUE/LED_RED ядра бьют мимо.
// Факт по этому классу плат (Zephyr promicro_nrf52840, ArduinoNRF):
//   - синий = P1.10 = Arduino 4 (на части клонов P0.15, проверим)
//   - второй LED = P0.15 = Arduino 24 (кандидат на красный)
// Плюс Bluefruit52 по умолчанию сам мигает CONN-светодиодом при advertising
// (те самые 2 раза в секунду) — отключаем autoConnLed и рулим сами.
// Синий — вспышка в момент advertising, красный — вспышка на тик.
// Если цвета перепутаны или полярность инверсная — сказать, поменяю.
#define LED_BLINK_ON_ADV 1
#define RED_BLINK_ON_TICK 1
#define PIN_LED_ADV 4   // P1.10
#define PIN_LED_TICK 24 // P0.15
#define LED_ON HIGH
#define LED_OFF LOW
#define ADV_BLINK_MS 50
#define TICK_BLINK_MS 30

// ============ Отладка ============
#define USB_SERIAL_TIMEOUT_MS 1500 // не висеть на батарейке без USB
