// pn532SpiTest — PN532 을 SPI 로 걸어 모듈이 살아 있는지 가리는 시험용 스케치
//
// pn532Test(I2C) 의 짝이다. I2C 쪽에서 SCL 이 GND 로 물려 버스가 통째로 죽었을 때,
// 선도 핀도 프로토콜도 다른 SPI 로 걸어 보면 **모듈 자체가 살아 있는지** 가려진다.
//   · SPI 로 읽힌다  → PN532 는 멀쩡하다. I2C 핀/납땜만 고장 난 것이다.
//   · SPI 로도 죽었다 → 모듈이 갔거나 전원(VCC·GND)이 안 들어가 있다.
//
// 보드: ESP32-C3 Mini / SuperMini (네이티브 USB)
//
// ── 배선 ────────────────────────────────────────────────────────────
//   3V3 → VCC        GND → GND
//   GPIO4 → SCK      GPIO5 → MISO      GPIO6 → MOSI      GPIO7 → SS(NSS)
//   (GPIO10 → RSTPDN 은 선택 — 안 쓰면 PIN_RSTPDN 을 -1 로 둔다)
//
//   I2C 로 쓰던 GPIO4·5 를 그대로 쓰고 두 가닥만 더 꽂으면 된다.
//   **모듈 쪽은 실크스크린 이름(SCK·MISO·MOSI·SS)에 맞춰 꽂아라** — 보드마다
//   이 이름들이 어느 헤더에 있는지가 다르다. SDA/SCL 자리에 그대로 꽂으면 안 된다.
//
// ── 모드 스위치 ─────────────────────────────────────────────────────
//   Elechouse V3(빨간 보드)는 딥스위치 두 개로 고른다. 보통 이렇다 —
//   **반드시 보드 뒷면에 인쇄된 표로 확인하라.**
//     HSU  : 1=OFF 2=OFF
//     I2C  : 1=ON  2=OFF
//     SPI  : 1=OFF 2=ON      ← 이 스케치는 여기
//   스위치가 SPI 가 아니면 아무리 배선이 맞아도 MISO 는 조용하다.
//
// ── 무엇을 보고 판정하나 ────────────────────────────────────────────
//   1) [NFC] 준비됨 v1.6 이 뜨면 → 모듈 살아 있다. I2C 쪽만 고장. 끝.
//   2) raw 가 전부 00 + 당겨보기가 '떠 있음' → 선이 모듈 MISO 에 안 닿았다.
//      (하드웨어 SPI 는 MISO 에 풀업을 안 켠다 — 안 닿은 선은 FF 가 아니라 00 이다)
//   3) raw 가 전부 00 + 당겨보기가 'LOW 로 물려 있음' → 단락이거나 모듈이 죽었다.
//   4) raw 가 섞여 나오는데 ver 이 실패 → 배선은 닿았고 프로토콜이 어긋난 것.
//      soft 빌드(USE_SOFT_SPI 1)로 바꿔 보고, 선을 짧게 한다.
//
// ── 시리얼 명령 (115200) ────────────────────────────────────────────
//   help            명령 목록
//   stats           읽기/실패/모듈리셋 통계, 가동 시간
//   ver             PN532 펌웨어 버전을 한 번 읽는다(살아 있나 확인)
//   raw [n]         CS 를 내리고 바이트를 그대로 주고받아 MISO 에 뭐가 오나 본다(기본 8)
//   lines           SCK/MISO/MOSI/SS 선이 살아 있나
//   probe [초]      그 시간 동안 쉬지 않고 폴링하며 한 번 한 번을 다 찍는다(기본 8초)
//   rty <n>         카드찾기 재시도 횟수 (기본 0x10)
//   cpu <mhz>       CPU 클럭 (160/80 — USB CDC 때문에 80 아래로는 가지 말 것)
//   rst             RSTPDN 을 한 번 내렸다 올린다 (PIN_RSTPDN 을 잡아 뒀을 때만)
//   reinit          PN532 를 처음부터 다시 잡는다
//
// ── 빌드 ────────────────────────────────────────────────────────────
//   arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
//     --libraries ~/Documents/Arduino/libraries pn532SpiTest
//   arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
//     -p /dev/cu.usbmodem* pn532SpiTest

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <Adafruit_PN532.h>
#include "esp_system.h"
#include "esp_wifi.h"

