// ChurchDisplayRx — TTGO T-Display (ESP32)
// WebSocket 으로 실시간 수신 → 한글 전체를 화면에 표시.
//
// ── 두 가지 수신 경로 ────────────────────────────────────────────
//  1) 서버 역할 (기본): 이 보드가 WebSocket 서버를 열고, 센서 보드
//     (SHT41MonitorC3, ESP32-C3)가 직접 접속해 온습도를 밀어 넣는다.
//     중간 서버가 필요 없다. mDNS 로 "churchdisplay.local" 을 광고한다.
//  2) 클라이언트 역할 (선택): 바깥 서버에 붙어 푸시를 받는다.
//     USE_WS_CLIENT 를 1 로 켜면 동작한다. RAM 여유가 없으면 끈 채로 둘 것.
//
// ── 한글 전체 지원 ────────────────────────────────────────────────
// TFT_eSPI 내장 폰트는 ASCII 전용이라 스무스폰트(VLW)를 쓴다.
//   FontKRFull12.h — 나눔고딕볼드 12px, 한글 음절 11,172자 전체 + ASCII (1.82MB)
//   FontNum38.h    — 나눔고딕볼드 38px, 숫자 전용 15자 (9KB)
// 서버가 어떤 한글을 보내도 그려진다. tools/ttf2vlw.py 로 생성.
//
// ── 이 구성에서 지켜야 할 제약 ────────────────────────────────────
//  · 파티션은 반드시 Huge APP(3MB). 폰트 1.82MB + 코드가 기본 1.31MB 를 넘는다.
//    sketch.yaml 에 박아뒀으므로 arduino-cli 는 자동으로 맞춘다.
//  · TFT_eSPI 는 글리프 메트릭을 RAM 에 올린다 — 11,267자 × 12B ≈ 135KB.
//    그래서 전체화면 스프라이트(64KB)를 쓰지 않고 화면에 직접 그린다.
//    깜빡임은 setTextPadding 으로 글자 영역만 덮어써서 없앤다.
//  · 큰 숫자는 작은 스프라이트에 38px 폰트를 따로 물려 쓴다.
//    (큰 폰트를 매 프레임 load/unload 하면 메트릭 재적재로 매우 느려짐)
//
// 필요 라이브러리:
//   TFT_eSPI(Setup25) · WebSockets(Links2004) · ArduinoJson 7.x · ChurchSecrets
//
// 서버가 보내는 JSON:
//   {"title":"본당 예배실","temp":24.6,"humi":51.2,"msg":"정상 작동 중"}

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include <ChurchSecrets.h>
#include "FontKRFull12.h"
#include "FontNum38.h"

// 센서 보드가 접속해 오는 서버 역할 (기본 켜짐)
#define USE_WS_SERVER 1
// 요청 간격(ms). 한 번에 한 대씩, 순번대로 돌아가며 호출한다.
//   1번 기기 → 5초 → 2번 기기 → 5초 → 1번 기기 → ...
// 브로드캐스트가 아니므로 여러 대가 동시에 응답을 쏘는 일이 없다.
// 대신 N대가 붙으면 각 기기가 다시 호출되기까지는 REQUEST_MS × N 이 걸린다.
#define REQUEST_MS 5000
// 바깥 서버에 붙어 받는 클라이언트 역할 (기본 꺼짐 — RAM 절약)
#define USE_WS_CLIENT 0
#define USE_TLS 0
const char*    WS_HOST = WS_HOST_LOCAL;
const uint16_t WS_PORT = WS_PORT_LOCAL;

#define PIN_BL 4
// T-Display 버튼. 아무 쪽이나 누르면 값 화면 ↔ 온도 그래프가 바뀐다.
#define BTN_TOP    35   // 입력 전용 핀(내부 풀업 없음, 보드에 외부 풀업 있음)
#define BTN_BOTTOM  0

// 기기별 온도·습도 이력. int16_t 에 0.1 단위로 담는다
// (5대 × 96 × 2B × 2계열 ≈ 2KB — 폰트가 135KB 를 쓰므로 float 대신 정수로).
#define HIST_N 96

static const int16_t SCR_W = 240;
static const int16_t SCR_H = 135;

