// nfcProjectClient — PN532 로 카드를 읽어 nfcProject 원형 디스플레이에 보내는 ESP32-C3 리더
//
// 보드: ESP32-C3 Mini / SuperMini (네이티브 USB) — 화면 없음. PN532 와 부저만 단다.
//       (아래 '디스플레이' 는 이 보드가 아니라 와이파이 건너편의 nfcProject 원형 화면이다)
// PN532: SPI — 3V3→VCC · GPIO4→SCK · GPIO5→MISO · GPIO6→MOSI · GPIO7→SS · GND→GND (config.h)
//        모듈 딥스위치를 SPI 로 놓아야 한다. I2C 는 이 모듈에서 고장 나 못 쓴다(2026-09-24 확인).
// 부저: GPIO10 → (+), GND → (−)  — 수동/능동은 config.h 의 BUZZER_ACTIVE
//        (SPI 의 SS 가 GPIO7 을 쓰게 되어 부저를 GPIO10 으로 옮겼다)
//   삑(높게 한 번)      디스플레이가 아는 키링으로 처리했다
//   삐-삐(낮게 두 번)   서버에 없는 카드 · 디스플레이에 못 보냄 · 답이 없음
//   삐리(올라가는 두 음) 켜져서 PN532 가 준비됨
//        PN532 VCC·GND 바로 옆에 100µF 전해 + 0.1µF 세라믹을 병렬로 — 없으면 태그 순간 리셋된다.
//
// 이 리더가 하는 일은 **읽어서 넘기는 것까지**다 —
//   1) 디스플레이로 가는 길을 연다 (아래 '두 갈래 길')
//   2) 카드를 대면 UID(대문자 hex, 예: 04CE1B53D12A81)를 읽어
//   3) 디스플레이에 {"uid":"…"} 를 보낸다
//   4) 디스플레이가 돌려주는 {"mode":"nfc","name":…,"points":…,"known":…,"note":…} 로 소리를 고른다
//
// ── 디스플레이로 가는 두 갈래 길 ────────────────────────────────────
//   1순위 **선(I2C)**    디스플레이 옆면 4핀 커넥터에 SDA·SCL·GND 세 가닥으로 문다(config.h 배선).
//                        이 리더가 그 버스의 슬레이브(0x30)가 되어, 읽은 UID 를 칸에 적어 두면
//                        디스플레이가 가져간다. 공유기도 mDNS 도 안 거치고, 라디오를 안 켜 시원하다.
//   2순위 **와이파이**   선이 조용하면(케이블이 빠졌거나 디스플레이가 꺼졌다) 그제서야
//                        와이파이를 켜고 mDNS 로 디스플레이를 찾아 ws://<IP>:81/ 에 붙는다.
//
//   두 길은 함께 살아 있을 수 있지만 **한 태깅은 언제나 한 길로만 나간다**(sendToDisplay).
//   두 길로 같이 보내면 디스플레이가 같은 카드를 두 번 처리해 출석이 두 번 올라간다.
//   반대로 답(ack)은 두 길로 겹쳐 와도 괜찮다 — 먼저 온 것만 쓰고 나머지는 버린다.
//
// 이름·잔액 조회와 출석 지급은 **디스플레이(nfcProject)가 한다** — 이 리더는 서버와 말하지 않는다.
// 서버와 이야기하는 쪽을 하나로 모아야 한 태깅이 두 번 처리되지 않는다.
// 그래서 config.h 에 TALENT_SERVER·TALENT_DEVICE_KEY 가 없다.
//
// ── 시리얼 명령 (115200, 한 줄씩) ───────────────────────────────────
//   <UID>              카드를 댄 것처럼 처리한다(예: 04CE1B53D12A81) — 카드 없이 시험할 때
//   next / prev / enable / toggle   디스플레이 화면 넘기기 명령을 그대로 보낸다
//   status             연결 상태를 찍는다
//   cpu <mhz>          CPU 클럭 (160/80 — 낮추면 시원해진다. 80 아래는 USB 가 끊겨 거부)
//   poll <ms>          폴링 사이 쉬는 시간 (클수록 시원하고 반응이 느려진다. 0 = 쉬지 않음)
//   raw                CS 를 내리고 바이트를 그대로 주고받아 MISO 에 뭐가 오나 본다
//   lines              SCK/MISO/MOSI/SS 선 상태
//   beep               부저 소리 세 가지를 차례로 낸다
//
// ── 빌드 ────────────────────────────────────────────────────────────
//   arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app nfcProjectClient
//   라이브러리: Adafruit PN532 (+ Adafruit BusIO) · ArduinoJson · WebSockets(by Markus Sattler)

