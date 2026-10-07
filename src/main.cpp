#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "config.h"

// ---------- Состояние ----------
static volatile raw_t g_rawTicks = 0;
static volatile uint32_t g_lastIsrMs = 0;
static volatile bool g_tickEvent = false; // флаг для мигания красным в loop (в ISR только флаг)
static scaled_t g_lastSavedScaled = 0;
static uint32_t g_lastAdvMs = 0;
static uint32_t g_lastSaveMs = 0;
static uint16_t g_battMv = 0;
static uint8_t g_flags = 0;
static uint8_t g_lowWarnCnt = 0;
static uint8_t g_lowUrgCnt = 0;
static uint8_t g_advCount = 0;

using namespace Adafruit_LittleFS_Namespace;
static const char *COUNT_PATH = "/tickcount.bin";

struct PersistRec {
  uint32_t magic;   // 0x54434E54
  uint32_t raw;     // rawTicks
  uint32_t savedScaled;
  uint32_t crc;     // magic ^ raw ^ savedScaled
};

static uint32_t recCrc(const PersistRec &r) { return r.magic ^ r.raw ^ r.savedScaled; }

// ---------- VDD internal ----------
// analogReadVDD() возвращает СЫРЫЕ отсчеты SAADC (не мВ!):
// дефолт ядра: ref Internal 0.6В, gain 1/6 => шкала 3.6В.
// Форсируем 12 бит и пересчитываем сами: мВ = raw * 3600 / 4096.
static uint16_t readVddMv() {
  analogReadResolution(12);
  extern uint32_t analogReadVDD(void);
  uint32_t raw = analogReadVDD();
  uint32_t mv = (raw * 3600UL) / 4096UL;
  if (mv > 65535UL) mv = 65535UL;
  return (uint16_t)mv;
}

// ---------- Flash persistence (двойной слот не нужен на LittleFS: пишем атомарно через tmp+rename) ----------
static bool persistLoad(raw_t &rawOut, scaled_t &scaledOut) {
  File f(InternalFS);
  if (!InternalFS.begin()) return false;
  if (!f.open(COUNT_PATH, FILE_O_READ)) return false;
  PersistRec r{};
  if (f.read((uint8_t *)&r, sizeof(r)) != sizeof(r)) { f.close(); return false; }
  f.close();
  if (r.magic != 0x54434E54UL || recCrc(r) != r.crc) return false;
  rawOut = (raw_t)r.raw;
  scaledOut = (scaled_t)r.savedScaled;
  return true;
}

static bool persistSave(raw_t raw, scaled_t scaled) {
  PersistRec r{0x54434E54UL, (uint32_t)raw, (uint32_t)scaled, 0};
  r.crc = recCrc(r);
  File f(InternalFS);
  // LittleFS в Adafruit: пишем напрямую, файл маленький — операция быстрая
  if (!f.open(COUNT_PATH, FILE_O_WRITE)) return false;
  size_t n = f.write((uint8_t *)&r, sizeof(r));
  f.close();
  return n == sizeof(r);
}

// ---------- Счетчик ----------
static inline uint32_t debounceMs() {
#if SENSOR_IS_REED
  return DEBOUNCE_MS_REED;
#else
  return DEBOUNCE_MS_HALL;
#endif
}

static inline scaled_t toScaled(raw_t raw) {
  // Осадки в ЦЕЛЫХ мм (округление вниз):
  // rain_mm = raw * TIP_ML / (CATCH_MM2/1000) = raw * TIP_NUM*1000 / (TIP_DEN*CATCH_MM2).
  // 1 тип: 1*865*1000/(100*8220) = 865000/822000 = 1. Считаем в 64 битах.
  uint64_t v = (uint64_t)raw * (uint64_t)TIP_VOLUME_ML_NUM * 1000ULL;
  v /= (uint64_t)TIP_VOLUME_ML_DEN * (uint64_t)CATCHMENT_AREA_MM2;
  if (v > 0xFFFFFFFFULL) v = 0xFFFFFFFFULL; // wrap дальше обработает вызывающий
  return (scaled_t)v;
}

void onPulse() {
  uint32_t now = millis();
  uint32_t dt = now - g_lastIsrMs;
  if (dt < debounceMs()) return; // антидребезг
  g_lastIsrMs = now;
  g_rawTicks++; // wrap uint32 — ок, считаем дальше
  g_tickEvent = true;
}

