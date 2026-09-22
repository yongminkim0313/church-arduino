// pn532Test — PN532 만 돌려 전원이 버티는지 보는 시험용 스케치
//
// nfcProjectClient 에서 와이파이·mDNS·WebSocket·HTTP(TLS)·부저를 모두 뺐다.
// 남은 건 I2C + PN532 폴링뿐이다. 여기서 멀쩡히 읽히면 문제는 NFC 가 아니라
// 무선 송신 순간의 전류 피크(3V3 레귤레이터·USB 케이블·배선)다.
//
// 보드: ESP32-C3 Mini / SuperMini (네이티브 USB)
// 배선: 3V3→VCC · GPIO4→SDA · GPIO5→SCL · GPIO6→IRQ(안 써도 됨) · GND→GND
//       PN532 VCC·GND 2cm 안에 100µF 전해 + 0.1µF 세라믹 병렬 — 없으면 태그 순간 리셋된다.
//
// ── 전원 문제를 어떻게 가려내나 ─────────────────────────────────────
//   1) 켤 때 찍히는 [리셋] 이 'Brownout(전압 강하)' 이면 → 전원이 범인. 확정.
//   2) [감시] 에서 PN532 버전 읽기가 중간에 실패하면 → 모듈이 혼자 죽었다 살아난 것. 전원/배선.
//   3) 카드를 댈 때만 죽으면 → 태그 여자(勵磁) 전류 피크. 커패시터를 키운다.
//   4) 이 스케치로는 멀쩡한데 본 스케치에서 죽으면 → 와이파이 송신 피크. 전원부를 손봐야 한다.
//
// ── 시리얼 명령 (115200) ────────────────────────────────────────────
//   help            명령 목록
//   stats           읽기/실패/모듈리셋 통계, 가동 시간
//   ver             PN532 펌웨어 버전을 한 번 읽는다(살아 있나 확인)
//   scan            I2C 버스 훑기 (PN532 = 0x24)
//   lines           SDA/SCL/IRQ 선이 살아 있나
//   probe [초]      그 시간 동안 쉬지 않고 폴링하며 한 번 한 번을 다 찍는다(기본 8초)
//   rty <n>         카드찾기 재시도 횟수 (기본 0x10, 작을수록 I2C 를 짧게 잡는다)
//   i2c <hz>        I2C 클럭 (100000 / 400000 — 선이 길면 100k 로 낮춘다)
//   cpu <mhz>       CPU 클럭 (160/80 — 낮추면 소비 전류가 준다. USB CDC 때문에 80 아래로는 가지 말 것)
//   beep            부저를 한 번 울려 본다 (PIN_BUZZER 를 켜 뒀을 때만)
//   reinit          PN532 를 처음부터 다시 잡는다
//
// ── 빌드 ────────────────────────────────────────────────────────────
//   arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
//     --libraries ~/Documents/Arduino/libraries pn532Test
//   arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app -p /dev/cu.usbmodem* pn532Test

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_PN532.h>
#include "esp_system.h"
#include "esp_wifi.h"

// ── 설정 ────────────────────────────────────────────────────────────
#define PIN_NFC_SDA   4
#define PIN_NFC_SCL   5
#define PIN_NFC_IRQ   6        // 안 쓰더라도 선 상태를 보려고 잡아 둔다
#define PIN_BUZZER   -1        // 시험 중엔 꺼 둔다(-1). 부저 전류까지 보고 싶으면 7 로.
#define BUZZER_ACTIVE 0

#define I2C_HZ_DEFAULT      100000UL   // 전원이 의심스러울 땐 100k 가 안전하다
#define CPU_MHZ_DEFAULT     80         // USB CDC 가 도는 최저 클럭. 전류를 줄여 본다
#define HEALTH_EVERY_MS     3000       // 이 간격으로 PN532 가 살아 있나 확인
#define SAME_TAG_COOLDOWN_MS 1500