#include <Arduino.h>
#include <SPI.h>
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

// SPI 를 연다. I2C 와 달리 클럭 스트레칭이 없어 버스가 물려 죽는 일이 없다 —
// 이 프로젝트가 I2C 를 버리고 SPI 로 온 이유이기도 하다.
static void spiStart() {
  pinMode(PIN_NFC_SS, OUTPUT);
  digitalWrite(PIN_NFC_SS, HIGH);
  SPI.begin(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SS);
}

// ── PN532 ───────────────────────────────────────────────────────────
// 라이브러리가 SPI 를 1MHz · LSB-first · MODE0 으로 잡는다(고정이라 바꿀 수 없다).
// IRQ 는 쓰지 않는다 — SPI 배선에 IRQ 선이 없고, 폴링으로 충분하다.
Adafruit_PN532 nfc(PIN_NFC_SS, &SPI);
static bool     nfcReady      = false;
static uint32_t nfcRetryAt    = 0;       // PN532 를 못 찾았을 때 다시 찾아볼 시각
static char     lastUid[24]   = "";
static uint32_t lastUidAt     = 0;

// ── 와이파이 (우선순위 3개) — 선이 죽었을 때 쓰는 예비 길 ───────────
// wifiWanted 가 false 인 동안은 라디오를 아예 올리지 않는다. C3 발열의 가장 큰 몫이
// 와이파이라, 선으로 다 되는 자리에서는 켤 이유가 없다(wifiGate 가 켤 때를 정한다).
#if LINK_I2C_ENABLE
static bool wifiWanted = false;          // 선이 조용해지면 wifiGate 가 켠다
#else
static bool wifiWanted = true;           // 선을 안 쓰는 설정 — 예전처럼 처음부터 와이파이
#endif

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

// ── 디스플레이의 답 (선·와이파이 공용) ──────────────────────────────
// 보낸 태그의 답을 기다리는 중인가 — 디스플레이가 조회·출석 처리를 마치면
// {"mode":"nfc",...} 를 돌려준다. 그 답으로 소리를 가른다(아래 ackService).
static char     ackUid[24] = "";
static uint32_t ackUntil   = 0;