// ── 설정 ────────────────────────────────────────────────────────────
#define PIN_SCK     4
#define PIN_MISO    5
#define PIN_MOSI    6
#define PIN_SS      7
#define PIN_RSTPDN -1        // 모듈 RSTPDN 을 잡았으면 10 등으로. 안 쓰면 -1

#define USE_SOFT_SPI  0      // 1 이면 비트뱅잉. 선이 길거나 하드웨어 SPI 가 수상할 때
#define SPI_HZ        1000000UL   // 라이브러리가 1MHz 고정이다 — raw 도 맞춰 둔다

#define CPU_MHZ_DEFAULT      80
#define HEALTH_EVERY_MS      3000
#define SAME_TAG_COOLDOWN_MS 1500

// ── 상태 ────────────────────────────────────────────────────────────
#if USE_SOFT_SPI
Adafruit_PN532 nfc(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);   // 소프트 SPI
#else
Adafruit_PN532 nfc(PIN_SS, &SPI);                          // 하드웨어 SPI
#endif

static bool     nfcReady     = false;
static uint32_t nfcVer       = 0;
static uint32_t bootAt       = 0;

static uint32_t cntPoll      = 0;
static uint32_t cntRead      = 0;
static uint32_t cntHealth    = 0;
static uint32_t cntHealthBad = 0;
static uint32_t cntReinit    = 0;
static uint32_t lastBadAt    = 0;

static char     lastUid[24]  = "";
static uint32_t lastUidAt    = 0;
static uint32_t healthAt     = 0;

// ── 리셋 원인 ───────────────────────────────────────────────────────
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

// ── RSTPDN ──────────────────────────────────────────────────────────
// SPI 생성자는 _reset 을 -1 로 두므로 라이브러리가 리셋을 안 한다. 직접 해 준다.
static void rstPulse() {
  if (PIN_RSTPDN < 0) { Serial.println("[리셋핀] 안 잡혀 있다 — PIN_RSTPDN 을 바꿔 빌드하라"); return; }
  digitalWrite(PIN_RSTPDN, LOW);
  delay(20);
  digitalWrite(PIN_RSTPDN, HIGH);
  delay(50);
  Serial.println("[리셋핀] RSTPDN 내렸다 올림");
}

// ── 선 점검 ─────────────────────────────────────────────────────────
// SPI 는 I2C 와 달리 풀업이 없다. 그래도 내부 풀다운/풀업으로 당겨 보면
// 단락(둘 다 0)과 남이 밀어 올리는 상태(둘 다 1)는 가려진다.
static void lineCheck() {
  const int8_t ps[] = {PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS};
  const char *nm[]  = {"SCK", "MISO", "MOSI", "SS"};
  for (int i = 0; i < 4; i++) {
    pinMode(ps[i], INPUT_PULLDOWN); delay(5);
    const int dn = digitalRead(ps[i]);
    pinMode(ps[i], INPUT_PULLUP);   delay(5);
    const int up = digitalRead(ps[i]);
    Serial.printf("[선] %s(GPIO%d) 풀다운=%d 풀업=%d → %s\n", nm[i], ps[i], dn, up,
                  (dn == 0 && up == 0) ? "LOW 로 붙잡혀 있음 — GND 단락이거나 남이 쥐고 있다"
                  : (dn == 1 && up == 1) ? "HIGH 로 밀려 있음 — 밖에서 당기고 있다"
                                         : "떠 있음(정상) — SPI 선은 풀업이 없으니 이게 맞다");
  }
  Serial.println("[선] SPI 는 이 판독만으로 연결을 못 가린다 — raw 로 MISO 에 뭐가 오나 봐라");
}

// ── 날것 SPI ────────────────────────────────────────────────────────
// CS 를 내린 채 0x00 을 밀어 넣고 되돌아오는 바이트를 그대로 찍는다.
// 전부 FF = MISO 가 떠 있다(안 닿음). 전부 00 = LOW 로 물렸다. 섞여 오면 뭔가 말하고 있다.
#if USE_SOFT_SPI
static uint8_t bitbang(uint8_t out) {          // LSB first, MODE0
  uint8_t in = 0;
  for (uint8_t b = 0; b < 8; b++) {
    digitalWrite(PIN_MOSI, (out >> b) & 1);
    delayMicroseconds(1);
    digitalWrite(PIN_SCK, HIGH);
    if (digitalRead(PIN_MISO)) in |= (1 << b);
    delayMicroseconds(1);
    digitalWrite(PIN_SCK, LOW);
  }
  return in;
}
#endif

