// ChurchDisplayRxBLE — TTGO T-Display (ESP32)
// ChurchDisplayRx 의 블루투스(BLE) 판. WiFi 를 전혀 쓰지 않는다.
//
// 이 보드가 BLE 페리페럴(GATT 서버)로 광고하고, 센서 보드가 센트럴로 붙는다.
// WiFi 판에서 이 보드가 WebSocket 서버였던 것과 같은 역할 배치다.
//
// ── GATT 구성 ─────────────────────────────────────────────────────
//   서비스   7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1001
//     CMD    ...1002  NOTIFY  — 디스플레이 → 센서 : {"cmd":"read"}
//     DATA   ...1003  WRITE   — 센서 → 디스플레이 : 측정값 JSON
//
// ── 요청/응답 ─────────────────────────────────────────────────────
//   5초마다 한 대씩 순번대로 CMD 를 notify 한다(브로드캐스트가 아니라
//   연결 핸들을 지정해 그 기기에게만 간다). 받은 센서는 그때 측정해서
//   DATA 에 JSON 을 쓴다.
//     {"title":"외부 센서","temp":24.6,"humi":51.2,"msg":"정상 측정"}
//
// ── 버튼 ──────────────────────────────────────────────────────────
//   위(GPIO35)  온도 그래프 / 아래(GPIO0) 습도 그래프 / 같은 버튼 다시 = 값 화면
//
// 필요 라이브러리: TFT_eSPI(Setup25) · NimBLE-Arduino 2.x · ArduinoJson 7.x
// 파티션은 Huge APP(3MB) — 한글 전체 폰트 1.82MB 때문. sketch.yaml 에 있다.
//
// 주의: BLE 기본 MTU 는 23바이트(실효 20B)라 위 JSON(약 70B)이 안 들어간다.
//       이쪽에서 setMTU(247) 로 열어두지만, 실제 확장은 센트럴이 MTU 교환을
//       요청해야 성립한다. 센서 쪽에서 반드시 MTU 를 올릴 것.

#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include "FontKRFull12.h"
#include "FontNum38.h"

// 요청 간격(ms). 한 번에 한 대씩, 순번대로 돌아가며 호출한다.
//   1번 기기 → 5초 → 2번 기기 → 5초 → 1번 기기 → ...
// N대가 붙으면 각 기기가 다시 호출되기까지는 REQUEST_MS × N 이 걸린다.
#define REQUEST_MS 5000

#define BLE_NAME      "churchdisplay"
#define UUID_SERVICE  "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1001"
#define UUID_CMD      "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1002"
#define UUID_DATA     "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e1003"

// 동시 접속 가능한 센서 수. NimBLE 기본값(3)을 그대로 쓴다.
// 더 붙이려면 nimconfig.h 의 CONFIG_BT_NIMBLE_MAX_CONNECTIONS 를 올려야 한다.
#define MAX_SENSORS CONFIG_BT_NIMBLE_MAX_CONNECTIONS

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
NimBLEServer*         bleServer = nullptr;
NimBLECharacteristic* chrCmd    = nullptr;   // 디스플레이 → 센서 (NOTIFY)
NimBLECharacteristic* chrData   = nullptr;   // 센서 → 디스플레이 (WRITE)

static uint8_t  sensors   = 0;       // 붙어 있는 센서 보드 수
// 값 하나짜리 원형 버퍼. 온도와 습도가 각각 하나씩 쓴다.
struct Series {
  int16_t v[HIST_N];
  uint8_t count;   // 채워진 개수 (최대 HIST_N)
  uint8_t head;    // 다음에 쓸 위치
};