// 레이아웃
static const int16_t BAR_H    = 20;            // 상단/하단 바 높이
static const int16_t Y_TITLE  = 24;
static const int16_t Y_LABEL  = 44;
static const int16_t Y_NUM    = 60;
static const int16_t NUM_W    = 112, NUM_H = 44;
static const int16_t X_TEMP   = 6, X_HUMI = 122;

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite numSpr = TFT_eSprite(&tft);        // 큰 숫자 전용(38px 폰트 상주)
#if USE_WS_SERVER
WebSocketsServer wsServer(DISPLAY_WS_PORT);    // 센서 보드가 붙는 곳
#endif
#if USE_WS_CLIENT
WebSocketsClient webSocket;
#endif

static bool     wsUp      = false;   // 클라이언트 역할의 연결 상태
static uint8_t  sensors   = 0;       // 서버 역할에 붙어 있는 센서 보드 수
// 값 하나짜리 원형 버퍼. 온도와 습도가 각각 하나씩 쓴다.
struct Series {
  int16_t v[HIST_N];
  uint8_t count;   // 채워진 개수 (최대 HIST_N)
  uint8_t head;    // 다음에 쓸 위치
};

// 기기별 상태. WebSocketsServer 는 클라이언트를 번호(num)로 구분한다.
struct SensorSlot {
  bool     used;
  uint32_t lastReq;    // 마지막 요청 시각 — 기기마다 따로 돈다
  uint32_t reqAt;      // 응답 대기 중인 요청의 시각(0이면 대기 없음)
  uint32_t sent;
  uint32_t answered;
  char     name[24];   // 그 기기가 보내온 title (그래프 범례에 쓴다)
  Series   t;          // 온도 이력 (0.1도 단위)
  Series   h;          // 습도 이력 (0.1% 단위)
  float    lastTemp, lastHumi;
  bool     hasTemp,  hasHumi;
};
static SensorSlot slot[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
static uint8_t  rrNext    = 0;       // 다음에 호출할 기기 번호(순번)
static uint32_t lastPoll  = 0;       // 마지막 요청 시각(전체 공통)
static bool     haveData  = false;
static uint32_t lastMsgMs = 0;
static uint32_t rxCount   = 0;

static char  title[96] = "";
static char  msg[96]   = "";
static float temp      = 0.0f;
static float humi      = 0.0f;
static bool  hasTemp   = false;
static bool  hasHumi   = false;
// 화면: 값 / 온도 그래프 / 습도 그래프
enum Screen { SCR_VALUES, SCR_TEMP, SCR_HUMI };
static Screen screen      = SCR_VALUES;
static bool   screenDirty = true;    // 모드가 바뀌면 전체를 다시 그린다
static bool  chromeDrawn = false;
static bool  numSprOk    = false;
static bool  fontOk      = false;

// 글리프 11,267개 메트릭 ≈ 135KB(가장 큰 단일 블록은 gBitmap 45KB).
static const uint32_t FONT_HEAP_NEED  = 145000;
static const uint32_t FONT_BLOCK_NEED = 48000;

static uint16_t COL_BG, COL_BAR, COL_DIM, COL_TXT, COL_HOT, COL_WET, COL_OK, COL_ERR;

// 기기별 그래프 선 색. 슬롯 번호 순서대로 쓴다.
static uint16_t lineColor(uint8_t n) {
  switch (n % 5) {
    case 0:  return COL_HOT;                        // 주황
    case 1:  return COL_WET;                        // 하늘
    case 2:  return COL_OK;                         // 초록
    case 3:  return tft.color565(230, 140, 255);    // 보라
    default: return tft.color565(250, 230, 120);    // 노랑
  }
}

// 새 값을 계열에 밀어 넣는다. 0.1 단위 정수로 담는다.
static void pushSeries(Series& s, float value) {
  s.v[s.head] = (int16_t)lroundf(value * 10.0f);
  s.head = (s.head + 1) % HIST_N;
  if (s.count < HIST_N) s.count++;
}

// 오래된 것부터 i번째 표본(0 = 가장 오래됨)
static int16_t seriesAt(const Series& s, uint8_t i) {
  uint8_t start = (s.head + HIST_N - s.count) % HIST_N;
  return s.v[(start + i) % HIST_N];
}

// 지금 보고 있는 화면이 어느 계열을 쓰는지
static const Series& seriesOf(uint8_t n, Screen sc) {
  return (sc == SCR_HUMI) ? slot[n].h : slot[n].t;
}

static void initColors() {
  COL_BG  = tft.color565(10, 16, 32);
  COL_BAR = tft.color565(22, 34, 62);
  COL_DIM = tft.color565(130, 150, 185);
  COL_TXT = tft.color565(235, 240, 250);
  COL_HOT = tft.color565(255, 160, 70);
  COL_WET = tft.color565(90, 190, 255);
  COL_OK  = tft.color565(80, 220, 130);
  COL_ERR = tft.color565(255, 95, 95);
}

// UTF-8 경계를 지키며 복사한다. 잘라내다 멀티바이트 한 글자를 반토막 내면
// 깨진 글리프가 되므로, 연속 바이트(10xxxxxx)에서 끊기면 그 앞까지만 담는다.
static void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = 0;
  while (src[n] && n < cap - 1) n++;
  while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// ══════════════════════════════════════════════════════════════════
//  화면
// ══════════════════════════════════════════════════════════════════
static void drawChrome() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCR_W, BAR_H, COL_BAR);
  tft.fillRect(0, SCR_H - BAR_H, SCR_W, BAR_H, COL_BAR);
  chromeDrawn = true;
}