// ── 상태 ────────────────────────────────────────────────────────────
Adafruit_PN532 nfc(255, 255, &Wire);   // IRQ·RST 안 씀 — 라이브러리 블로킹 폴링만 쓴다

static uint32_t i2cHz        = I2C_HZ_DEFAULT;
static bool     nfcReady     = false;
static uint32_t nfcVer       = 0;
static uint32_t bootAt       = 0;

static uint32_t cntPoll      = 0;      // 폴링 횟수
static uint32_t cntRead      = 0;      // 카드 읽은 횟수
static uint32_t cntHealth    = 0;      // 감시 횟수
static uint32_t cntHealthBad = 0;      // 감시 중 버전 읽기 실패 — 모듈이 죽었다 살아난 흔적
static uint32_t cntReinit    = 0;      // 다시 잡은 횟수
static uint32_t lastBadAt    = 0;      // 마지막으로 모듈이 죽은 시각

static char     lastUid[24]  = "";
static uint32_t lastUidAt    = 0;
static uint32_t healthAt     = 0;

// ── 부저 (기본 꺼짐) ────────────────────────────────────────────────
static void beep(uint16_t hz, uint16_t ms) {
  if (PIN_BUZZER < 0) return;
#if BUZZER_ACTIVE
  digitalWrite(PIN_BUZZER, HIGH); delay(ms); digitalWrite(PIN_BUZZER, LOW);
#else
  tone(PIN_BUZZER, hz); delay(ms); noTone(PIN_BUZZER);
#endif
}

// ── 리셋 원인 ───────────────────────────────────────────────────────
// 전원이 범인이면 여기서 바로 드러난다 — Brownout 은 3V3 이 임계 아래로 꺼진 것이다.
static void printResetReason() {
  const esp_reset_reason_t r = esp_reset_reason();
  const char *why;
  switch (r) {
    case ESP_RST_POWERON:  why = "전원 인가(정상)";                          break;
    case ESP_RST_SW:       why = "소프트웨어 재시작";                        break;
    case ESP_RST_PANIC:    why = "펌웨어 예외(패닉) — 코드 문제";            break;
    case ESP_RST_INT_WDT:  why = "인터럽트 와치독";                          break;
    case ESP_RST_TASK_WDT: why = "태스크 와치독";                            break;
    case ESP_RST_WDT:      why = "와치독";                                   break;
    case ESP_RST_BROWNOUT: why = "*** Brownout(전압 강하) — 전원이 범인 ***"; break;
    case ESP_RST_USB:      why = "USB 재시작(업로드·포트 열기)";             break;
    case ESP_RST_EXT:      why = "외부 리셋 핀";                             break;
    case ESP_RST_DEEPSLEEP:why = "딥슬립에서 깸";                            break;
    default:               why = "알 수 없음";                               break;
  }
  Serial.printf("[리셋] %s (code=%d)\n", why, (int)r);
}

// ── I2C ─────────────────────────────────────────────────────────────
// PN532 는 응답이 준비될 때까지 SCL 을 붙잡는다(클럭 스트레칭). ESP32 기본 타임아웃 50ms 로는
// 중간에 포기해 버리고, 그러면 모듈이 SCL 을 쥔 채 버스가 물려 전원을 내리기 전엔 안 풀린다.
static void wireStart() {
  Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL, i2cHz);
  Wire.setTimeOut(1000);
}

static void i2cScan() {
  int found = 0;
  Serial.printf("[I2C] SDA=%d SCL=%d @%luHz 훑는 중:", PIN_NFC_SDA, PIN_NFC_SCL, (unsigned long)i2cHz);
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
  }
  Serial.println(found ? "" : " 아무것도 없음 — 전원(3V3·GND)·모드 스위치(I2C) 확인");
}