// 기기별 상태. BLE 연결 핸들을 슬롯 번호로 바꿔 관리한다.
struct SensorSlot {
  bool     used;
  uint16_t conn;       // BLE 연결 핸들 — notify 를 이 기기에게만 보내는 데 쓴다
  bool     subscribed; // CMD 특성을 구독했는지(구독 전 notify 는 의미 없다)
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
static SensorSlot slot[MAX_SENSORS] = {};
// .ino 의 자동 프로토타입 생성은 클래스 정의 뒤의 함수를 놓칠 때가 있어
// 아래 세 개는 직접 선언해 둔다.
static void requestReading(uint8_t num);
static void pollSensors();
static void requestTotals(uint32_t& sent, uint32_t& answered);

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
  if (sensors > 0) {
    snprintf(state, sizeof(state), fontOk ? "센서 %u대 연결" : "SENSOR %u", sensors);
    stateCol = COL_OK;
  } else {
    snprintf(state, sizeof(state), fontOk ? "센서 대기 중" : "WAITING");
    stateCol = COL_ERR;
  }
  tft.setTextColor(stateCol, COL_BAR);
  tft.setTextPadding(126);
  tft.drawString(state, 6, 3);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COL_DIM, COL_BAR);
  tft.setTextPadding(90);
  tft.drawString(fontOk ? "블루투스" : "BLE", SCR_W - 6, 3);

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
    requestTotals(sent, answered);
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
  for (uint8_t n = 0; n < MAX_SENSORS; n++) {
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
  for (uint8_t n = 0; n < MAX_SENSORS; n++) {
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
  for (uint8_t n = 0; n < MAX_SENSORS && lx < SCR_W - 40; n++) {
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
  if (num < MAX_SENSORS && slot[num].used) {
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

// ══════════════════════════════════════════════════════════════════
//  BLE — 연결 관리와 수신
// ══════════════════════════════════════════════════════════════════
// 연결 핸들로 슬롯을 찾는다. WebSocket 판의 클라이언트 번호(num)에 해당한다.
static int slotOf(uint16_t conn) {
  for (uint8_t n = 0; n < MAX_SENSORS; n++)
    if (slot[n].used && slot[n].conn == conn) return n;
  return -1;
}

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& info) override {
    for (uint8_t n = 0; n < MAX_SENSORS; n++) {
      if (slot[n].used) continue;
      slot[n] = SensorSlot{};
      slot[n].used = true;
      slot[n].conn = info.getConnHandle();
      snprintf(slot[n].name, sizeof(slot[n].name), "센서 %u", n + 1);
      sensors++;
      Serial.printf("[BLE] 센서 접속 #%u  %s  (총 %u대)\n",
                    n, info.getAddress().toString().c_str(), sensors);
      break;
    }
    // 자리가 남아 있으면 계속 광고해 다른 센서도 붙을 수 있게 한다.
    if (sensors < MAX_SENSORS) NimBLEDevice::startAdvertising();
    screenDirty = true;
  }

  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& info, int reason) override {
    int n = slotOf(info.getConnHandle());
    if (n >= 0) { slot[n].used = false; if (sensors) sensors--; }
    Serial.printf("[BLE] 센서 끊김 #%d (사유 %d, 총 %u대)\n", n, reason, sensors);
    NimBLEDevice::startAdvertising();      // 다시 붙을 수 있게
    screenDirty = true;
  }
};

class CmdCB : public NimBLECharacteristicCallbacks {
  // 센서가 CMD 를 구독해야 notify 가 실제로 전달된다.
  void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& info, uint16_t sub) override {
    int n = slotOf(info.getConnHandle());
    if (n < 0) return;
    slot[n].subscribed = (sub > 0);
    Serial.printf("[BLE] #%d 구독 %s\n", n, sub ? "시작" : "해제");
    if (slot[n].subscribed) requestReading(n);   // 준비되면 바로 첫 값을 받아 온다
  }
};

class DataCB : public NimBLECharacteristicCallbacks {
  // 센서가 측정값 JSON 을 여기에 쓴다.
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    int n = slotOf(info.getConnHandle());
    NimBLEAttValue v = c->getValue();
    if (n >= 0) {
      slot[n].answered++;
      if (slot[n].reqAt) {
        Serial.printf("[BLE] #%d 응답 (왕복 %lums)\n",
                      n, (unsigned long)(millis() - slot[n].reqAt));
        slot[n].reqAt = 0;
      }
    }
    handlePayload(n < 0 ? 0xFF : (uint8_t)n, (uint8_t*)v.data(), v.size());
  }
};