static void renderValues() {
  if (!chromeDrawn) drawChrome();

  tft.setTextDatum(TL_DATUM);

  // ── 상태바 ──
  // 폰트 적재에 실패한 경우에는 한글이 안 나오므로 ASCII 로 상태를 알린다.
  char state[32]; uint16_t stateCol;
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(state, sizeof(state), fontOk ? "무선 끊김" : "NO WIFI");
    stateCol = COL_ERR;
  } else if (sensors > 0) {
    snprintf(state, sizeof(state), fontOk ? "센서 %u대 연결" : "SENSOR %u", sensors);
    stateCol = COL_OK;
  } else if (wsUp) {
    snprintf(state, sizeof(state), fontOk ? "실시간 연결" : "LIVE");
    stateCol = COL_OK;
  } else {
    snprintf(state, sizeof(state), fontOk ? "센서 대기 중" : "WAITING");
    stateCol = COL_ERR;
  }
  tft.setTextColor(stateCol, COL_BAR);
  tft.setTextPadding(126);
  tft.drawString(state, 6, 3);

  char rssi[16] = "";
  if (WiFi.status() == WL_CONNECTED) snprintf(rssi, sizeof(rssi), "%d dBm", WiFi.RSSI());
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_DIM, COL_BAR);
  tft.setTextPadding(90);
  tft.drawString(rssi, SCR_W - 6, 3);

  // ── 제목(서버가 보낸 한글 그대로) ──
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TXT, COL_BG);
  tft.setTextPadding(SCR_W - 12);
  tft.drawString(title, 6, Y_TITLE);

  // ── 라벨 + 큰 숫자 ──
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextPadding(60);
  tft.drawString(haveData && hasTemp ? (fontOk ? "온도" : "TEMP") : "", X_TEMP, Y_LABEL);
  tft.drawString(haveData && hasHumi ? (fontOk ? "습도" : "HUMI") : "", X_HUMI, Y_LABEL);

  if (!haveData) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextPadding(SCR_W - 12);
    tft.drawString(fontOk ? "수신 대기 중" : "waiting...", 6, Y_NUM + 10);
  } else if (numSprOk) {
    if (hasTemp) {
      numSpr.fillSprite(COL_BG);
      numSpr.setTextColor(COL_HOT, COL_BG);
      numSpr.setTextDatum(TL_DATUM);
      numSpr.drawFloat(temp, 1, 0, 0);
      numSpr.pushSprite(X_TEMP, Y_NUM);
    }
    if (hasHumi) {
      numSpr.fillSprite(COL_BG);
      numSpr.setTextColor(COL_WET, COL_BG);
      numSpr.setTextDatum(TL_DATUM);
      numSpr.drawFloat(humi, 1, 0, 0);
      numSpr.pushSprite(X_HUMI, Y_NUM);
    }
  }

  // ── 하단: 메시지 + 마지막 수신 경과 ──
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_DIM, COL_BAR);
  tft.setTextPadding(150);
  tft.drawString(msg[0] ? msg : "-", 6, SCR_H - BAR_H + 3);

  char age[32] = "";
  if (haveData) {
    uint32_t sent = 0, answered = 0;
#if USE_WS_SERVER
    requestTotals(sent, answered);
#endif
    // "3초 전 42/43" — 마지막 수신 경과, 응답/요청 횟수(전 기기 합계)
    snprintf(age, sizeof(age), fontOk ? "%lu초 전 %lu/%lu" : "%lus %lu/%lu",
             (unsigned long)((millis() - lastMsgMs) / 1000),
             (unsigned long)answered, (unsigned long)sent);
  }
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(88);
  tft.drawString(age, SCR_W - 6, SCR_H - BAR_H + 3);

  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

