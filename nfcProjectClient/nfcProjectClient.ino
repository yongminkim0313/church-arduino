// nfcProjectClient — PN532 로 카드를 읽어 nfcProject 원형 디스플레이에 보내는 ESP32-C3 리더
//
// 보드: ESP32-C3 Mini / SuperMini (네이티브 USB) — 화면 없음. PN532 와 부저만 단다.
//       (아래 '디스플레이' 는 이 보드가 아니라 와이파이 건너편의 nfcProject 원형 화면이다)
// PN532: I2C — 3V3→VCC · GPIO4→SDA · GPIO5→SCL · GPIO6→IRQ · GND→GND (config.h)
// 부저: GPIO7 → (+), GND → (−)  — 수동/능동은 config.h 의 BUZZER_ACTIVE
//   삑(높게 한 번)      디스플레이가 아는 키링으로 처리했다
//   삐-삐(낮게 두 번)   서버에 없는 카드 · 디스플레이에 못 보냄 · 답이 없음
//   삐리(올라가는 두 음) 켜져서 PN532 가 준비됨
//        PN532 VCC·GND 바로 옆에 100µF 전해 + 0.1µF 세라믹을 병렬로 — 없으면 태그 순간 리셋된다.
//
// 이 리더가 하는 일은 **읽어서 넘기는 것까지**다 —
//   1) config.h 의 와이파이에 붙는다
//   2) 디스플레이를 mDNS(nfc-display.local)로 찾아 ws://<IP>:81/ 에 WebSocket 으로 붙는다
//   3) 카드를 대면 UID(대문자 hex, 예: 04CE1B53D12A81)를 읽어
//   4) 디스플레이에 {"uid":"…"} 를 보낸다
//   5) 디스플레이가 돌려주는 {"mode":"nfc","name":…,"points":…,"known":…,"note":…} 로 소리를 고른다
//
// 이름·잔액 조회와 출석 지급은 **디스플레이(nfcProject)가 한다** — 이 리더는 서버와 말하지 않는다.
// 서버와 이야기하는 쪽을 하나로 모아야 한 태깅이 두 번 처리되지 않는다.
// 그래서 config.h 에 TALENT_SERVER·TALENT_DEVICE_KEY 가 없다.
//
// ── 시리얼 명령 (115200, 한 줄씩) ───────────────────────────────────
//   <UID>              카드를 댄 것처럼 처리한다(예: 04CE1B53D12A81) — 카드 없이 시험할 때
//   next / prev / enable / toggle   디스플레이 화면 넘기기 명령을 그대로 보낸다
//   status             연결 상태를 찍는다
//   scan               I2C 버스를 훑는다(PN532 = 0x24)
//   beep               부저 소리 세 가지를 차례로 낸다
//
// ── 빌드 ────────────────────────────────────────────────────────────
//   arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app nfcProjectClient
//   라이브러리: Adafruit PN532 (+ Adafruit BusIO) · ArduinoJson · WebSockets(by Markus Sattler)

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <Adafruit_PN532.h>
#include "config.h"

// ── 부저 ────────────────────────────────────────────────────────────
static void beep(uint16_t hz, uint16_t ms) {
  if (PIN_BUZZER < 0) return;
#if BUZZER_ACTIVE
  digitalWrite(PIN_BUZZER, HIGH); delay(ms); digitalWrite(PIN_BUZZER, LOW);
#else
  tone(PIN_BUZZER, hz); delay(ms); noTone(PIN_BUZZER);
#endif
}
static void sndOk()    { beep(2700, 90); }                                   // 삑
static void sndFail()  { beep(700, 160); delay(90); beep(700, 160); }       // 삐-삐
static void sndReady() { beep(1800, 70); delay(40); beep(2700, 90); }       // 삐리