// 어느 길로 왔든 답을 푸는 곳은 여기 하나다. 두 길로 같은 답이 겹쳐 와도 탈이 없다 —
// 먼저 온 것이 ackUid 를 비우므로 뒤에 온 것은 첫 줄에서 조용히 돌아간다.
// JSON 으로 안 읽히는 것(잡음)은 그대로 버린다.
//
// 와이파이로 오는 답에는 "mode":"nfc" 가 붙어 있고, 선으로 오는 답은 I2C 한 덩이에
// 들어가야 해서 그 칸을 뺀 짧은 꼴이다. 그래서 mode 를 보지 않고 **uid 가 맞는지**로
// 가른다 — 대기 화면 알림({"mode":"idle",...})에는 uid 가 없어 어차피 걸러진다.
static void handleDisplayReply(const char *text, size_t length) {
  if (!ackUid[0]) return;                        // 기다리는 답이 없다
  JsonDocument doc;
  if (deserializeJson(doc, text, length)) return;
  if (strcmp(doc["uid"] | "", ackUid))  return;  // 다른 카드의 답(또는 uid 가 없는 상태 알림)
  const bool known = doc["known"] | false;
  const char *note = doc["note"] | "";
  Serial.printf("[결과] %s %s · %ld P%s%s\n", ackUid, (const char *)(doc["name"] | ""),
                (long)(doc["points"] | 0L), note[0] ? " · " : "", note);
  ackUid[0] = '\0';
  if (known) sndOk(); else sndFail();            // 서버가 아는 키링이라야 '삑'
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

// ── 선 (I2C 슬레이브) — 1순위 길 ────────────────────────────────────
// 오가는 글자는 WebSocket 때와 **똑같다**(JSON 한 덩이) — 규약이 하나라 푸는 곳도 하나다.
//
// 슬레이브는 먼저 말을 걸 수 없다. 그래서 보낼 것을 칸(linkOut)에 적어 두면 디스플레이가
// 주기적으로 읽어 간다. 읽어 갈 때 오가는 한 덩이는 이렇게 생겼다 —
//   [0]      보낼 글자의 길이 (0 = 줄 것 없음)
//   [1..56]  글자
// 마스터가 말을 걸어 준 것 자체가 '선이 살아 있다' 는 증거다(따로 ping 을 던지지 않는다).
#if LINK_I2C_ENABLE
#define LINK_OUT_SLOTS 4
#define LINK_OUT_LEN   56
#define LINK_REC_LEN   (1 + LINK_OUT_LEN)

static volatile char     linkOut[LINK_OUT_SLOTS][LINK_OUT_LEN];
static volatile uint8_t  linkOutHead = 0;   // loop 만 쓴다(넣는 쪽)
static volatile uint8_t  linkOutTail = 0;   // 콜백만 쓴다(빼는 쪽)
static char              linkIn[224];
static volatile size_t   linkInLen    = 0;
static volatile bool     linkInReady  = false;
static volatile uint32_t linkAliveUntil = 0;
static uint32_t          linkSilentSince = 0;   // 선이 조용해진 시각(와이파이를 언제 켤지 재는 자)

static bool linkAlive() { return (int32_t)(millis() - linkAliveUntil) < 0; }

// 마스터가 "줄 것 있나" 하고 읽어 갈 때 불린다.
// **여기서 꾸물거리면 안 된다** — 이 버스에는 화면 확장칩과 터치도 함께 붙어 있어,
// 늦게 놓아 주면 디스플레이가 그리다 멈춘다. 메모리 복사만 하고 바로 빠져나온다.
static void onLinkRequest() {
  uint8_t rec[LINK_REC_LEN] = {0};
  if (linkOutTail != linkOutHead) {
    const uint8_t slot = linkOutTail;
    const size_t  n    = strnlen((const char *)linkOut[slot], LINK_OUT_LEN);
    rec[0] = (uint8_t)n;
    memcpy(rec + 1, (const void *)linkOut[slot], n);
    linkOutTail = (uint8_t)((slot + 1) % LINK_OUT_SLOTS);
  }
  Wire.write(rec, sizeof(rec));
  linkAliveUntil = millis() + LINK_ALIVE_MS;
}

// 마스터가 결과를 써 넣을 때. 여기서도 복사만 한다 — 푸는 것은 loop(linkService)에서.
static void onLinkReceive(int n) {
  (void)n;
  size_t i = 0;
  while (Wire.available() && i < sizeof(linkIn) - 1) linkIn[i++] = (char)Wire.read();
  while (Wire.available()) Wire.read();          // 넘치는 것은 버린다
  linkIn[i]      = '\0';
  linkInLen      = i;
  linkInReady    = i > 0;
  linkAliveUntil = millis() + LINK_ALIVE_MS;
}

// 보낼 것을 칸에 넣는다(넣기만 한다 — 가져가는 것은 마스터다). 칸이 차면 false.
static bool linkPush(const char *msg) {
  const uint8_t next = (uint8_t)((linkOutHead + 1) % LINK_OUT_SLOTS);
  if (next == linkOutTail) return false;
  strncpy((char *)linkOut[linkOutHead], msg, LINK_OUT_LEN - 1);
  linkOut[linkOutHead][LINK_OUT_LEN - 1] = '\0';
  linkOutHead = next;
  return true;
}

static void linkService() {
  if (!linkInReady) return;
  linkInReady = false;
  Serial.printf("[선] %s\n", linkIn);
  handleDisplayReply(linkIn, linkInLen);
}

// 와이파이를 언제 켤지 정한다. 선이 살아 있는 동안은 라디오를 올리지 않는다 —
// 켜 두기만 해도 C3 가 눈에 띄게 뜨거워진다. 한 번 켠 와이파이는 다시 끄지 않는다:
// 선이 오락가락할 때마다 껐다 켜면 붙는 데 걸리는 몇 초가 그대로 태깅 실패가 된다.
static void wifiGate() {
#if LINK_WIFI_FALLBACK
  if (wifiWanted) return;
  if (linkAlive()) { linkSilentSince = millis(); return; }
  if (millis() - linkSilentSince < LINK_GRACE_MS) return;
  wifiWanted = true;
  Serial.println("[연결] 선이 조용하다 — 와이파이로 붙어 본다");
#endif
}
#else
static bool linkAlive()            { return false; }
static void linkService()          {}
static void wifiGate()             {}
static bool linkPush(const char *) { return false; }
#endif

// ── 디스플레이 WebSocket — 2순위 길 ─────────────────────────────────
WebSocketsClient ws;
static bool      wsStarted   = false;    // ws.begin 을 불렀나(디스플레이 IP 를 찾았나)
static bool      wsConnected = false;
static IPAddress displayIp;
static uint32_t  resolveAt   = 0;        // 다음 mDNS 조회 시각
static uint32_t  downSince   = 0;        // 끊긴 채로 있던 시작 시각(0 = 붙어 있음). IP 가 바뀌었나 다시 확인하려고

static void wsEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      downSince   = 0;
      Serial.printf("[디스플레이] 붙음 ws://%s:%d/\n", displayIp.toString().c_str(), DISPLAY_PORT);
      break;
    case WStype_DISCONNECTED:
      if (wsConnected) { Serial.println("[디스플레이] 끊김 — 다시 붙는다"); downSince = millis(); }
      wsConnected = false;
      break;
    case WStype_TEXT:
      Serial.printf("[디스플레이] %.*s\n", (int)length, (const char *)payload);
      handleDisplayReply((const char *)payload, length);
      break;
    default:
      break;
  }
}