// ══════════════════════════════════════════════════════════════════
//  온도 그래프 화면 — 붙어 있는 기기 전부를 한 판에 겹쳐 그린다
// ══════════════════════════════════════════════════════════════════
static const int16_t GX = 30, GY = 30;                 // 그래프 좌상단
static const int16_t GW = SCR_W - GX - 8;              // 폭
static const int16_t GH = SCR_H - GY - 26;             // 높이

static void renderGraph(Screen kind) {
  const bool  isH  = (kind == SCR_HUMI);
  const char* head = isH ? (fontOk ? "습도 그래프" : "HUMI GRAPH")
                         : (fontOk ? "온도 그래프" : "TEMP GRAPH");
  // 너무 납작하지 않게 보장할 최소 폭: 온도 2.0도, 습도 5.0%
  const int16_t MIN_SPAN = isH ? 50 : 20;

  tft.fillScreen(COL_BG);
  tft.setTextDatum(TL_DATUM);

  // ── 표본이 있는 기기와 값 범위를 먼저 구한다 ──
  int16_t lo = 32767, hi = -32768;
  uint8_t withData = 0, samples = 0;
  for (uint8_t n = 0; n < WEBSOCKETS_SERVER_CLIENT_MAX; n++) {
    const Series& sr = seriesOf(n, kind);
    if (!sr.count) continue;
    withData++;
    if (sr.count > samples) samples = sr.count;
    for (uint8_t i = 0; i < sr.count; i++) {
      int16_t v = seriesAt(sr, i);
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
  }

  if (!withData) {
    tft.setTextColor(COL_TXT, COL_BG);
    tft.drawString(head, 8, 6);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(fontOk ? "아직 쌓인 값이 없다" : "no samples yet", 8, 60);
    return;
  }

  if (hi - lo < MIN_SPAN) {
    int16_t mid = (hi + lo) / 2;
    lo = mid - MIN_SPAN / 2; hi = mid + MIN_SPAN / 2;
  }
  int16_t pad = (hi - lo) / 10 + 1;
  lo -= pad; hi += pad;

  // ── 눈금 · 격자 ──
  tft.drawRect(GX, GY, GW, GH, COL_BAR);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextDatum(TR_DATUM);
  for (uint8_t g = 0; g <= 2; g++) {
    int16_t y = GY + (GH - 1) * g / 2;
    int16_t v = hi - (int32_t)(hi - lo) * g / 2;
    if (g) for (int16_t x = GX + 1; x < GX + GW; x += 6) tft.drawPixel(x, y, COL_BAR);
    char lbl[12];
    snprintf(lbl, sizeof(lbl), "%d.%d", v / 10, abs(v % 10));
    tft.drawString(lbl, GX - 3, y - 6);
  }
  tft.setTextDatum(TL_DATUM);

  // ── 기기별 선 ──
  for (uint8_t n = 0; n < WEBSOCKETS_SERVER_CLIENT_MAX; n++) {
    const Series& sr = seriesOf(n, kind);
    if (sr.count < 2) continue;
    uint16_t col = lineColor(n);
    int16_t px = 0, py = 0;
    for (uint8_t i = 0; i < sr.count; i++) {
      // 최신 표본이 오른쪽 끝에 오도록 배치한다.
      int16_t x = GX + GW - 1 - (int32_t)(sr.count - 1 - i) * (GW - 1) / (HIST_N - 1);
      int16_t y = GY + GH - 1 - (int32_t)(seriesAt(sr, i) - lo) * (GH - 1) / (hi - lo);
      if (i) tft.drawLine(px, py, x, y, col);
      px = x; py = y;
    }
    tft.fillCircle(px, py, 2, col);        // 최신 값 표시
  }

  // ── 제목 + 범례 (기기 이름과 현재 온도) ──
  tft.setTextColor(COL_TXT, COL_BG);
  tft.drawString(head, 6, 4);

  int16_t lx = 78;
  for (uint8_t n = 0; n < WEBSOCKETS_SERVER_CLIENT_MAX && lx < SCR_W - 40; n++) {
    if (!seriesOf(n, kind).count) continue;
    uint16_t col = lineColor(n);
    tft.fillRect(lx, 9, 10, 3, col);
    char lg[40];
    snprintf(lg, sizeof(lg), "%s %.1f", slot[n].name,
             isH ? slot[n].lastHumi : slot[n].lastTemp);
    tft.setTextColor(col, COL_BG);
    tft.drawString(lg, lx + 14, 4);
    lx += 16 + tft.textWidth(lg);
  }

  // ── 안내 ──
  tft.setTextColor(COL_DIM, COL_BG);
  char foot[56];
  snprintf(foot, sizeof(foot),
           fontOk ? "다시 누르면 값 화면  ·  표본 %d개" : "press again: values  ·  %d pts",
           samples);
  tft.drawString(foot, 6, SCR_H - 16);
}

// 현재 모드에 맞는 화면을 그린다.
static void render() {
  if (screenDirty) { chromeDrawn = false; screenDirty = false; }
  if (screen == SCR_VALUES) renderValues();
  else                      renderGraph(screen);
}

// ── 버튼 ──────────────────────────────────────────────────────────
//   위(GPIO35)  → 온도 그래프. 온도 그래프에서 또 누르면 값 화면.
//   아래(GPIO0) → 습도 그래프. 습도 그래프에서 또 누르면 값 화면.
// 눌림은 둘 다 LOW. 200ms 잠금으로 채터링을 막는다.
static void handleButtons() {
  static uint32_t lockUntil = 0;
  if (millis() < lockUntil) return;

  bool top = (digitalRead(BTN_TOP) == LOW);
  bool bot = (digitalRead(BTN_BOTTOM) == LOW);
  if (!top && !bot) return;

  lockUntil = millis() + 200;
  Screen want = top ? SCR_TEMP : SCR_HUMI;
  screen = (screen == want) ? SCR_VALUES : want;   // 같은 버튼을 또 누르면 되돌아간다
  screenDirty = true;

  const char* nm = (screen == SCR_TEMP) ? "온도 그래프"
                 : (screen == SCR_HUMI) ? "습도 그래프" : "값 화면";
  Serial.printf("[버튼] %s → %s\n", top ? "위" : "아래", nm);
  render();
}

// ══════════════════════════════════════════════════════════════════
//  수신
// ══════════════════════════════════════════════════════════════════
static void handlePayload(uint8_t num, uint8_t* payload, size_t length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("JSON 파싱 실패: %s\n", err.c_str());
    copyUtf8(msg, sizeof(msg), "형식 오류");
    return;
  }

  if (doc["title"].is<const char*>()) copyUtf8(title, sizeof(title), doc["title"].as<const char*>());
  if (doc["msg"].is<const char*>())   copyUtf8(msg,   sizeof(msg),   doc["msg"].as<const char*>());
  if (doc["temp"].is<float>()) { temp = doc["temp"].as<float>(); hasTemp = true; }
  if (doc["humi"].is<float>()) { humi = doc["humi"].as<float>(); hasHumi = true; }

  // 기기별로 따로 쌓는다 — 그래프는 이 이력으로 그린다.
  if (num < WEBSOCKETS_SERVER_CLIENT_MAX && slot[num].used) {
    if (doc["title"].is<const char*>())
      copyUtf8(slot[num].name, sizeof(slot[num].name), doc["title"].as<const char*>());
    if (doc["temp"].is<float>()) {
      slot[num].lastTemp = doc["temp"].as<float>(); slot[num].hasTemp = true;
      pushSeries(slot[num].t, slot[num].lastTemp);
    }
    if (doc["humi"].is<float>()) {
      slot[num].lastHumi = doc["humi"].as<float>(); slot[num].hasHumi = true;
      pushSeries(slot[num].h, slot[num].lastHumi);
    }
  }

  haveData  = true;
  lastMsgMs = millis();
  rxCount++;
  Serial.printf("수신 #%lu  온도=%.1f 습도=%.1f  제목=%s\n",
                (unsigned long)rxCount, temp, humi, title);
}