// I2C 를 연다. PN532 는 응답이 준비될 때까지 SCL 을 직접 잡아 끈다(클럭 스트레칭) —
// 카드 찾기는 재시도를 다 돌 때까지 오래 끄는데, ESP32 기본 타임아웃 50ms 로는 중간에
// 포기해 버리고 그러면 모듈이 SCL 을 쥔 채 버스가 물려 모듈 전원을 내리기 전엔 안 풀린다.
static void wireStart() {
  Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL);
  Wire.setTimeOut(1000);
}

// ── PN532 ───────────────────────────────────────────────────────────
// IRQ 를 쓰면 '찾기' 를 걸어 두고 IRQ 가 LOW 로 떨어질 때만 읽는다 — 버스를 계속 두드리지 않는다.
Adafruit_PN532 nfc(NFC_USE_IRQ ? PIN_NFC_IRQ : 255, 255 /* RST 안 씀 */, &Wire);
static bool     nfcReady      = false;
static bool     detectArmed   = false;   // 찾기(InListPassiveTarget)를 걸어 둔 상태인가
static uint32_t armedAt       = 0;
static uint32_t nfcRetryAt    = 0;       // PN532 를 못 찾았을 때 다시 찾아볼 시각
static char     lastUid[24]   = "";
static uint32_t lastUidAt     = 0;
static bool     useIrq        = false;   // IRQ 선이 실제로 이어져 있을 때만 켠다(nfcBegin 이 확인)

// ── 와이파이 (우선순위 3개) ─────────────────────────────────────────
struct WifiAp { const char *ssid; const char *pass; };
static const WifiAp WIFI_APS[] = {
  { WIFI_SSID_0, WIFI_PASS_0 },
  { WIFI_SSID_1, WIFI_PASS_1 },
  { WIFI_SSID_2, WIFI_PASS_2 },
};
static const int      WIFI_AP_COUNT = sizeof(WIFI_APS) / sizeof(WIFI_APS[0]);
static const uint32_t WIFI_TRY_MS   = 8000;
static int      wifiIdx   = 0;
static uint32_t wifiTryAt = 0;
static bool     wifiTried = false;
static bool     online    = false;

static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (wifiTried && millis() - wifiTryAt < WIFI_TRY_MS) return;
  if (wifiTried) wifiIdx = (wifiIdx + 1) % WIFI_AP_COUNT;
  WiFi.disconnect();
  WiFi.begin(WIFI_APS[wifiIdx].ssid, WIFI_APS[wifiIdx].pass);
  wifiTryAt = millis();
  wifiTried = true;
  Serial.printf("[와이파이] %d순위 '%s' 에 붙어 보는 중\n", wifiIdx + 1, WIFI_APS[wifiIdx].ssid);
}

// ── 디스플레이 WebSocket ────────────────────────────────────────────
WebSocketsClient ws;
static bool      wsStarted   = false;    // ws.begin 을 불렀나(디스플레이 IP 를 찾았나)
static bool      wsConnected = false;
static IPAddress displayIp;
static uint32_t  resolveAt   = 0;        // 다음 mDNS 조회 시각

// 보낸 태그의 답을 기다리는 중인가 — 디스플레이가 조회·출석 처리를 마치면
// {"mode":"nfc",...} 를 돌려준다. 그 답으로 소리를 가른다(아래 ackService).
static char     ackUid[24] = "";
static uint32_t ackUntil   = 0;

static void wsEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.printf("[디스플레이] 붙음 ws://%s:%d/\n", displayIp.toString().c_str(), DISPLAY_PORT);
      break;
    case WStype_DISCONNECTED:
      if (wsConnected) Serial.println("[디스플레이] 끊김 — 다시 붙는다");
      wsConnected = false;
      break;
    case WStype_TEXT: {
      Serial.printf("[디스플레이] %.*s\n", (int)length, (const char *)payload);
      if (!ackUid[0]) break;                       // 기다리는 답이 없다
      JsonDocument doc;
      if (deserializeJson(doc, (const char *)payload, length)) break;
      if (strcmp(doc["mode"] | "", "nfc")) break;  // 대기 화면 상태 알림 — 우리 답이 아니다
      if (strcmp(doc["uid"] | "", ackUid))  break; // 다른 카드의 답
      const bool known = doc["known"] | false;
      const char *note = doc["note"] | "";
      Serial.printf("[결과] %s %s · %ld P%s%s\n", ackUid, (const char *)(doc["name"] | ""),
                    (long)(doc["points"] | 0L), note[0] ? " · " : "", note);
      ackUid[0] = '\0';
      if (known) sndOk(); else sndFail();          // 서버가 아는 키링이라야 '삑'
      break;
    }
    default:
      break;
  }
}