static ServerCB serverCB;
static CmdCB    cmdCB;
static DataCB   dataCB;

// ══════════════════════════════════════════════════════════════════
//  요청 — 5초마다 한 대씩 순번대로
// ══════════════════════════════════════════════════════════════════
// 특정 기기 한 대에게만 "지금 측정해서 보내라"고 notify 한다.
// 인자 없는 notify() 는 구독자 전체에 가므로 반드시 연결 핸들을 넘긴다.
static void requestReading(uint8_t num) {
  if (num >= MAX_SENSORS || !slot[num].used || !slot[num].subscribed) return;
  slot[num].lastReq = millis();   // 이 기기가 마지막으로 호출된 시각(참고용)
  slot[num].reqAt   = millis();
  slot[num].sent++;
  static const char* REQ = "{\"cmd\":\"read\"}";
  chrCmd->notify((const uint8_t*)REQ, strlen(REQ), slot[num].conn);
}

// REQUEST_MS 마다 딱 한 대만 호출한다. rrNext 부터 시계방향으로 돌며
// 실제로 붙어 있는 다음 기기를 찾는다(중간 번호가 비어 있어도 건너뛴다).
static void pollSensors() {
  if (sensors == 0) return;
  if (millis() - lastPoll < REQUEST_MS) return;

  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    uint8_t n = (rrNext + i) % MAX_SENSORS;
    if (!slot[n].used || !slot[n].subscribed) continue;
    rrNext   = (n + 1) % MAX_SENSORS;   // 다음 차례로 넘긴다
    lastPoll = millis();
    requestReading(n);
    return;                             // 이번 주기엔 여기까지
  }
}

// 화면에 보여줄 합계
static void requestTotals(uint32_t& sent, uint32_t& answered) {
  sent = answered = 0;
  for (uint8_t n = 0; n < MAX_SENSORS; n++)
    if (slot[n].used) { sent += slot[n].sent; answered += slot[n].answered; }
}

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

  copyUtf8(title, sizeof(title), fontOk ? "블루투스 준비 중" : "starting BLE...");
  render();

  // ── BLE 페리페럴(GATT 서버) ──
  NimBLEDevice::init(BLE_NAME);
  // 기본 MTU 23 으로는 실효 20바이트뿐이라 측정 JSON(약 70B)이 안 들어간다.
  // 이쪽에서 크게 열어두고, 실제 확장은 센트럴이 MTU 교환을 요청해 성립시킨다.
  NimBLEDevice::setMTU(247);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(&serverCB);
  bleServer->advertiseOnDisconnect(true);

  NimBLEService* svc = bleServer->createService(UUID_SERVICE);
  chrCmd  = svc->createCharacteristic(UUID_CMD,  NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  chrData = svc->createCharacteristic(UUID_DATA, NIMBLE_PROPERTY::WRITE  | NIMBLE_PROPERTY::WRITE_NR);
  chrCmd->setCallbacks(&cmdCB);
  chrData->setCallbacks(&dataCB);
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(BLE_NAME);
  adv->addServiceUUID(UUID_SERVICE);
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.printf("BLE 광고 시작  이름=%s  주소=%s\n",
                BLE_NAME, NimBLEDevice::getAddress().toString().c_str());
  Serial.printf("  서비스 %s\n  CMD %s (notify)\n  DATA %s (write)\n",
                UUID_SERVICE, UUID_CMD, UUID_DATA);
  Serial.printf("BLE 준비 후 여유 힙=%u B\n", ESP.getFreeHeap());

  copyUtf8(title, sizeof(title), fontOk ? "대기 중" : "waiting");
  render();
}

void loop() {
  // NimBLE 는 자체 태스크로 돌아가므로 여기서 스택을 돌릴 필요가 없다.
  // 다만 delay() 로 오래 막으면 화면과 버튼이 굳으니 논블로킹을 유지한다.
  pollSensors();   // 5초마다 순번대로 한 대씩

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