#if USE_WS_SERVER
// 센서 보드(ESP32-C3)가 붙었을 때. 접속 수를 세어 상태바에 보여준다.
static void onWsServerEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      if (num >= WEBSOCKETS_SERVER_CLIENT_MAX) break;
      IPAddress ip = wsServer.remoteIP(num);
      slot[num] = SensorSlot{};
      slot[num].used = true;
      snprintf(slot[num].name, sizeof(slot[num].name), "센서 %u", num + 1);
      if (sensors < 255) sensors++;
      Serial.printf("[서버] 센서 접속 #%u  %s  (총 %u대)\n",
                    num, ip.toString().c_str(), sensors);
      requestReading(num);                                 // 붙자마자 첫 값을 받아 온다
      lastPoll = millis();                                 // 다음 순번은 5초 뒤부터
      rrNext   = (num + 1) % WEBSOCKETS_SERVER_CLIENT_MAX;
      break;
    }
    case WStype_DISCONNECTED:
      if (num < WEBSOCKETS_SERVER_CLIENT_MAX && slot[num].used) {
        slot[num].used = false;
        if (sensors > 0) sensors--;
      }
      Serial.printf("[서버] 센서 끊김 #%u  (총 %u대)\n", num, sensors);
      break;
    case WStype_TEXT:
      if (num < WEBSOCKETS_SERVER_CLIENT_MAX && slot[num].used) {
        slot[num].answered++;
        if (slot[num].reqAt) {
          Serial.printf("[서버] #%u 응답 (왕복 %lums)\n",
                        num, (unsigned long)(millis() - slot[num].reqAt));
          slot[num].reqAt = 0;
        }
      }
      handlePayload(num, payload, length);
      break;
    default:
      break;
  }
}
#endif