// 답이 오지 않으면(디스플레이가 서버에 못 물었거나 끊겼다) 기다림을 접고 실패음을 낸다.
// 소리가 아예 없으면 자원봉사자는 리더가 못 읽은 줄 알고 카드를 계속 댄다.
static void ackService() {
  if (!ackUid[0]) return;
  if ((int32_t)(millis() - ackUntil) < 0) return;
  Serial.printf("[결과] %s — 디스플레이가 답하지 않음\n", ackUid);
  ackUid[0] = '\0';
  sndFail();
}

// 디스플레이를 찾아 WebSocket 을 연다. mDNS → 실패하면 DISPLAY_IP → 그래도 없으면 5초 뒤 다시.
static void displayService() {
  if (!online || wsStarted) return;
  if ((int32_t)(millis() - resolveAt) < 0) return;
  IPAddress ip = MDNS.queryHost(DISPLAY_HOST, 2000);
  if (ip == IPAddress((uint32_t)0) && DISPLAY_IP[0]) ip.fromString(DISPLAY_IP);
  if (ip == IPAddress((uint32_t)0)) {
    Serial.printf("[디스플레이] %s.local 을 못 찾음 — 5초 뒤 다시\n", DISPLAY_HOST);
    resolveAt = millis() + 5000;
    return;
  }
  displayIp = ip;
  Serial.printf("[디스플레이] %s.local = %s\n", DISPLAY_HOST, ip.toString().c_str());
  ws.begin(ip, DISPLAY_PORT, "/");
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(2000);
  wsStarted = true;
}

// 보냈으면 true.
static bool sendToDisplay(const char *msg) {
  if (!wsConnected) { Serial.printf("[디스플레이] 안 붙어 있어 못 보냄: %s\n", msg); return false; }
  ws.sendTXT(msg);
  Serial.printf("[보냄] %s\n", msg);
  return true;
}

// ── 태그 한 건 ──────────────────────────────────────────────────────
// 읽은 UID 를 디스플레이로 넘기는 것이 전부다. 이름·잔액 조회도, 출석 지급도 하지 않는다 —
// 서버와 이야기하는 쪽을 디스플레이 하나로 모았다. 한 태깅을 두 곳에서 처리하면
// 같은 출석이 두 번 올라가고, 어느 쪽 값이 맞는지도 알 수 없게 된다.
// 결과(이름·잔액·출석 여부)는 디스플레이가 되돌려 주고, 그걸로 소리를 가른다(wsEvent).
static void handleTag(const char *uid) {
  Serial.printf("[태그] %s\n", uid);
  char msg[64];
  snprintf(msg, sizeof(msg), "{\"uid\":\"%s\"}", uid);
  if (!sendToDisplay(msg)) { sndFail(); return; }   // 디스플레이에 못 보냈다 — 답을 기다릴 것도 없다
  strncpy(ackUid, uid, sizeof(ackUid) - 1);
  ackUid[sizeof(ackUid) - 1] = '\0';
  ackUntil = millis() + ACK_WAIT_MS;
}

// ── PN532 읽기 ──────────────────────────────────────────────────────
static void uidToHex(const uint8_t *u, uint8_t len, char *out, size_t cap) {
  size_t n = 0;
  for (uint8_t i = 0; i < len && n + 2 < cap; i++) n += snprintf(out + n, cap - n, "%02X", u[i]);
  out[n] = '\0';
}