// ---------- BLE adv payload ----------
static void buildMfgPayload(uint8_t *buf, scaled_t scaled, uint16_t battMv, uint8_t flags) {
  buf[0] = (uint8_t)(COMPANY_ID & 0xFF);
  buf[1] = (uint8_t)((COMPANY_ID >> 8) & 0xFF);
  buf[2] = (uint8_t)(MFG_MAGIC & 0xFF);
  buf[3] = (uint8_t)((MFG_MAGIC >> 8) & 0xFF);
  buf[4] = (uint8_t)(scaled & 0xFF);
  buf[5] = (uint8_t)((scaled >> 8) & 0xFF);
  buf[6] = (uint8_t)((scaled >> 16) & 0xFF);
  buf[7] = (uint8_t)((scaled >> 24) & 0xFF);
  buf[8] = (uint8_t)(battMv & 0xFF);
  buf[9] = (uint8_t)((battMv >> 8) & 0xFF);
  buf[10] = flags;
}

static void advUpdate() {
  scaled_t scaled = toScaled((raw_t)g_rawTicks);
  uint8_t payload[MFG_PAYLOAD_LEN];
  buildMfgPayload(payload, scaled, g_battMv, g_flags);

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, payload, sizeof(payload));
  Bluefruit.Advertising.addName(); // короткое ADV_NAME, чтобы найти в nRF Connect
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setInterval(ADV_INTERVAL_625US, ADV_INTERVAL_625US);
  Bluefruit.Advertising.setFastTimeout(0);
  Bluefruit.Advertising.start(0); // 0 = бесконечно

#if LED_BLINK_ON_ADV
  // Короткая вспышка в момент рассылки — видно что живое, средний ток мизерный
  pinMode(PIN_LED_ADV, OUTPUT);
  digitalWrite(PIN_LED_ADV, LED_ON);
  delay(ADV_BLINK_MS);
  digitalWrite(PIN_LED_ADV, LED_OFF);
#endif

  if (Serial) {
    Serial.print("ADV scaled=");
    Serial.print(scaled);
    Serial.print("mm raw=");
    Serial.print((uint32_t)g_rawTicks);
    Serial.print(" batt=");
    Serial.print(g_battMv);
    Serial.print("mV flags=0x");
    Serial.print(g_flags, HEX);
    Serial.print(" adv_running=");
    Serial.println(Bluefruit.Advertising.isRunning() ? "YES" : "NO");
  }
}

// ---------- Батарейка ----------
static void batteryPoll(bool force) {
  if (!force && (g_advCount % BAT_MEASURE_EVERY_ADV) != 0) return;
  uint16_t mv = readVddMv();
  g_battMv = mv;

  // warn
  if (mv < BAT_WARN_MV) { if (++g_lowWarnCnt >= BAT_LOW_CONSECUTIVE) g_flags |= FLAG_LOW_BATT_WARN; }
  else if (mv > (BAT_WARN_MV + BAT_HYST_MV)) { g_lowWarnCnt = 0; g_flags &= (uint8_t)~FLAG_LOW_BATT_WARN; }
  // urgent: внеочередной save
  if (mv < BAT_URGENT_MV) {
    if (++g_lowUrgCnt >= BAT_LOW_CONSECUTIVE) {
      g_flags |= FLAG_LOW_BATT_URGENT;
      scaled_t sc = toScaled((raw_t)g_rawTicks);
      if (persistSave((raw_t)g_rawTicks, sc)) { g_lastSavedScaled = sc; g_lastSaveMs = millis(); }
    }
  } else if (mv > (BAT_URGENT_MV + BAT_HYST_MV)) {
    g_lowUrgCnt = 0;
    g_flags &= (uint8_t)~FLAG_LOW_BATT_URGENT;
  }
}