#if USE_WS_CLIENT
static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsUp = true;
      Serial.println("[WS] 연결됨");
      webSocket.sendTXT("{\"hello\":\"ttgo-t-display\"}");
      break;
    case WStype_DISCONNECTED:
      wsUp = false;
      Serial.println("[WS] 끊김");
      break;
    case WStype_TEXT:
      handlePayload(0xFF, payload, length);   // 슬롯 없음(바깥 서버 경로)
      break;
    case WStype_ERROR:
      Serial.println("[WS] 오류");
      break;
    default:
      break;
  }
}
#endif

#if USE_WS_SERVER
// 특정 기기 한 대에게만 "지금 측정해서 보내라"고 요청한다.
static void requestReading(uint8_t num) {
  if (num >= WEBSOCKETS_SERVER_CLIENT_MAX || !slot[num].used) return;
  slot[num].lastReq = millis();   // 이 기기가 마지막으로 호출된 시각(참고용)
  slot[num].reqAt   = millis();
  slot[num].sent++;
  wsServer.sendTXT(num, "{\"cmd\":\"read\"}");
}

// REQUEST_MS 마다 딱 한 대만 호출한다. rrNext 부터 시계방향으로 돌며
// 실제로 붙어 있는 다음 기기를 찾는다(중간 번호가 비어 있어도 건너뛴다).
static void pollSensors() {
  if (sensors == 0) return;
  if (millis() - lastPoll < REQUEST_MS) return;

  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    uint8_t n = (rrNext + i) % WEBSOCKETS_SERVER_CLIENT_MAX;
    if (!slot[n].used || !wsServer.clientIsConnected(n)) continue;
    rrNext   = (n + 1) % WEBSOCKETS_SERVER_CLIENT_MAX;   // 다음 차례로 넘긴다
    lastPoll = millis();
    requestReading(n);
    return;                                              // 이번 주기엔 여기까지
  }
}