// I2C 버스를 훑어 응답하는 주소를 찍는다. PN532 는 0x24 — 안 보이면 배선·모드 스위치 문제다.
static void i2cScan() {
  int found = 0;
  Serial.printf("[I2C] SDA=%d SCL=%d 훑는 중:", PIN_NFC_SDA, PIN_NFC_SCL);
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
  }
  Serial.println(found ? "" : " 아무것도 없음");
  Serial.printf("[I2C] IRQ(GPIO%d) = %s\n", PIN_NFC_IRQ, digitalRead(PIN_NFC_IRQ) ? "HIGH" : "LOW");
}

// 배선이 어긋났을 때 — 여러 핀 조합으로 0x24(PN532)를 찾아본다. 끝나면 원래 핀으로 되돌린다.
static void i2cScanAll() {
  static const int8_t pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 10};
  int hits = 0;
  Serial.println("[I2C] 핀 조합을 모두 훑는 중(0x24 찾기)...");
  for (int8_t sda : pins) for (int8_t scl : pins) {
    if (sda == scl) continue;
    Wire.end();
    Wire.begin(sda, scl, 100000);
    Wire.beginTransmission(0x24);
    if (Wire.endTransmission() == 0) { Serial.printf("  → SDA=GPIO%d · SCL=GPIO%d 에서 PN532 응답\n", sda, scl); hits++; }
  }
  Wire.end();
  wireStart();
  Serial.println(hits ? "[I2C] 끝" : "[I2C] 어느 조합에서도 0x24 가 없음 — 전원(3V3·GND)·모드 스위치(I2C) 확인");
}

// 선이 살아 있는지 — 핀을 내부 풀다운(약 45kΩ)으로 당겨 두고 읽는다. PN532 모듈이 켜져 있고
// 선이 이어져 있으면 모듈의 풀업(보통 4.7kΩ)이 이겨 HIGH, 끊겼거나 모듈 전원이 없으면 LOW.
static void lineCheck() {
  Wire.end();
  const int8_t ps[] = {PIN_NFC_SDA, PIN_NFC_SCL, PIN_NFC_IRQ};
  const char *nm[]  = {"SDA", "SCL", "IRQ"};
  for (int i = 0; i < 3; i++) {
    pinMode(ps[i], INPUT_PULLDOWN);
    delay(5);
    const int dn = digitalRead(ps[i]);
    pinMode(ps[i], INPUT_PULLUP);                 // 반대로 당겨도 LOW 면 누군가 LOW 로 붙잡고 있다
    delay(5);
    const int up = digitalRead(ps[i]);
    Serial.printf("[선] %s(GPIO%d) 풀다운=%d 풀업=%d → %s\n", nm[i], ps[i], dn, up,
                  dn ? "이어짐(모듈 풀업이 보인다)"
                     : up ? "떠 있음 — 선이 안 닿았다"
                          : "LOW 로 붙잡혀 있음 — GND 에 닿았거나 모듈이 버스를 쥐고 있다");
  }
  pinMode(PIN_NFC_IRQ, INPUT_PULLUP);
  wireStart();
}

static bool nfcScanned = false;   // 못 찾았을 때 스캔은 한 번만 찍는다