// ---------- Setup / loop ----------
void setup() {
#if ENABLE_VCC_CUT
  // Отрубить VCC-периферию (светодиоды) на Nice!Nano. На Feather пин обычный GPIO — безопасно как OUTPUT HIGH коротко? Guard: делаем только если плата отвечает за VCC-cut.
  // Закомментировать если клон без MOSFET!
  pinMode(VCC_CUT_PIN, OUTPUT);
  digitalWrite(VCC_CUT_PIN, HIGH);
#endif

  // Берем светодиоды под свой контроль и гасим.
  // Карта Feather не совпадает с разводкой клона, поэтому родные LED_RED/LED_BLUE
  // ядра не используем — только явные PIN_LED_ADV / PIN_LED_TICK из config.h.
  pinMode(PIN_LED_ADV, OUTPUT);
  digitalWrite(PIN_LED_ADV, LED_OFF);
  pinMode(PIN_LED_TICK, OUTPUT);
  digitalWrite(PIN_LED_TICK, LED_OFF);

  pinMode(SENSOR_PIN, INPUT_PULLUP);
#if SENSOR_IS_REED
  g_flags |= FLAG_REED_MODE;
#endif
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), onPulse, FALLING);

  // USB Serial только для отладки, на батарейке не висеть
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < USB_SERIAL_TIMEOUT_MS)) { delay(10); }

  // Восстановить счетчик
  raw_t raw = 0; scaled_t sc = 0;
  if (persistLoad(raw, sc)) {
    g_rawTicks = raw;
    g_lastSavedScaled = sc;
  }

  // DC/DC для экономии
  NRF_POWER->DCDCEN = 1;

  // ВАЖНО: begin(1,0), а не (0,0)! С нулем peripheral-ролей SoftDevice не
  // конфигурирует CONN_CFG_PERIPHERAL и sd_ble_gap_adv_start молча падает —
  // advertising не стартует вообще. 1 слот никого не пускает: тип у нас
  // non-connectable (см. setType ниже), слот просто висит резервом.
  if (!Bluefruit.begin(1, 0)) {
    if (Serial) Serial.println("FATAL: Bluefruit.begin failed");
    while (1) {
      digitalWrite(PIN_LED_TICK, LED_ON);
      delay(200);
      digitalWrite(PIN_LED_TICK, LED_OFF);
      delay(200);
    }
  }
  Bluefruit.autoConnLed(false); // выкл авто-мигание CONN-светодиодом (2 Гц при adv) — рулим сами
  Bluefruit.setTxPower(ADV_TX_POWER_DBM);
  Bluefruit.setName(ADV_NAME);
  // Наш план: только рассылка, подключиться нельзя
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);

  g_battMv = readVddMv();
  batteryPoll(true);

  if (Serial) {
    Serial.println("== TC-01 boot ==");
    Serial.print("restored raw=");
    Serial.print((uint32_t)g_rawTicks);
    Serial.print(" savedScaled=");
    Serial.print(g_lastSavedScaled);
    Serial.println("mm");
    Serial.print("VDD=");
    Serial.print(g_battMv);
    Serial.println("mV");
    Serial.print("CALIB: tip=");
    Serial.print(TIP_VOLUME_ML_NUM);
    Serial.print('/');
    Serial.print(TIP_VOLUME_ML_DEN);
    Serial.print("ml catchment=");
    Serial.print(CATCHMENT_AREA_MM2);
    Serial.println("mm2");
  }

  advUpdate();
  g_lastAdvMs = millis();
  g_lastSaveMs = millis();
}

void loop() {
  uint32_t now = millis();

  // 0. Вспышка красным на каждый тик (флаг из ISR, само мигание тут — в ISR нельзя delay)
#if RED_BLINK_ON_TICK
  if (g_tickEvent) {
    g_tickEvent = false;
    digitalWrite(PIN_LED_TICK, LED_ON);
    delay(TICK_BLINK_MS);
    digitalWrite(PIN_LED_TICK, LED_OFF);
    if (Serial) { Serial.print("TICK raw="); Serial.println((uint32_t)g_rawTicks); }
  }
#endif

  // 1. Adv раз в 5 сек (по таймеру, не чаще)
  if (now - g_lastAdvMs >= 5000UL) {
    g_advCount++;
    batteryPoll(false);
    advUpdate();
    g_lastAdvMs = now;
  }

  // 2. Save: по приросту scaled на SAVE_STEP + heartbeat 24ч
  scaled_t sc = toScaled((raw_t)g_rawTicks);
  bool stepHit = (sc >= g_lastSavedScaled) && ((sc - g_lastSavedScaled) >= SAVE_STEP_UNITS);
  // wrap scaled (раз в 136 лет при 1л/сек) — тоже сейв
  if (sc < g_lastSavedScaled) stepHit = true;
  if (stepHit || (now - g_lastSaveMs >= SAVE_HEARTBEAT_MS)) {
    if (persistSave((raw_t)g_rawTicks, sc)) {
      g_lastSavedScaled = sc;
      g_lastSaveMs = now;
      if (Serial) { Serial.print("SAVE scaled="); Serial.print(sc); Serial.println("mm"); }
    } else if (Serial) {
      Serial.println("SAVE FAILED");
    }
  }

  // 3. Сон до следующего события: тик (GPIOTE) или таймер. delay(100) в Adafruit = __WFE, ток мкА.
  // Не использовать busy-wait!
  delay(100);
}