// 전송 전에 MISO 를 내부 풀다운/풀업으로 당겨 본다. SPI 전송만으로는 "떠 있다" 와
// "LOW 로 물렸다" 를 못 가린다 — 하드웨어 SPI 가 MISO 에 풀업을 안 켜기 때문이다.
static void misoPullCheck() {
  pinMode(PIN_MISO, INPUT_PULLDOWN); delay(5);
  const int dn = digitalRead(PIN_MISO);
  pinMode(PIN_MISO, INPUT_PULLUP);   delay(5);
  const int up = digitalRead(PIN_MISO);
  // CS 가 HIGH 인 지금 PN532 는 MISO 를 하이임피던스로 놓는다. 그러니 '떠 있음' 은
  // 제대로 꽂혀 있어도 나오는 정상 판독이다 — 연결 여부의 근거로 쓰면 안 된다.
  // 여기서 잡히는 건 단락(둘 다 0)뿐이고, 연결 판정은 아래 주고받은 바이트로 한다.
  Serial.printf("[raw] MISO 당겨보기 풀다운=%d 풀업=%d → %s\n", dn, up,
                (dn == 0 && up == 0) ? "LOW 로 물려 있음 — GND 단락이거나 모듈이 쥐고 있다"
                : (dn == 1 && up == 1) ? "HIGH 로 밀려 있음 — 밖에서 당기고 있다(풀업이 보인다)"
                                       : "하이임피던스(정상) — CS 가 올라가 있으면 이게 맞다");
#if !USE_SOFT_SPI
  SPI.end();
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);   // 핀 모드를 건드렸으니 다시 잡는다
  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH);
#endif
}

static void rawProbe(uint8_t n) {
  if (n == 0 || n > 32) n = 8;
  uint8_t got[32];

  misoPullCheck();

#if USE_SOFT_SPI
  pinMode(PIN_SCK, OUTPUT);  digitalWrite(PIN_SCK, LOW);
  pinMode(PIN_MOSI, OUTPUT); digitalWrite(PIN_MOSI, LOW);
  pinMode(PIN_MISO, INPUT);
  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, LOW);
  delay(2);                                  // 깨우기 — CS 를 2ms 붙잡는다
  got[0] = bitbang(0x02);                    // SPI_STATREAD
  for (uint8_t i = 1; i < n; i++) got[i] = bitbang(0x00);
  digitalWrite(PIN_SS, HIGH);
#else
  SPI.beginTransaction(SPISettings(SPI_HZ, LSBFIRST, SPI_MODE0));
  digitalWrite(PIN_SS, LOW);
  delay(2);
  got[0] = SPI.transfer(0x02);               // SPI_STATREAD
  for (uint8_t i = 1; i < n; i++) got[i] = SPI.transfer(0x00);
  digitalWrite(PIN_SS, HIGH);
  SPI.endTransaction();
#endif

  Serial.print("[raw] 되돌아온 바이트:");
  uint8_t ff = 0, zz = 0;
  for (uint8_t i = 0; i < n; i++) {
    Serial.printf(" %02X", got[i]);
    if (got[i] == 0xFF) ff++;
    if (got[i] == 0x00) zz++;
  }
  Serial.println();
  if (zz == n)      Serial.println("[raw] 전부 00 — MISO 가 조용하다. 선이 안 닿았거나 모드 스위치가 SPI 가 "
                                   "아니거나 모듈에 전원이 없다 (ver 이 되면 이 판정은 무시해라)");
  else if (ff == n) Serial.println("[raw] 전부 FF — 아무도 MISO 를 안 몰고 있다(위 당겨보기가 남긴 풀업만 보인다). "
                                   "판정은 당겨보기 줄을 믿어라 — '떠 있음'이면 안 닿은 것이다");
  else              Serial.println("[raw] 섞여 온다 — 모듈이 말하고 있다. 배선은 닿았다");
}