static void nfcBegin() {
  nfc.begin();
  const uint32_t ver = nfc.getFirmwareVersion();
  nfcReady = ver != 0;
  if (nfcReady) {
    nfc.SAMConfig();
    nfc.setPassiveActivationRetries(0x10);   // 폴링 모드에서 한 번 훑는 시간을 짧게
    // IRQ 선이 있나 — PN532 는 쉴 때 IRQ 를 HIGH 로 민다. 풀다운으로 당겨도 HIGH 면 이어진 것이다.
    // 배선도에서 IRQ 는 '생략 가능' 이라, 없으면 폴링으로 돈다(IRQ 만 믿으면 카드를 영영 못 본다).
    useIrq = false;
    if (NFC_USE_IRQ) {
      pinMode(PIN_NFC_IRQ, INPUT_PULLDOWN);
      delay(5);
      useIrq = digitalRead(PIN_NFC_IRQ) == HIGH;
      pinMode(PIN_NFC_IRQ, useIrq ? INPUT : INPUT_PULLUP);
    }
    Serial.printf("[NFC] PN532 준비됨 (v%d.%d, %s)\n", (int)((ver >> 16) & 0xFF), (int)((ver >> 8) & 0xFF),
                  useIrq ? "IRQ" : (NFC_USE_IRQ ? "IRQ 선 없음 → 폴링" : "폴링"));
    sndReady();
  } else {
    Serial.printf("[NFC] PN532 를 못 찾음 — 배선(SDA=%d, SCL=%d)·I2C 모드 스위치 확인. 5초 뒤 다시\n",
                  PIN_NFC_SDA, PIN_NFC_SCL);
    nfcRetryAt = millis() + 5000;
    if (!nfcScanned) { nfcScanned = true; i2cScan(); }
  }
  detectArmed = false;
}

// 읽은 UID 를 거른다: ISO14443A UID 는 4·7·10 바이트뿐이고, 우리 키링은 7바이트(NTAG213)다.
// 4바이트는 휴대폰·교통카드라 조용히 넘긴다(휴대폰은 댈 때마다 다른 UID 를 낸다).
static void gotUid(const uint8_t *uid, uint8_t len) {
  char hex[48];
  uidToHex(uid, len > 16 ? 16 : len, hex, sizeof(hex));   // 길이가 이상해도 보이게 찍는다
  Serial.println(">>>>");
  if (len != 7 && len != 10) {
    Serial.printf("[NFC] %u바이트 UID %s — 넘긴다%s\n", (unsigned)len, hex,
                  len == 4 ? " (휴대폰·교통카드)" : " (예상 밖 길이 — 프레임이 어긋났다)");
    return;
  }
  if (!strcmp(hex, lastUid) && millis() - lastUidAt < SAME_TAG_COOLDOWN_MS) {
    lastUidAt = millis();                      // 대고 있는 동안은 계속 미룬다
    return;
  }
  strncpy(lastUid, hex, sizeof(lastUid) - 1);
  lastUidAt = millis();
  handleTag(hex);
}

static void nfcService() {
  if (!nfcReady) {
    if ((int32_t)(millis() - nfcRetryAt) >= 0) nfcBegin();
    return;
  }
  // readDetectedPassiveTargetID 는 응답의 길이 바이트를 믿고 복사한다 — 버퍼를 넉넉히 잡는다.
  uint8_t uid[255] = {0};
  uint8_t len = 0;
  if (useIrq) {
  if (!detectArmed) {
    nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);   // 카드가 오면 IRQ 가 LOW 로
    detectArmed = true;
    armedAt = millis();
  }
  if (digitalRead(PIN_NFC_IRQ) == LOW) {
    detectArmed = false;
    if (nfc.readDetectedPassiveTargetID(uid, &len)) gotUid(uid, len);
  } else if (millis() - armedAt > 60000) {
    detectArmed = false;                       // 1분 넘게 조용하면 찾기를 새로 건다(어긋남 방지)
  }
  } else {
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 60)) gotUid(uid, len);
  }
}