// 선이 살아 있는지 — 내부 풀다운(약 45kΩ)으로 당겨 두고 읽는다. 모듈이 켜져 있고 선이
// 이어져 있으면 모듈 풀업(보통 4.7kΩ)이 이겨 HIGH, 끊겼거나 모듈 전원이 없으면 LOW.
static void lineCheck() {
  Wire.end();
  const int8_t ps[] = {PIN_NFC_SDA, PIN_NFC_SCL, PIN_NFC_IRQ};
  const char *nm[]  = {"SDA", "SCL", "IRQ"};
  for (int i = 0; i < 3; i++) {
    pinMode(ps[i], INPUT_PULLDOWN); delay(5);
    const int dn = digitalRead(ps[i]);
    pinMode(ps[i], INPUT_PULLUP);   delay(5);
    const int up = digitalRead(ps[i]);
    Serial.printf("[선] %s(GPIO%d) 풀다운=%d 풀업=%d → %s\n", nm[i], ps[i], dn, up,
                  dn ? "이어짐(모듈 풀업이 보인다)"
                     : up ? "떠 있음 — 선이 안 닿았다"
                          : "LOW 로 붙잡혀 있음 — GND 에 닿았거나 모듈이 버스를 쥐고 있다");
  }
  pinMode(PIN_NFC_IRQ, INPUT_PULLUP);
  wireStart();
}

// ── PN532 ───────────────────────────────────────────────────────────
static bool nfcBegin(bool quiet = false) {
  nfc.begin();
  nfcVer   = nfc.getFirmwareVersion();
  nfcReady = nfcVer != 0;
  if (nfcReady) {
    nfc.SAMConfig();
    nfc.setPassiveActivationRetries(0x10);
    if (!quiet)
      Serial.printf("[NFC] PN532 준비됨 v%d.%d\n", (int)((nfcVer >> 16) & 0xFF), (int)((nfcVer >> 8) & 0xFF));
  } else if (!quiet) {
    Serial.println("[NFC] PN532 를 못 찾음 — 배선·전원·I2C 모드 스위치 확인 (scan / lines 로 점검)");
  }
  return nfcReady;
}

static void uidToHex(const uint8_t *u, uint8_t len, char *out, size_t cap) {
  size_t n = 0;
  for (uint8_t i = 0; i < len && n + 2 < cap; i++) n += snprintf(out + n, cap - n, "%02X", u[i]);
  out[n] = '\0';
}

static void gotUid(const uint8_t *uid, uint8_t len, uint32_t took) {
  char hex[48];
  uidToHex(uid, len > 16 ? 16 : len, hex, sizeof(hex));
  if (!strcmp(hex, lastUid) && millis() - lastUidAt < SAME_TAG_COOLDOWN_MS) {
    lastUidAt = millis();
    return;
  }
  strncpy(lastUid, hex, sizeof(lastUid) - 1);
  lastUidAt = millis();
  cntRead++;
  Serial.printf("[태그] %s (%u바이트, %lums)%s\n", hex, (unsigned)len, (unsigned long)took,
                (len == 4) ? " — 휴대폰·교통카드" : "");
  beep(2700, 60);
}

// 살아 있나 — 버전을 물어 본다. 실패하면 모듈이 죽은 것이니 다시 잡는다.
// 카드를 대는 순간에만 실패가 몰린다면 태그 여자 전류 피크가 원인이다.
static void healthCheck() {
  cntHealth++;
  const uint32_t v = nfc.getFirmwareVersion();
  if (v) return;
  cntHealthBad++;
  lastBadAt = millis();
  Serial.printf("[감시] PN532 가 응답이 없다 (%lu번째 실패) — 다시 잡는다\n", (unsigned long)cntHealthBad);
  Wire.end();
  wireStart();
  cntReinit++;
  if (nfcBegin(true)) Serial.println("[감시] 다시 살아남 — 모듈이 혼자 죽었다 살아난 것이다(전원/배선 의심)");
  else                Serial.println("[감시] 아직 죽어 있다");
}