// 디스플레이를 mDNS 로 찾는다. 세 가지를 차례로 시도한다 —
//   1) ws 서비스 질의(queryService) : 디스플레이가 광고하는 ws._tcp 를 훑는다.
//      ESP32 의 queryHost('.local' 호스트네임 직접 해석)는 상대가 켜져 있어도 0.0.0.0 을
//      돌려주는 일이 잦은데, 서비스 질의는 잘 잡힌다 — 순서와 무관하게 붙게 하는 핵심이다.
//   2) 호스트 질의(queryHost)         : 예전 방식(서비스 이름이 안 맞는 공유기 대비).
//   3) 고정 IP(DISPLAY_IP)            : mDNS 가 아예 막힌 망일 때 config.h 에 적어 둔 값.
// 셋 다 실패하면 0.0.0.0 을 돌려준다.
static IPAddress resolveDisplay() {
  const int n = MDNS.queryService("ws", "tcp");     // 광고된 ws 서버들
  for (int i = 0; i < n; i++)                        // 이름이 nfc-display 인 것을 먼저 고른다
    if (MDNS.hostname(i) == DISPLAY_HOST) return MDNS.address(i);
  if (n > 0) return MDNS.address(0);                 // 망에 ws 서버가 하나뿐이면(디스플레이) 그걸로

  IPAddress ip = MDNS.queryHost(DISPLAY_HOST, 2000); // 서비스로 못 찾으면 호스트 이름으로
  if (ip != IPAddress((uint32_t)0)) return ip;

  if (DISPLAY_IP[0]) { ip.fromString(DISPLAY_IP); return ip; }  // 그래도 없으면 고정 IP
  return IPAddress((uint32_t)0);
}