// 화면에 보여줄 합계
static void requestTotals(uint32_t& sent, uint32_t& answered) {
  sent = answered = 0;
  for (uint8_t n = 0; n < WEBSOCKETS_SERVER_CLIENT_MAX; n++)
    if (slot[n].used) { sent += slot[n].sent; answered += slot[n].answered; }
}
#endif

// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(BTN_TOP, INPUT);            // GPIO35 는 입력 전용 — 보드의 외부 풀업을 쓴다
  pinMode(BTN_BOTTOM, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  initColors();

  // 한글 전체 폰트는 부팅 때 한 번만 올리고 계속 유지한다.
  // 매 프레임 load/unload 하면 메트릭 11,267개를 다시 읽느라 화면이 멈춘다.
  //
  // TFT_eSPI 의 loadFont() 는 malloc 결과를 검사하지 않는다. 힙이 모자라면
  // 널 포인터에 그대로 써서 부팅 루프에 빠지므로, 여기서 먼저 막는다.
  Serial.printf("폰트 적재 전  여유 힙=%u  최대블록=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (ESP.getFreeHeap() >= FONT_HEAP_NEED && ESP.getMaxAllocHeap() >= FONT_BLOCK_NEED) {
    tft.loadFont(FontKRFull12);
    fontOk = true;
    Serial.printf("폰트 적재 완료 여유 힙=%u\n", ESP.getFreeHeap());
  } else {
    Serial.println("힙 부족 — 한글 폰트를 올리지 않는다(ASCII 내장 폰트로 동작).");
  }

  // 큰 숫자용 소형 스프라이트(약 10KB)에 38px 숫자 폰트를 물려 둔다.
  numSpr.setColorDepth(16);
  numSprOk = (numSpr.createSprite(NUM_W, NUM_H) != nullptr);
  if (numSprOk) numSpr.loadFont(FontNum38);
  Serial.printf("숫자 스프라이트: %s, 여유 힙: %u B\n",
                numSprOk ? "생성" : "실패(숫자 생략)", ESP.getFreeHeap());

  drawChrome();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  copyUtf8(title, sizeof(title), fontOk ? "무선 연결 중" : "connecting...");
  render();
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.printf("\n무선 연결됨  ip=%s  여유 힙=%u B\n",
                WiFi.localIP().toString().c_str(), ESP.getFreeHeap());

#if USE_WS_SERVER
  // 센서 보드가 IP 대신 이름으로 찾을 수 있게 mDNS 로 광고한다.
  if (MDNS.begin(DISPLAY_HOSTNAME)) {
    MDNS.addService("ws", "tcp", DISPLAY_WS_PORT);
    Serial.printf("mDNS: %s.local:%u\n", DISPLAY_HOSTNAME, DISPLAY_WS_PORT);
  } else {
    Serial.println("mDNS 시작 실패 — 센서는 대체 IP 로 붙어야 한다.");
  }
  wsServer.begin();
  wsServer.onEvent(onWsServerEvent);
  Serial.printf("WebSocket 서버 열림  ws://%s:%u/\n",
                WiFi.localIP().toString().c_str(), DISPLAY_WS_PORT);
#endif

#if USE_WS_CLIENT
#if USE_TLS
  webSocket.beginSSL(WS_HOST, WS_PORT, WS_PATH);
#else
  webSocket.begin(WS_HOST, WS_PORT, WS_PATH);
#endif
  webSocket.onEvent(onWsEvent);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(15000, 3000, 2);
#endif

  copyUtf8(title, sizeof(title), fontOk ? "대기 중" : "waiting");
  render();
}

void loop() {
#if USE_WS_SERVER
  wsServer.loop();                  // 논블로킹 — delay() 를 넣지 말 것

  pollSensors();   // 5초마다 순번대로 한 대씩
#endif
#if USE_WS_CLIENT
  webSocket.loop();
#endif

  handleButtons();

  static uint32_t lastDraw = 0;
  static uint32_t lastRx   = 0;
  if (screen != SCR_VALUES) {
    // 그래프는 새 표본이 들어올 때만 다시 그린다(매초 다시 그리면 깜빡인다).
    if (rxCount != lastRx) { lastRx = rxCount; render(); }
  } else if (rxCount != lastRx || millis() - lastDraw >= 1000) {
    lastRx   = rxCount;
    lastDraw = millis();
    render();
  }
}