// 그 시간 동안 쉬지 않고 폴링하며 한 번 한 번의 결과와 걸린 시간을 다 찍는다.
static void nfcProbe(uint32_t secs) {
  if (!nfcReady) { Serial.println("[프로브] PN532 가 없다"); return; }
  Serial.printf("[프로브] %lu초 동안 직접 폴링 — 지금 카드를 대세요\n", (unsigned long)secs);
  const uint32_t end = millis() + secs * 1000;
  uint32_t tries = 0, hits = 0, slow = 0, maxMs = 0;
  while ((int32_t)(millis() - end) < 0) {
    uint8_t uid[255] = {0};
    uint8_t len = 0;
    const uint32_t t0 = millis();
    const bool ok = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 400);
    const uint32_t dt = millis() - t0;
    tries++;
    if (dt > maxMs) maxMs = dt;
    if (dt > 450) slow++;                    // 타임아웃보다 오래 걸렸다 = 모듈이 버스를 붙잡았다
    if (ok) {
      hits++;
      char hex[48];
      uidToHex(uid, len > 16 ? 16 : len, hex, sizeof(hex));
      Serial.printf("[프로브] 읽음 len=%u uid=%s (%lums)\n", (unsigned)len, hex, (unsigned long)dt);
    }
  }
  Serial.printf("[프로브] 끝 — 시도 %lu · 읽음 %lu · 느림 %lu · 최대 %lums\n",
                (unsigned long)tries, (unsigned long)hits, (unsigned long)slow, (unsigned long)maxMs);
}

static void printStats() {
  const uint32_t up = (millis() - bootAt) / 1000;
  Serial.printf("[통계] 가동 %lu초 · 폴링 %lu · 읽음 %lu · 감시 %lu(실패 %lu) · 재초기화 %lu\n",
                (unsigned long)up, (unsigned long)cntPoll, (unsigned long)cntRead,
                (unsigned long)cntHealth, (unsigned long)cntHealthBad, (unsigned long)cntReinit);
  Serial.printf("[통계] PN532 %s · I2C %luHz · CPU %uMHz · 남은 힙 %lu바이트\n",
                nfcReady ? "살아 있음" : "죽음", (unsigned long)i2cHz,
                (unsigned)getCpuFrequencyMhz(), (unsigned long)ESP.getFreeHeap());
  if (cntHealthBad)
    Serial.printf("[통계] 마지막으로 모듈이 죽은 건 %lu초 전 — 전원을 의심하라\n",
                  (unsigned long)((millis() - lastBadAt) / 1000));
}

static void printHelp() {
  Serial.println("[명령] help · stats · ver · scan · lines · probe [초] · rty <n> · i2c <hz> · cpu <mhz> · beep · reinit");
}

// ── 시리얼 ──────────────────────────────────────────────────────────
static char   lineBuf[64];
static size_t lineLen = 0;