// 끊긴 채 이만큼 지나면 mDNS 로 다시 찾아본다 — 디스플레이가 재부팅돼 IP 가 바뀌었을 수 있다.
// (WebSocketsClient 는 늘 옛 IP 로만 재접속을 시도하므로, IP 가 바뀌면 여기서 새 IP 로 다시 건다.)
#define DISPLAY_REBIND_MS 15000

// 디스플레이를 찾아 WebSocket 을 연다.
//   · 아직 못 붙었으면(wsStarted=false) 3초마다 다시 찾는다 — 리더를 먼저 켜 두고
//     디스플레이를 나중에 켜도, 디스플레이가 뜨는 순간 이 재시도가 잡아 붙는다.
//   · 이미 붙은 적 있는데 오래 끊겨 있으면 mDNS 로 다시 찾아, IP 가 바뀌었으면 새 IP 로 다시 건다.
static void displayService() {
  if (!online) return;

  // 붙은 적 있고 지금도 붙어 있으면 라이브러리의 자동 재접속에 맡긴다.
  if (wsStarted && (wsConnected || downSince == 0)) return;

  // 오래 끊겨 있을 때만 다시 찾는다(막 끊긴 직후는 자동 재접속이 같은 IP 로 붙여 줄 수 있다).
  if (wsStarted && (int32_t)(millis() - downSince) < DISPLAY_REBIND_MS) return;

  if ((int32_t)(millis() - resolveAt) < 0) return;
  resolveAt = millis() + 3000;

  IPAddress ip = resolveDisplay();
  if (ip == IPAddress((uint32_t)0)) {
    if (!wsStarted)
      Serial.printf("[디스플레이] %s 를 아직 못 찾음 — 다시 찾는다(디스플레이가 켜지면 붙는다)\n", DISPLAY_HOST);
    return;
  }

  if (!wsStarted) {                                  // 첫 연결
    displayIp = ip;
    Serial.printf("[디스플레이] %s = %s\n", DISPLAY_HOST, ip.toString().c_str());
    ws.begin(ip, DISPLAY_PORT, "/");
    ws.onEvent(wsEvent);
    ws.setReconnectInterval(2000);
    wsStarted = true;
    downSince  = millis();                            // 이 IP 로 못 붙는 채 오래 지나면 다시 찾도록(잘못 잡혔을 때 대비)
    return;
  }

  if (ip != displayIp) {                             // 재부팅 등으로 IP 가 바뀌었다 — 새 IP 로 다시 건다
    Serial.printf("[디스플레이] IP 바뀜 %s → %s — 다시 붙는다\n",
                  displayIp.toString().c_str(), ip.toString().c_str());
    displayIp = ip;
    ws.disconnect();
    ws.begin(ip, DISPLAY_PORT, "/");
  }
  downSince = millis();                              // 다음 재확인까지 또 기다린다(찾기를 계속 두드리지 않게)
}