// ── PN532 ───────────────────────────────────────────────────────────
static bool nfcBegin(bool quiet = false) {
  if (PIN_RSTPDN >= 0) rstPulse();
  nfc.begin();
  nfcVer   = nfc.getFirmwareVersion();
  nfcReady = nfcVer != 0;
  if (nfcReady) {
    nfc.SAMConfig();
    nfc.setPassiveActivationRetries(0x10);
    if (!quiet)
      Serial.printf("[NFC] PN532 준비됨 v%d.%d — 모듈은 살아 있다(I2C 쪽만 고장)\n",
                    (int)((nfcVer >> 16) & 0xFF), (int)((nfcVer >> 8) & 0xFF));
  } else if (!quiet) {
    Serial.println("[NFC] PN532 를 못 찾음 — 모드 스위치가 SPI 인지, SS/MISO/MOSI/SCK 가 맞는지 (raw / lines 로 점검)");
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
}

static void healthCheck() {
  cntHealth++;
  const uint32_t v = nfc.getFirmwareVersion();
  if (v) return;
  cntHealthBad++;
  lastBadAt = millis();
  Serial.printf("[감시] PN532 가 응답이 없다 (%lu번째 실패) — 다시 잡는다\n", (unsigned long)cntHealthBad);
  cntReinit++;
  if (nfcBegin(true)) Serial.println("[감시] 다시 살아남 — 모듈이 혼자 죽었다 살아난 것이다(전원/배선 의심)");
  else                Serial.println("[감시] 아직 죽어 있다");
}

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
    if (dt > 450) slow++;
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
  Serial.printf("[통계] PN532 %s · %s SPI %luHz · CPU %uMHz · 남은 힙 %lu바이트\n",
                nfcReady ? "살아 있음" : "죽음",
                USE_SOFT_SPI ? "소프트" : "하드웨어", (unsigned long)SPI_HZ,
                (unsigned)getCpuFrequencyMhz(), (unsigned long)ESP.getFreeHeap());
  if (cntHealthBad)
    Serial.printf("[통계] 마지막으로 모듈이 죽은 건 %lu초 전 — 전원을 의심하라\n",
                  (unsigned long)((millis() - lastBadAt) / 1000));
}

static void printHelp() {
  Serial.println("[명령] help · stats · ver · raw [n] · lines · probe [초] · rty <n> · cpu <mhz> · rst · reinit");
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
  } else if (!strncmp(s, "raw", 3)) {
    rawProbe(s[3] ? (uint8_t)strtoul(s + 3, nullptr, 10) : 8);
  } else if (!strcmp(s, "lines")) {
    lineCheck();
  } else if (!strncmp(s, "probe", 5)) {
    const uint32_t secs = s[5] ? strtoul(s + 5, nullptr, 10) : 8;
    nfcProbe(secs ? secs : 8);
  } else if (!strncmp(s, "rty ", 4)) {
    const uint8_t v = (uint8_t)strtol(s + 4, nullptr, 0);
    nfc.setPassiveActivationRetries(v);
    Serial.printf("[NFC] 카드찾기 재시도 = 0x%02X\n", v);
  } else if (!strncmp(s, "cpu ", 4)) {
    const uint32_t mhz = strtoul(s + 4, nullptr, 10);
    if (mhz < 80) { Serial.println("[CPU] 80MHz 아래로는 USB 시리얼이 끊긴다 — 거부"); return; }
    setCpuFrequencyMhz(mhz);
    Serial.printf("[CPU] %uMHz\n", (unsigned)getCpuFrequencyMhz());
  } else if (!strcmp(s, "rst")) {
    rstPulse();
  } else if (!strcmp(s, "reinit")) {
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
  delay(1500);
  Serial.println("\n=== pn532SpiTest — PN532 SPI (와이파이·블루투스 꺼짐) ===");
  printResetReason();

  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true, true);
  esp_wifi_stop();
  esp_wifi_deinit();
#if SOC_BT_SUPPORTED
  btStop();
#endif
  Serial.println("[전원] 와이파이·블루투스 내림 — 소비 전류는 CPU + PN532 뿐이다");

  setCpuFrequencyMhz(CPU_MHZ_DEFAULT);
  Serial.printf("[전원] CPU %uMHz · %s SPI %luHz\n", (unsigned)getCpuFrequencyMhz(),
                USE_SOFT_SPI ? "소프트" : "하드웨어", (unsigned long)SPI_HZ);
  Serial.printf("[배선] SCK=GPIO%d MISO=GPIO%d MOSI=GPIO%d SS=GPIO%d RSTPDN=%d\n",
                PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS, PIN_RSTPDN);

  if (PIN_RSTPDN >= 0) { pinMode(PIN_RSTPDN, OUTPUT); digitalWrite(PIN_RSTPDN, HIGH); }

  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH);
#if !USE_SOFT_SPI
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
#endif

  rawProbe(8);        // 라이브러리를 부르기 전에 MISO 에 뭐가 오나 먼저 본다
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