// 카드 읽기만 따로 시험한다 — IRQ 분기를 건너뛰고 라이브러리의 블로킹 폴링을 그대로 쓴다.
// 여기서 읽히는데 평소에 안 읽히면 IRQ 분기가 범인, 여기서도 안 읽히면 배선·전원·카드 쪽이다.
static void nfcProbe() {
  if (!nfcReady) { Serial.println("[프로브] PN532 가 없다"); return; }
  Serial.println("[프로브] 8초 동안 직접 폴링 — 지금 카드를 대세요");
  const uint32_t end = millis() + 8000;
  int tries = 0, hits = 0;
  while ((int32_t)(millis() - end) < 0) {
    uint8_t uid[255] = {0};
    uint8_t len = 0;
    tries++;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 400)) {
      hits++;
      char hex[48];
      uidToHex(uid, len > 16 ? 16 : len, hex, sizeof(hex));
      Serial.printf("[프로브] 읽음 len=%u uid=%s\n", (unsigned)len, hex);
    }
  }
  Serial.printf("[프로브] 끝 — 시도 %d회 · 읽음 %d회\n", tries, hits);
  detectArmed = false;                       // 프로브가 PN532 상태를 흔들었으니 찾기를 새로 건다
}

// ── 시리얼 ──────────────────────────────────────────────────────────
static char   lineBuf[64];
static size_t lineLen = 0;

static void handleLine(char *s) {
  while (*s == ' ') s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = '\0';
  if (!n) return;
  if (!strcmp(s, "next") || !strcmp(s, "prev") || !strcmp(s, "enable") || !strcmp(s, "toggle")) {
    sendToDisplay(s);
  } else if (!strcmp(s, "beep")) {
    Serial.println("[부저] 삑 · 삐-삐 · 삐리");
    sndOk(); delay(400); sndFail(); delay(400); sndReady();
  } else if (!strcmp(s, "probe")) {
    nfcProbe();
  } else if (!strncmp(s, "rty ", 4)) {
    // 카드찾기 재시도 횟수 — 적을수록 PN532 가 빨리 "없음" 을 답해 SCL 을 짧게 잡는다
    const uint8_t n = (uint8_t)strtol(s + 4, nullptr, 0);
    nfc.setPassiveActivationRetries(n);
    detectArmed = false;
    Serial.printf("[NFC] 카드찾기 재시도 = 0x%02X\n", n);
  } else if (!strcmp(s, "irq")) {
    useIrq = !useIrq;
    detectArmed = false;
    pinMode(PIN_NFC_IRQ, useIrq ? INPUT : INPUT_PULLUP);
    Serial.printf("[NFC] 이제 %s 로 돈다\n", useIrq ? "IRQ" : "폴링");
  } else if (!strcmp(s, "lines")) {
    lineCheck();
  } else if (!strcmp(s, "scanall")) {
    i2cScanAll();
  } else if (!strcmp(s, "scan")) {
    i2cScan();
  } else if (!strcmp(s, "status")) {
    Serial.printf("[상태] 와이파이 %s · 디스플레이 %s · PN532 %s\n",
                  online ? WiFi.localIP().toString().c_str() : "끊김",
                  wsConnected ? displayIp.toString().c_str() : "안 붙음",
                  nfcReady ? "준비됨" : "없음");
  } else {
    for (char *p = s; *p; p++) *p = toupper(*p);  // UID 는 대문자로
    handleTag(s);
  }
}

static void serialService() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n') { lineBuf[lineLen] = '\0'; handleLine(lineBuf); lineLen = 0; }
    else if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== nfcProjectClient — PN532 → nfcProject 디스플레이 ===");

  if (PIN_BUZZER >= 0) { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
  wireStart();
  if (NFC_USE_IRQ) pinMode(PIN_NFC_IRQ, INPUT_PULLUP);
  nfcBegin();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  wifiService();
}

void loop() {
  wifiService();
  const bool now = WiFi.status() == WL_CONNECTED;
  if (now != online) {
    online = now;
    if (online) {
      Serial.printf("[와이파이] 연결됨 %s (%s)\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      MDNS.begin("nfc-client");
      resolveAt = millis();
    } else {
      Serial.println("[와이파이] 끊김");
    }
  }

  displayService();
  if (wsStarted) ws.loop();
  ackService();                  // 디스플레이 답이 늦으면 기다림을 접고 실패음
  nfcService();
  serialService();
  delay(5);
}