// 보냈으면 true. **한 길로만 보낸다** — 선이 살아 있으면 선으로, 아니면 WebSocket 으로.
// 두 길로 같이 보내면 디스플레이가 같은 UID 를 두 번 받아 출석이 두 번 올라간다.
static bool sendToDisplay(const char *msg) {
#if LINK_I2C_ENABLE
  // 칸에 적어 두면 디스플레이가 LINK_POLL_MS 안에 가져간다(슬레이브는 먼저 못 건다).
  if (linkAlive()) {
    if (linkPush(msg)) { Serial.printf("[보냄·선] %s\n", msg); return true; }
    Serial.printf("[선] 보낼 칸이 찼다 — 와이파이로 돌린다: %s\n", msg);
  }
#endif
  if (!wsConnected) { Serial.printf("[디스플레이] 선도 와이파이도 없어 못 보냄: %s\n", msg); return false; }
  ws.sendTXT(msg);
  Serial.printf("[보냄·와이파이] %s\n", msg);
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

// CS 를 내린 채 바이트를 그대로 주고받아 MISO 에 뭐가 오나 본다 — 라이브러리를 안 거친다.
// PN532 가 SPI 모드로 살아 있으면 상태 바이트 01(준비됨)이 돌아온다.
static void rawProbe() {
  uint8_t got[8];
  SPI.beginTransaction(SPISettings(1000000UL, LSBFIRST, SPI_MODE0));
  digitalWrite(PIN_NFC_SS, LOW);
  delay(2);                                  // 깨우기 — CS 를 2ms 붙잡는다
  got[0] = SPI.transfer(0x02);               // SPI_STATREAD
  for (uint8_t i = 1; i < 8; i++) got[i] = SPI.transfer(0x00);
  digitalWrite(PIN_NFC_SS, HIGH);
  SPI.endTransaction();

  Serial.print("[raw] 되돌아온 바이트:");
  uint8_t same = 0;
  for (uint8_t i = 0; i < 8; i++) {
    Serial.printf(" %02X", got[i]);
    if (got[i] == got[0]) same++;
  }
  Serial.println();
  // PN532 를 이미 잡아 둔 뒤에는 이 날것 거래에 상태 바이트가 안 돌아온다(라이브러리가
  // 모듈을 정상 모드로 올려 둔 상태다). 그러니 이 판정은 **PN532 를 못 찾았을 때만** 믿어라.
  if (same == 8) Serial.printf("[raw] 전부 같은 값 — MISO 가 조용하다.%s\n",
                               nfcReady ? " (PN532 는 이미 준비됨 — 이 줄은 무시해라)"
                                        : " 선·모드 스위치(SPI)·모듈 전원을 봐라");
  else           Serial.println("[raw] 섞여 온다 — 모듈이 말하고 있다. 배선은 닿았다");
}

// 선 상태. SPI 선에는 풀업이 없으니 I2C 때만큼 결정적이지 않다 — 잡히는 건 단락(둘 다 0)뿐이다.
// 특히 **MISO 가 '떠 있음' 으로 나오는 건 정상이다**: CS 가 HIGH 인 동안 PN532 는 MISO 를
// 하이임피던스로 놓는다. 연결 판정은 raw 로 한다.
static void lineCheck() {
  const int8_t ps[] = {PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SS};
  const char *nm[]  = {"SCK", "MISO", "MOSI", "SS"};
  for (int i = 0; i < 4; i++) {
    pinMode(ps[i], INPUT_PULLDOWN);
    delay(5);
    const int dn = digitalRead(ps[i]);
    pinMode(ps[i], INPUT_PULLUP);                 // 반대로 당겨도 LOW 면 누군가 LOW 로 붙잡고 있다
    delay(5);
    const int up = digitalRead(ps[i]);
    Serial.printf("[선] %s(GPIO%d) 풀다운=%d 풀업=%d → %s\n", nm[i], ps[i], dn, up,
                  (dn == 0 && up == 0) ? "LOW 로 붙잡혀 있음 — GND 단락이거나 남이 쥐고 있다"
                  : (dn == 1 && up == 1) ? "HIGH 로 밀려 있음 — 밖에서 당기고 있다"
                                         : "하이임피던스(정상)");
  }
  // SPI.begin() 은 이미 초기화돼 있으면 그냥 돌아간다 — 핀을 매트릭스에 다시 안 붙인다.
  // 위에서 pinMode 로 떼어 놨으니 반드시 end() 부터 불러야 복구된다(안 그러면 그대로 죽는다).
  SPI.end();
  spiStart();
}

static bool nfcScanned = false;   // 못 찾았을 때 스캔은 한 번만 찍는다

static void nfcBegin() {
  nfc.begin();
  const uint32_t ver = nfc.getFirmwareVersion();
  nfcReady = ver != 0;
  if (nfcReady) {
    nfc.SAMConfig();
    nfc.setPassiveActivationRetries(NFC_RETRIES);   // 한 번 훑는 시간 — 짧을수록 RF 를 덜 켠다
    Serial.printf("[NFC] PN532 준비됨 (v%d.%d, SPI 폴링)\n",
                  (int)((ver >> 16) & 0xFF), (int)((ver >> 8) & 0xFF));
    sndReady();
  } else {
    Serial.printf("[NFC] PN532 를 못 찾음 — 배선(SCK=%d, MISO=%d, MOSI=%d, SS=%d)·모듈 딥스위치가 SPI 인지 확인. 5초 뒤 다시\n",
                  PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SS);
    nfcRetryAt = millis() + 5000;
    if (!nfcScanned) { nfcScanned = true; rawProbe(); }
  }
}

// 읽은 UID 를 거른다: ISO14443A UID 는 4·7·10 바이트뿐이고, 우리 키링은 7바이트(NTAG213)다.
// 4바이트는 휴대폰·교통카드라 조용히 넘긴다(휴대폰은 댈 때마다 다른 UID 를 낸다).
static void gotUid(const uint8_t *uid, uint8_t len) {
  char hex[48];
  uidToHex(uid, len > 16 ? 16 : len, hex, sizeof(hex));   // 길이가 이상해도 보이게 찍는다
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

// 폴링 사이에 쉰다. 한 번 훑을 때마다 PN532 가 RF 필드를 켜 전류를 쓰므로,
// 쉬는 시간이 그대로 모듈 발열로 이어진다. 120ms 면 사람이 카드를 대고 있는
// 시간(보통 0.5초 넘는다)보다 훨씬 짧아 놓치지 않는다.
static uint32_t nfcPollAt = 0;
// 발열은 실물을 만져 보며 정하는 수밖에 없다. 구운 채로 `poll` 로 바꿔 볼 수 있게 변수로 둔다.
static uint32_t nfcPollEvery = NFC_POLL_EVERY_MS;

static void nfcService() {
  if (!nfcReady) {
    if ((int32_t)(millis() - nfcRetryAt) >= 0) nfcBegin();
    return;
  }
  if ((int32_t)(millis() - nfcPollAt) < 0) return;
  nfcPollAt = millis() + nfcPollEvery;
  // 라이브러리가 응답의 길이 바이트를 믿고 복사한다 — 버퍼를 넉넉히 잡는다.
  uint8_t uid[255] = {0};
  uint8_t len = 0;
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 60)) gotUid(uid, len);
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
    // 카드찾기 재시도 횟수 — 적을수록 PN532 가 빨리 "없음" 을 답해 폴링 한 바퀴가 짧아진다
    const uint8_t n = (uint8_t)strtol(s + 4, nullptr, 0);
    nfc.setPassiveActivationRetries(n);
    Serial.printf("[NFC] 카드찾기 재시도 = 0x%02X\n", n);
  } else if (!strncmp(s, "cpu ", 4)) {
    const uint32_t mhz = strtoul(s + 4, nullptr, 10);
    if (mhz < 80) { Serial.println("[CPU] 80MHz 아래로는 USB 시리얼이 끊긴다 — 거부"); return; }
    setCpuFrequencyMhz(mhz);
    Serial.printf("[CPU] %uMHz\n", (unsigned)getCpuFrequencyMhz());
  } else if (!strncmp(s, "poll ", 5)) {
    nfcPollEvery = strtoul(s + 5, nullptr, 10);
    Serial.printf("[NFC] 폴링 간격 = %lums%s\n", (unsigned long)nfcPollEvery,
                  nfcPollEvery ? "" : " (쉬지 않는다)");
  } else if (!strcmp(s, "lines")) {
    lineCheck();
  } else if (!strcmp(s, "raw")) {
    rawProbe();
  } else if (!strcmp(s, "status")) {
    Serial.printf("[상태] 보내는 길 %s\n",
                  linkAlive() ? "선(UART)" : (wsConnected ? "와이파이(WebSocket)" : "없음 — 못 보낸다"));
    Serial.printf("[상태] 선 %s · 와이파이 %s · 디스플레이 %s · PN532 %s\n",
                  LINK_I2C_ENABLE ? (linkAlive() ? "살아 있음" : "조용함") : "꺼 둠",
                  !wifiWanted ? "안 켬(선이 있어 필요 없다)"
                              : (online ? WiFi.localIP().toString().c_str() : "끊김"),
                  wsConnected ? displayIp.toString().c_str() : "안 붙음",
                  nfcReady ? "준비됨" : "없음");
    Serial.printf("[상태] CPU %uMHz · 폴링 %lums · 재시도 0x%02X · 모뎀슬립 %s\n",
                  (unsigned)getCpuFrequencyMhz(), (unsigned long)nfcPollEvery,
                  NFC_RETRIES, WIFI_MODEM_SLEEP ? "켬" : "끔");
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

  // 발열을 줄인다 — 와이파이를 올리기 전에 클럭부터 내린다.
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.printf("[전원] CPU %uMHz · 폴링 %lums · 재시도 0x%02X · 모뎀슬립 %s\n",
                (unsigned)getCpuFrequencyMhz(), (unsigned long)nfcPollEvery,
                NFC_RETRIES, WIFI_MODEM_SLEEP ? "켬" : "끔");

  if (PIN_BUZZER >= 0) { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
  spiStart();
  nfcBegin();

#if LINK_I2C_ENABLE
  // 선을 먼저 연다 — 디스플레이가 말을 걸어 오면 와이파이는 끝내 켜지 않는다(wifiGate).
  // setBufferSize 는 begin 보다 먼저 — 기본 128 바이트로는 결과 한 덩이가 잘릴 수 있다.
  Wire.setBufferSize(256);
  Wire.onRequest(onLinkRequest);
  Wire.onReceive(onLinkReceive);
  if (Wire.begin((uint8_t)LINK_I2C_ADDR, PIN_LINK_SDA, PIN_LINK_SCL, LINK_I2C_HZ))
    Serial.printf("[선] I2C 슬레이브 0x%02X · SDA=GPIO%d SCL=GPIO%d — 디스플레이를 기다린다\n",
                  LINK_I2C_ADDR, PIN_LINK_SDA, PIN_LINK_SCL);
  else
    Serial.println("[선] I2C 슬레이브를 못 열었다 — 와이파이로만 간다");
  linkSilentSince = millis();
#endif

  WiFi.mode(WIFI_STA);
  // 모뎀 슬립 — C3 발열의 가장 큰 몫이다. 재우면 라디오가 DTIM 사이에 꺼져
  // 평균 전류가 크게 준다. 대신 디스플레이로 가는 왕복에 수십~수백 ms 가 붙는데,
  // ACK_WAIT_MS(4초) 안이라 소리 판정에는 지장이 없다.
  WiFi.setSleep(WIFI_MODEM_SLEEP ? true : false);
  if (wifiWanted) wifiService();      // 선을 쓰는 설정이면 여기서는 아직 켜지 않는다
}

void loop() {
  linkService();                 // 1순위 길 — 들어온 답을 처리하고 살아 있는지 물어본다
  wifiGate();                    // 선이 오래 조용하면 그제서야 와이파이를 켠다

  if (wifiWanted) {              // 2순위 길 — 선으로 다 되는 자리에서는 여기가 통째로 안 돈다
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
  }

  ackService();                  // 디스플레이 답이 늦으면 기다림을 접고 실패음
  nfcService();
  serialService();
  delay(5);
}