static void handleLine(char *s) {
  while (*s == ' ') s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = '\0';
  if (!n) return;

  if (!strcmp(s, "help")) {
    printHelp();
  } else if (!strcmp(s, "stats")) {
    printStats();
  } else if (!strcmp(s, "ver")) {
    const uint32_t v = nfc.getFirmwareVersion();
    if (v) Serial.printf("[NFC] v%d.%d — 살아 있다\n", (int)((v >> 16) & 0xFF), (int)((v >> 8) & 0xFF));
    else   Serial.println("[NFC] 응답 없음 — 죽었다");
  } else if (!strcmp(s, "scan")) {
    i2cScan();
  } else if (!strcmp(s, "lines")) {
    lineCheck();
  } else if (!strncmp(s, "probe", 5)) {
    const uint32_t secs = s[5] ? strtoul(s + 5, nullptr, 10) : 8;
    nfcProbe(secs ? secs : 8);
  } else if (!strncmp(s, "rty ", 4)) {
    const uint8_t v = (uint8_t)strtol(s + 4, nullptr, 0);
    nfc.setPassiveActivationRetries(v);
    Serial.printf("[NFC] 카드찾기 재시도 = 0x%02X\n", v);
  } else if (!strncmp(s, "i2c ", 4)) {
    i2cHz = strtoul(s + 4, nullptr, 10);
    Wire.end();
    wireStart();
    Serial.printf("[I2C] %luHz 로 다시 열고 PN532 를 다시 잡는다\n", (unsigned long)i2cHz);
    nfcBegin();
  } else if (!strncmp(s, "cpu ", 4)) {
    const uint32_t mhz = strtoul(s + 4, nullptr, 10);
    if (mhz < 80) { Serial.println("[CPU] 80MHz 아래로는 USB 시리얼이 끊긴다 — 거부"); return; }
    setCpuFrequencyMhz(mhz);
    Serial.printf("[CPU] %uMHz\n", (unsigned)getCpuFrequencyMhz());
  } else if (!strcmp(s, "beep")) {
    if (PIN_BUZZER < 0) Serial.println("[부저] 꺼져 있다 — PIN_BUZZER 를 7 로 바꿔 빌드하라");
    else { Serial.println("[부저] 삑"); beep(2700, 90); }
  } else if (!strcmp(s, "reinit")) {
    Wire.end();
    wireStart();
    cntReinit++;
    nfcBegin();
  } else {
    Serial.printf("[?] 모르는 명령: %s\n", s);
    printHelp();
  }
}

static void serialService() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n') { lineBuf[lineLen] = '\0'; handleLine(lineBuf); lineLen = 0; }
    else if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
  }
}

// ── setup / loop ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1500);                                  // USB CDC 가 붙을 때까지 — 첫 줄을 놓치지 않으려고
  Serial.println("\n=== pn532Test — PN532 단독 (와이파이·블루투스 꺼짐) ===");
  printResetReason();

  // 무선을 확실히 내린다 — 이 스케치의 존재 이유다.
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true, true);
  esp_wifi_stop();          // 이미 내려가 있으면 NOT_INIT 을 돌려줄 뿐 — 무시해도 된다
  esp_wifi_deinit();
#if SOC_BT_SUPPORTED
  btStop();
#endif
  Serial.println("[전원] 와이파이·블루투스 내림 — 이제 소비 전류는 CPU + PN532 뿐이다");

  setCpuFrequencyMhz(CPU_MHZ_DEFAULT);
  Serial.printf("[전원] CPU %uMHz · I2C %luHz\n", (unsigned)getCpuFrequencyMhz(), (unsigned long)i2cHz);

  if (PIN_BUZZER >= 0) { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
  pinMode(PIN_NFC_IRQ, INPUT_PULLUP);

  wireStart();
  i2cScan();
  nfcBegin();

  bootAt   = millis();
  healthAt = millis() + HEALTH_EVERY_MS;
  printHelp();
  Serial.println("[시작] 카드를 대세요. 30초마다 통계를 찍는다.");
}

void loop() {
  static uint32_t statsAt = 0;

  if (!nfcReady) {
    static uint32_t retryAt = 0;
    if ((int32_t)(millis() - retryAt) >= 0) {
      retryAt = millis() + 3000;
      Wire.end();
      wireStart();
      nfcBegin(true);
      if (nfcReady) Serial.println("[NFC] 다시 살아남");
    }
  } else {
    uint8_t uid[255] = {0};                     // 라이브러리가 응답의 길이 바이트를 믿고 복사한다 — 넉넉히
    uint8_t len = 0;
    const uint32_t t0 = millis();
    cntPoll++;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 60)) gotUid(uid, len, millis() - t0);

    if ((int32_t)(millis() - healthAt) >= 0) {
      healthAt = millis() + HEALTH_EVERY_MS;
      healthCheck();
    }
  }

  if ((int32_t)(millis() - statsAt) >= 0) {
    statsAt = millis() + 30000;
    printStats();
  }

  serialService();
  delay(5);
}
