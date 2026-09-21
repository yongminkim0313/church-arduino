// nfcProject — WiFi WebSocket 디스플레이 (CST820 원형)
//
// 보드: LILYGO T-RGB 2.1" 원형 (ESP32-S3 · ST7701S RGB 480×480 · CST820 터치)
// 이 보드는 화면만 담당한다 —
//   1) config.h 의 와이파이에 붙는다
//   2) nfc-display.local 로 자기 이름을 알린다(mDNS)
//   3) 포트 81 에 WebSocket 서버를 연다
//   4) 외부(NFC 시스템)가 붙어 명령을 보내면 그에 맞춰 화면을 바꾼다
//
// ── 화면 세 가지 ────────────────────────────────────────────────────
//   1) 대기 화면  : 항상 '사용 활성'(useEnable) 그림에서 시작한다. 비활성 그림은 더 쓰지 않는다.
//                   화면을 짧게 누르면(또는 시리얼 'n') microSD 에 받아 둔 사진이 순서대로 넘어간다 —
//                   활성 → 사진1 → 사진2 → … → 사진N → 다시 활성.
//   2) 원격 전환  : WebSocket 으로 enable(=활성으로) / toggle·next(=다음 사진) 가 오면 바뀐다.
//   3) NFC 화면   : 클라이언트가 카드 UID·이름·잔여포인트를 보내면, 그 UID 의 사진을
//                   전체화면으로 덮고 위에 이름(상단)·잔여 포인트(하단)를 얹는다.
//                   몇 초 뒤(NFC_SHOW_MS) 자동으로 대기 화면으로 돌아간다.
//                   UID 사진은 microSD 의 /talent/<UID>.565 를 읽는다(없으면 내장 MEMBERS[]).
//
// ── 사진 동기화 (화면 길게 누르기) ──────────────────────────────────
//   화면을 LONG_PRESS_MS 이상 누르면 펀펀포인트 서버(config.h 의 TALENT_SERVER)에서
//   이름표(GET /api/talent/roster?px=480)를 받아, 사진이 있는 UID 마다 480 원형 사진
//   (GET /api/talent/photo/device/<img>)을 내려받아 microSD 에 /talent/<UID>.565 로 저장한다.
//   요청에는 x-talent-key 헤더(TALENT_DEVICE_KEY)를 실어 서버가 기기를 확인한다.
//   이미 받은 사진은 다시 받지 않는다 — 이름표의 img(ph-<사진 해시>-480.565)는 사진이 바뀔 때만
//   달라지므로, 받을 때 그 값을 /talent/<UID>.ver 에 적어 두고 다음 동기화에서 같으면 건너뛴다.
//   서버가 리틀엔디안 RGB565 로 주므로 파일 바이트를 그대로 화면 버퍼로 읽으면 된다.
//
// ── 화면이 켜지는 순서 (v1.0 과 같다) ───────────────────────────────
//   1) XL9535 확장칩   화면 명령 줄(CS·SCK·MOSI)과 전원 인에이블이 여기 붙어 있다
//   2) RGB 패널        ESP32 가 16개 데이터선으로 픽셀을 계속 흘려보낸다(PSRAM 프레임버퍼)
//   3) 초기화 표       ST7701S 에 "이런 화면이다" 를 알려 준다(2.1인치는 type4)
//   4) 백라이트        GPIO46 을 켜야 보인다. 밝기는 '흔든 횟수' 로 정한다(BL_LEVEL)
//
// ── WebSocket 규약 (ws://nfc-display.local:81/) ─────────────────────
//   보내는 쪽 → 보드
//     · 대기 화면 사진 바꾸기
//         JSON  {"state":"enable"} / {"state":"toggle"} / {"state":"next"} / {"state":"prev"}
//         글자  "enable" / "toggle" / "next" / "prev"   (enable = 활성 화면으로, toggle·next = 다음 사진)
//         ※ "disable"·{"enabled":false} 는 예전 규약이다 — 비활성 그림을 걷어내 활성 화면으로 간다.
//     · NFC 태그 알림 (이 UID 사진 + 이름 + 잔여포인트를 띄운다)
//         JSON  {"uid":"04A1B2C3","name":"홍길동","points":1200}
//   보드 → 붙은 쪽 : 상태가 바뀌거나 새로 붙을 때마다
//     · JSON  {"mode":"idle","photo":"enable","index":0,"count":12,"online":true}   (photo = enable 또는 사진 UID)
//     · JSON  {"mode":"nfc","uid":"04A1B2C3","online":true}
//
// ── 빌드 (Arduino IDE 도구 메뉴) ────────────────────────────────────
//   보드: ESP32S3 Dev Module   PSRAM: OPI PSRAM   Flash Size: 16MB
//   Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)  ← 그림과 한글 폰트가 플래시에 들어간다
//   USB CDC On Boot: Enabled
//   라이브러리: GFX Library for Arduino · ArduinoJson · WebSockets(by Markus Sattler)
//   한글: GFX 라이브러리에 딸린 u8g2_font_quan7_h_cjk 폰트를 쓴다(한글 전체 포함).
//
//   ※ LilyGo-T-RGB 라이브러리(LV_Helper.h·LilyGo_RGBPanel.h)는 넣지 말 것 — lvgl/SensorLib
//     버전 충돌로 빌드가 깨진다. 이 스케치는 Arduino_GFX 로 직접 그린다(v1.0 과 같은 이유).
//
// ── 시리얼 명령 (115200) ────────────────────────────────────────────
//   n 다음 사진 · p 이전 사진 · h 활성 화면으로 · s 사진 목록 · l SD 파일 목록 · r 재부팅 · ? 도움말
//
// ── 그림 바꾸기 ─────────────────────────────────────────────────────
// 그림 헤더는 tools/img2rgb565_dither.py 로 만든다(v1.0 머리말 참고). 자기 그림으로 바꾸려면
// useEnable.h 를 --var USEENABLE 로 다시 만들어 이 폴더에 덮어쓴다.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>          // 서버에서 사진 내려받기(길게 누르기)
#include <WiFiClientSecure.h>    // https 다운로드
#include <SD_MMC.h>             // microSD (SDMMC 1-bit) 저장
#include <U8g2lib.h>             // 이게 있어야 Arduino_GFX 가 한글 u8g2 폰트(quan7)·UTF8 출력을 켠다
#include <Arduino_GFX_Library.h>
#include "config.h"              // WIFI_SSID · WIFI_PASSWORD · MDNS_HOSTNAME · WS_PORT
#include "useEnable.h"           // USEENABLE[480×480]  — 사용 활성(대기 화면의 첫 장)
#include "connected.h"           // CONNECTED[50×35]    — 연결 아이콘(와이파이에 붙었을 때만 겹친다)

// ── 연결 아이콘 위치 ────────────────────────────────────────────────
// (CONN_X, CONN_Y) 가 아이콘의 왼쪽 위 모서리. 화면은 480×480, 왼쪽 위가 (0, 0).
static const int CONN_X = 307;
static const int CONN_Y = 30;

// ── client 연결 상태 점 ─────────────────────────────────────────────
// 화면 상단 가운데에 작은 점 하나로 client(NFC 시스템) 연결 상태를 보여 준다.
//   초록 = client 가 WebSocket 으로 붙어 있다(정상 연결)
//   노랑 = 와이파이·서버는 떴는데 아직 붙은 client 가 없다(대기 중)
//   빨강 = 와이파이가 없어 서버에 붙을 수 없다(연결 실패)
static const int DOT_CX = 240;   // 점 가운데 x (화면 상단 가운데)
static const int DOT_CY = 22;    // 점 가운데 y
static const int DOT_R  = 8;     // 점 반지름
#define DOT_GREEN  0x07E0
#define DOT_YELLOW 0xFFE0
#define DOT_RED    0xF800

// ── 핀 ──────────────────────────────────────────────────────────────
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL  48
#define PIN_BL       46          // 백라이트
#define TOUCH_ADDR   0x15        // CST820 의 I2C 주소

// ── 밝기 ────────────────────────────────────────────────────────────
// PWM 이 아니다. 핀을 짧게 흔든 횟수로 16단계 중 하나를 고른다(LILYGO 보드 방식).
#define BL_LEVEL 4               // ← 1(가장 어둡다) … 16(가장 밝다)

static void backlightOn(uint8_t level) {
  if (level < 1)  level = 1;
  if (level > 16) level = 16;
  digitalWrite(PIN_BL, HIGH);                      // 16단계에서 시작
  delayMicroseconds(30);
  for (uint8_t i = 0; i < (uint8_t)(16 - level); i++) {
    digitalWrite(PIN_BL, LOW);                     // 한 번 흔들 때마다 한 단계 어둡게
    digitalWrite(PIN_BL, HIGH);
  }
}

// ── 화면 객체 (v1.0 과 동일한 값) ───────────────────────────────────
Arduino_XL9535SWSPI *bus = new Arduino_XL9535SWSPI(
    PIN_I2C_SDA, PIN_I2C_SCL,
    2 /* 전원 인에이블 */, 3 /* CS */, 5 /* SCK */, 4 /* MOSI */);

// 픽셀 클럭 8MHz · 중계 버퍼 열 줄 — 화면이 자글거리는 것을 막는다(v1.0 PanelTFT 와 같다).
static const int32_t PCLK_HZ      = 8000000L;
static const int     BOUNCE_LINES = 10;

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    45 /* DE */, 41 /* VSYNC */, 47 /* HSYNC */, 42 /* PCLK */,
    21 /* R0 */, 18 /* R1 */, 17 /* R2 */, 16 /* R3 */, 15 /* R4 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    7  /* B0 */, 6  /* B1 */, 5  /* B2 */, 3  /* B3 */, 2  /* B4 */,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 1 /* hsync_pulse_width */, 30 /* hsync_back_porch */,
    1 /* vsync_polarity */, 20 /* vsync_front_porch */, 1 /* vsync_pulse_width */, 30 /* vsync_back_porch */,
    1 /* pclk_active_neg */,
    PCLK_HZ /* 픽셀 클럭 */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    480 * BOUNCE_LINES /* 중계 버퍼(픽셀 수) */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0 /* 회전 */, true /* 그릴 때마다 바로 반영 */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_type4_init_operations, sizeof(st7701_type4_init_operations));

// ── WebSocket 서버 ──────────────────────────────────────────────────
WebSocketsServer webSocket = WebSocketsServer(WS_PORT);

// ── 대기 화면 사진 목록 (터치·시리얼로 순차 전환) ────────────────────
// 0번은 언제나 활성 그림(내장 USEENABLE), 1번부터는 microSD /talent/<UID>.565 사진이다.
// 목록은 부팅 때와 동기화(길게 누르기) 뒤에 SD 를 훑어 다시 만든다(scanPhotos). 이름 순 정렬.
#define MAX_PHOTOS 200
static char photoUids[MAX_PHOTOS][24];   // SD 사진의 UID(파일 이름에서 .565 를 뗀 것)
static int  sdPhotoCount = 0;
static int  bufIdx = -1;                 // photoBuf 에 지금 담긴 대기 사진 번호(-1 = 모름) — 같은 사진을 또 읽지 않게

// ── UID 별 사진 (NFC 태그 시) ───────────────────────────────────────
// 지금은 기기에 내장한 사진을 UID 에 연결한다. microSD 를 넣으면 loadPhotoForUid() 안에서
// 카드 파일(/sd/<uid>.raw · 480*480*2 바이트 RGB565)을 읽어 오도록 바꾸면 된다.
struct Member { const char *uid; const uint16_t *photo; };
static const Member MEMBERS[] = {
  { "04A1B2C3", USEENABLE },     // ← 예시. 실제 카드 UID·사진으로 교체하세요
};
static const int MEMBER_COUNT = sizeof(MEMBERS) / sizeof(MEMBERS[0]);

// ── microSD ─────────────────────────────────────────────────────────
static bool      sdReady  = false;
static uint16_t *photoBuf = nullptr;   // SD 에서 읽은 사진 한 장(480×480 RGB565) — loadPhotoForUid 가 채운다

// UID 로 사진 한 장을 고른다.
//   1) microSD 의 /talent/<UID>.565 (길게 눌러 서버에서 받아 둔 것) — 있으면 이걸 쓴다
//   2) 내장 MEMBERS[] 표
//   3) 그래도 없으면 기본 사진(useDisable)
// SD 파일은 서버가 리틀엔디안 RGB565 로 저장했고, ESP32 도 리틀엔디안이라 바이트를 그대로 읽으면
// 네이티브 RGB565 값이 된다(내장 헤더의 값과 같은 방식). draw16bitRGBBitmap 이 그대로 그린다.
static const uint16_t *loadPhotoForUid(const char *uid) {
  if (sdReady && photoBuf) {
    char path[48];
    snprintf(path, sizeof(path), "/talent/%s.565", uid);
    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
      const size_t need = 480 * 480 * sizeof(uint16_t);
      bufIdx = -1;                             // 버퍼를 NFC 사진으로 덮는다 — 대기 사진은 다시 읽어야 한다
      if ((size_t)f.size() == need && f.read((uint8_t *)photoBuf, need) == need) {
        f.close();
        return photoBuf;
      }
      f.close();
    }
  }
  for (int i = 0; i < MEMBER_COUNT; i++)
    if (!strcasecmp(uid, MEMBERS[i].uid)) return MEMBERS[i].photo;
  return USEENABLE;
}

// ── 화면 색·글자 (NFC 오버레이) ─────────────────────────────────────
#define COL_WHITE 0xFFFF
#define COL_GOLD  0xFEA0         // 잔여 포인트 숫자 색
#define BAND_TOP_H 96            // 상단 이름 띠 높이
#define BAND_BOT_Y 372           // 하단 포인트 띠 시작 y
static const uint32_t NFC_SHOW_MS = 6000;   // NFC 화면을 몇 ms 보여 준 뒤 대기 화면으로 돌아갈지

// ── 상태 ────────────────────────────────────────────────────────────
enum Mode { MODE_IDLE, MODE_NFC };
static Mode    mode      = MODE_IDLE;
static int     photoIdx  = 0;    // 대기 화면에서 지금 보이는 사진 번호(0 = 활성 그림, 1.. = SD 사진)
static uint32_t nfcUntil = 0;    // 이 시각(millis)이 지나면 NFC 화면을 접는다
static char    nfcUid[32] = "";

static bool online    = false;   // 화면에 그려 둔 연결 상태(아이콘이 떠 있나)
static bool wasDown   = false;   // 직전에 손가락이 닿아 있었나(누르는 순간을 가려내려고)
static bool serversUp = false;   // mDNS·WebSocket 을 켰나(와이파이가 붙은 뒤 한 번만)
static uint32_t downAt   = 0;    // 손가락이 닿기 시작한 시각(길게/짧게 가려내려고)
static bool     longFired = false; // 이번 누름에서 '길게' 동작을 이미 한 번 했나
static bool     bootSyncDone = false; // 부팅 후 와이파이가 처음 붙었을 때 사진을 한 번 받아 왔나

// ── 합쳐 그리기용 버퍼 ──────────────────────────────────────────────
// 배경을 먼저 그리고 아이콘/띠를 나중에 얹으면 그 틈에 깜빡인다(RGB 화면은 프레임버퍼를
// 초당 수십 번 계속 읽어 간다). 그래서 PSRAM 에 한 장을 두고 배경+아이콘+띠를 먼저 합친 뒤
// 한 번에 옮긴다. 그 위에 글자만 gfx 로 직접 얹는다(글자 영역은 작아 깜빡임이 거의 없다).
static uint16_t *frame = nullptr;

// RGB565 두 색을 섞는다. a=0..255 는 앞 색(fg)의 비중.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
  uint8_t r1 = (fg >> 11) & 0x1F, g1 = (fg >> 5) & 0x3F, b1 = fg & 0x1F;
  uint8_t r2 = (bg >> 11) & 0x1F, g2 = (bg >> 5) & 0x3F, b2 = bg & 0x1F;
  uint16_t r = (r1 * a + r2 * (255 - a)) / 255;
  uint16_t g = (g1 * a + g2 * (255 - a)) / 255;
  uint16_t b = (b1 * a + b2 * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}

// 정수를 천 단위 콤마로 (예: 1200 → "1,200"). NFC 하단 포인트 표기용.
static void commafy(long v, char *out, size_t n) {
  char tmp[16];
  bool neg = v < 0; if (neg) v = -v;
  int len = snprintf(tmp, sizeof(tmp), "%ld", v);
  size_t o = 0;
  if (neg && o < n - 1) out[o++] = '-';
  for (int i = 0; i < len && o < n - 1; i++) {
    if (i > 0 && (len - i) % 3 == 0 && o < n - 1) out[o++] = ',';
    out[o++] = tmp[i];
  }
  out[o] = '\0';
}

// frame 버퍼에 배경 사진 한 장을 통째로 채운다(항상 전체화면 480×480).
static void fillBackground(const uint16_t *bg) {
  memcpy(frame, bg, 480 * 480 * sizeof(uint16_t));
}

// frame 버퍼에 연결 아이콘을 얹는다(투명색은 건너뛴다).
static void overlayConnIcon() {
  for (int y = 0; y < CONNECTED_H; y++) {
    const int fy = CONN_Y + y;
    if (fy < 0 || fy >= 480) continue;
    for (int x = 0; x < CONNECTED_W; x++) {
      const int fx = CONN_X + x;
      if (fx < 0 || fx >= 480) continue;
      const uint16_t c = CONNECTED[y * CONNECTED_W + x];
      if (c != CONNECTED_TRANSPARENT) frame[fy * 480 + fx] = c;
    }
  }
}

// frame 버퍼의 한 가로 띠를 어둡게(반투명 검정처럼) 만든다 — 글자가 사진 위에서 잘 보이게.
static void darkenBand(int y0, int y1, uint8_t keep /*앞(사진)색 비중*/) {
  if (y0 < 0) y0 = 0; if (y1 > 480) y1 = 480;
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < 480; x++)
      frame[y * 480 + x] = blend565(frame[y * 480 + x], 0x0000, keep);
}

// 글자를 (가운데 x = cx) 에 세로 중앙(cy)으로 놓고 그린다. u8g2 한글 폰트 기준.
static void drawCenteredUTF8(const char *s, int cx, int cy, uint8_t size, uint16_t color) {
  gfx->setFont(u8g2_font_quan7_h_cjk);
  gfx->setUTF8Print(true);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(cx - w / 2 - x1, cy - h / 2 - y1);
  gfx->print(s);
}

// 안내 문구 한 줄을 검은 화면 가운데에 띄운다(사진 동기화 진행 상황 등).
static void drawStatus(const char *msg) {
  if (frame) {
    for (int i = 0; i < 480 * 480; i++) frame[i] = 0x0000;
    gfx->draw16bitRGBBitmap(0, 0, frame, 480, 480);
  } else {
    gfx->fillScreen(0x0000);
  }
  drawCenteredUTF8(msg, 240, 240, 3, COL_WHITE);
}

// ── client 연결 상태 점 ─────────────────────────────────────────────
enum LinkState { LINK_FAIL, LINK_WAIT, LINK_OK };
static LinkState shownLink = (LinkState)-1;   // 화면에 그려 둔 점 색(바뀔 때만 다시 그린다)

// 지금 client 연결 상태. 와이파이 없음 → 실패, 붙은 client 있음 → 정상, 그 외 → 대기.
static LinkState linkState() {
  if (!online)                            return LINK_FAIL;
  if (serversUp && webSocket.connectedClients() > 0) return LINK_OK;
  return LINK_WAIT;
}

// 상단 점을 지금 상태 색으로 그린다(어두운/밝은 배경 모두에서 보이게 검은 테두리를 두른다).
static void drawLinkDot() {
  const LinkState ls = linkState();
  shownLink = ls;
  uint16_t col = ls == LINK_OK ? DOT_GREEN : (ls == LINK_WAIT ? DOT_YELLOW : DOT_RED);
  gfx->fillCircle(DOT_CX, DOT_CY, DOT_R, col);
  gfx->drawCircle(DOT_CX, DOT_CY, DOT_R, 0x0000);
}

// ── 화면 그리기 ─────────────────────────────────────────────────────
static int photoTotal() { return 1 + sdPhotoCount; }   // 활성 그림 + SD 사진

// 대기 사진 번호의 이름 — 0 은 "enable", 그 밖은 SD 사진의 UID.
static const char *photoLabel(int idx) { return idx == 0 ? "enable" : photoUids[idx - 1]; }

// 대기 사진 번호의 픽셀을 돌려준다. SD 사진은 photoBuf 로 읽는다. 못 읽으면 nullptr.
static const uint16_t *idlePixels(int idx) {
  if (idx == 0) return USEENABLE;
  if (!sdReady || !photoBuf || idx > sdPhotoCount) return nullptr;
  if (bufIdx == idx) return photoBuf;                  // 이미 읽어 둔 사진
  char path[48];
  snprintf(path, sizeof(path), "/talent/%s.565", photoUids[idx - 1]);
  const size_t need = 480 * 480 * sizeof(uint16_t);
  bufIdx = -1;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  const uint32_t t0 = millis();
  const bool ok = (size_t)f.size() == need && f.read((uint8_t *)photoBuf, need) == need;
  f.close();
  if (!ok) return nullptr;
  bufIdx = idx;
  Serial.printf("[SD] %s 읽음 (%lums)\n", path, (unsigned long)(millis() - t0));
  return photoBuf;
}

// 대기 화면 — 지금 사진 + (붙어 있으면) 연결 아이콘.
static void drawIdle() {
  if (photoIdx < 0 || photoIdx >= photoTotal()) photoIdx = 0;
  const uint16_t *px = idlePixels(photoIdx);
  if (!px) {                                           // SD 사진을 못 읽으면 활성 그림으로 돌아간다
    Serial.printf("대기 화면: 사진 #%d(%s) 을 못 읽어 활성 화면으로\n", photoIdx, photoLabel(photoIdx));
    photoIdx = 0;
    px = USEENABLE;
  }
  if (!frame) {                                        // 버퍼를 못 잡았으면 배경만이라도
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)px, 480, 480);
    drawLinkDot();
    return;
  }
  fillBackground(px);
  if (online) overlayConnIcon();
  gfx->draw16bitRGBBitmap(0, 0, frame, 480, 480);
  drawLinkDot();
  Serial.printf("대기 화면: 사진 %d/%d (%s) · 연결 아이콘 %s\n",
                photoIdx, photoTotal() - 1, photoLabel(photoIdx), online ? "보임" : "숨김");
}

// NFC 화면 — UID 사진 전체화면 + 상단 이름 + 하단 잔여 포인트.
static void drawNfc(const uint16_t *photo, const char *name, long points) {
  if (!frame) {                                        // 버퍼가 없으면 사진만
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)photo, 480, 480);
    drawLinkDot();
    return;
  }
  fillBackground(photo);
  darkenBand(0, BAND_TOP_H, 110);                      // 상단 띠(사진 43% 남기고 어둡게)
  darkenBand(BAND_BOT_Y, 480, 110);                    // 하단 띠
  gfx->draw16bitRGBBitmap(0, 0, frame, 480, 480);      // 사진+띠를 한 번에

  drawCenteredUTF8(name, 240, BAND_TOP_H / 2, 4, COL_WHITE);   // 상단: 이름

  char num[24]; commafy(points, num, sizeof(num));
  char pts[32]; snprintf(pts, sizeof(pts), "%s P", num);
  drawCenteredUTF8("잔여 포인트", 240, BAND_BOT_Y + 30, 2, COL_WHITE);  // 하단: 라벨
  drawCenteredUTF8(pts,          240, BAND_BOT_Y + 74, 4, COL_GOLD);   // 하단: 숫자
  drawLinkDot();

  Serial.printf("NFC 화면: uid=%s 이름=%s 포인트=%ld\n", nfcUid, name, points);
}

// 지금 상태를 붙어 있는 모두(또는 한 client)에게 알린다.
static void sendState(int8_t only = -1) {
  char msg[160];
  if (mode == MODE_NFC)
    snprintf(msg, sizeof(msg), "{\"mode\":\"nfc\",\"uid\":\"%s\",\"online\":%s}",
             nfcUid, online ? "true" : "false");
  else
    snprintf(msg, sizeof(msg), "{\"mode\":\"idle\",\"photo\":\"%s\",\"index\":%d,\"count\":%d,\"online\":%s}",
             photoLabel(photoIdx), photoIdx, sdPhotoCount, online ? "true" : "false");
  if (only < 0) webSocket.broadcastTXT(msg);
  else          webSocket.sendTXT((uint8_t)only, msg);
}

// 대기 화면으로 (다시) 들어간다.
static void goIdle(bool announce = true) {
  mode = MODE_IDLE;
  drawIdle();
  if (announce) sendState();
}

// 대기 화면 사진을 번호로 바꾼다(끝을 넘으면 처음으로 돈다).
static void showPhoto(int idx) {
  const int n = photoTotal();
  photoIdx = ((idx % n) + n) % n;
  goIdle();
}

// 순차 전환 — 터치·시리얼 n/p·WebSocket toggle/next/prev.
static void nextPhoto() { showPhoto(photoIdx + 1); }
static void prevPhoto() { showPhoto(photoIdx - 1); }
static void homePhoto() { showPhoto(0); }            // 활성 화면으로

// 파일 이름을 가나다(바이트) 순으로 — 매번 같은 순서로 넘어가게.
static int cmpUid(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

// SD 의 /talent/*.565(온전한 480 사진) 를 훑어 대기 사진 목록을 다시 만든다.
static void scanPhotos() {
  sdPhotoCount = 0;
  bufIdx = -1;
  if (sdReady) {
    File dir = SD_MMC.open("/talent");
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f && sdPhotoCount < MAX_PHOTOS; f = dir.openNextFile()) {
        const char *nm = f.name();                       // 코어 3.x 는 경로 없이 이름만 준다
        const char *sl = strrchr(nm, '/'); if (sl) nm = sl + 1;
        const size_t len = strlen(nm);
        if (!f.isDirectory() && f.size() == 480 * 480 * 2 && len > 4 && len - 4 < sizeof(photoUids[0]) &&
            !strcasecmp(nm + len - 4, ".565")) {
          memcpy(photoUids[sdPhotoCount], nm, len - 4);
          photoUids[sdPhotoCount][len - 4] = '\0';
          sdPhotoCount++;
        }
        f.close();
      }
      dir.close();
    }
  }
  qsort(photoUids, sdPhotoCount, sizeof(photoUids[0]), cmpUid);
  if (photoIdx >= photoTotal()) photoIdx = 0;
  Serial.printf("[사진] 넘겨 볼 사진: 활성 화면 + SD %d 장\n", sdPhotoCount);
}

// 넘겨 볼 사진 목록을 시리얼에 찍는다('s').
static void listPhotos() {
  Serial.printf("[사진] 모두 %d 장 (지금 %d)\n", photoTotal(), photoIdx);
  for (int i = 0; i < photoTotal(); i++)
    Serial.printf("  %s%3d  %s\n", i == photoIdx ? ">" : " ", i, photoLabel(i));
}

// NFC 화면을 띄운다(몇 초 뒤 자동으로 대기 화면으로 돌아간다).
static void showNfc(const char *uid, const char *name, long points) {
  strncpy(nfcUid, uid, sizeof(nfcUid) - 1); nfcUid[sizeof(nfcUid) - 1] = '\0';
  const uint16_t *photo = loadPhotoForUid(uid);
  mode = MODE_NFC;
  nfcUntil = millis() + NFC_SHOW_MS;
  drawNfc(photo, name, points);
  sendState();
}

// ── microSD (SDMMC 1-bit) ───────────────────────────────────────────
// LILYGO T-RGB 는 SD 를 SDMMC 1-bit 로 붙인다. SD_CS(IO07)는 GPIO 가 아니라 XL9535 확장칩(bus)의
// 인에이블 핀이라, SD_MMC.begin 전에 확장칩으로 HIGH 로 켜야 카드가 잡힌다(공식 installSD 와 같은 순서).
static bool sdBegin() {
  bus->pinMode(SD_EN_EXPANDER, OUTPUT);
  bus->digitalWrite(SD_EN_EXPANDER, 1);        // SD 전원/인에이블 켜기(확장칩 7번)

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true /*1-bit*/, false /*포맷 안 함*/)) {
    Serial.println("[SD] 마운트 실패 — 카드/핀을 확인하세요");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[SD] 카드가 없습니다");
    return false;
  }
  SD_MMC.mkdir("/talent");
  Serial.printf("[SD] 준비됨 (%lluMB)\n", (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

// microSD 의 /talent 폴더에 들어 있는 사진 파일을 시리얼로 나열한다.
// 부팅 때 한 번 찍고, 시리얼로 'l' 을 보내면 다시 찍는다. 460800 바이트라야 온전한 480 사진이다.
static void listSD() {
  if (!sdReady) { Serial.println("[SD] 준비 안 됨 — 카드/핀 확인"); return; }
  File dir = SD_MMC.open("/talent");
  if (!dir || !dir.isDirectory()) { Serial.println("[SD] /talent 폴더가 없습니다"); return; }
  Serial.println("[SD] /talent 목록:");
  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char *nm = f.name();
    const size_t ln = strlen(nm);
    if (ln < 4 || strcasecmp(nm + ln - 4, ".565")) { f.close(); continue; }   // .ver(해시) 등은 건너뛴다
    const size_t sz = f.size();
    Serial.printf("   %-28s %8u bytes%s\n", f.name(), (unsigned)sz,
                  sz == 480 * 480 * 2 ? "  (사진 OK)" : "  (?)");
    n++; f.close();
  }
  dir.close();
  Serial.printf("[SD] 사진 %d 개\n", n);
}

// ── 서버에서 사진 받기 (길게 누르기) ────────────────────────────────
// HTTPClient 를 열어 x-talent-key 를 실어 GET 한다. https 면 인증서 검사는 건너뛴다(setInsecure).
static void beginHttp(HTTPClient &http, WiFiClientSecure &scli, WiFiClient &cli, const char *url) {
  if (strncmp(TALENT_SERVER, "https", 5) == 0) { scli.setInsecure(); http.begin(scli, url); }
  else                                         { http.begin(cli, url); }
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);
}

// 이미 받아 둔 사진이 최신인가 — /talent/<UID>.565 가 온전하고, /talent/<UID>.ver 에 적힌
// 해시(이름표의 img)가 지금 이름표의 img 와 같으면 true. 그러면 받지 않는다.
static bool photoUpToDate(const char *uid, const char *img) {
  char path[48], ver[48];
  snprintf(path, sizeof(path), "/talent/%s.565", uid);
  snprintf(ver,  sizeof(ver),  "/talent/%s.ver", uid);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  const bool whole = (size_t)f.size() == 480 * 480 * sizeof(uint16_t);
  f.close();
  if (!whole) return false;
  File v = SD_MMC.open(ver, FILE_READ);
  if (!v) return false;
  const String saved = v.readString();
  v.close();
  return saved == img;
}

// 받은 사진의 해시(이름표의 img)를 /talent/<UID>.ver 에 적는다.
static void saveVersion(const char *uid, const char *img) {
  char ver[48];
  snprintf(ver, sizeof(ver), "/talent/%s.ver", uid);
  File v = SD_MMC.open(ver, FILE_WRITE);
  if (v) { v.print(img); v.close(); }
}

// 사진 한 장(480×480 RGB565, 460800B)을 내려받아 /talent/<UID>.565 로 저장. 성공하면 true.
static bool downloadPhoto(const char *img, const char *uid) {
  char url[192];
  snprintf(url, sizeof(url), "%s/api/talent/photo/device/%s", TALENT_SERVER, img);

  HTTPClient http; WiFiClientSecure scli; WiFiClient cli;
  beginHttp(http, scli, cli, url);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); Serial.printf("[동기화] %s 사진 실패 code=%d\n", uid, code); return false; }

  const size_t need = 480 * 480 * sizeof(uint16_t);
  char path[48], tmp[48];
  snprintf(path, sizeof(path), "/talent/%s.565", uid);
  snprintf(tmp,  sizeof(tmp),  "/talent/%s.tmp", uid);

  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) { http.end(); Serial.printf("[동기화] %s 파일 못 엶\n", uid); return false; }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  uint32_t idleAt = millis();
  while (http.connected() && written < need) {
    const size_t avail = stream->available();
    if (avail) {
      const int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n > 0) { f.write(buf, n); written += n; idleAt = millis(); }
    } else {
      if (millis() - idleAt > 5000) break;      // 5초간 아무것도 안 오면 포기
      delay(2);
    }
  }
  f.close();
  http.end();

  if (written != need) { SD_MMC.remove(tmp); Serial.printf("[동기화] %s 크기 안 맞음 %u/%u\n", uid, (unsigned)written, (unsigned)need); return false; }
  SD_MMC.remove(path);                          // 옛 파일을 치우고 tmp 를 정식 이름으로
  SD_MMC.rename(tmp, path);
  saveVersion(uid, img);                        // 다음 동기화에서 같은 사진을 건너뛰게
  return true;
}

// 서버 이름표를 받아, 사진이 있는 UID 마다 480 사진을 내려받아 SD 로 동기화한다.
static void syncPhotos() {
  if (WiFi.status() != WL_CONNECTED) { drawStatus("와이파이가 없습니다"); delay(1500); goIdle(); return; }
  if (!sdReady)                      { drawStatus("SD 카드가 없습니다"); delay(1500); goIdle(); return; }

  drawStatus("이름표 받는 중...");
  char url[160];
  snprintf(url, sizeof(url), "%s/api/talent/roster?px=%d", TALENT_SERVER, TALENT_PX);

  HTTPClient http; WiFiClientSecure scli; WiFiClient cli;
  beginHttp(http, scli, cli, url);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    char m[40]; snprintf(m, sizeof(m), "이름표 실패 (%d)", code);
    drawStatus(m); delay(2000); goIdle(); return;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) { drawStatus("이름표 해석 실패"); delay(2000); goIdle(); return; }

  JsonArray data = doc["data"].as<JsonArray>();
  int total = 0;
  for (JsonObject r : data) if (r["img"].is<const char *>()) total++;

  int i = 0, ok = 0, same = 0;
  for (JsonObject r : data) {
    const char *uid = r["uid"] | "";
    const char *img = r["img"] | "";
    if (!uid[0] || !img[0]) continue;
    ++i;
    if (photoUpToDate(uid, img)) { same++; continue; }   // 해시가 같다 — 받지 않는다
    char m[48]; snprintf(m, sizeof(m), "사진 받는 중 %d/%d", i, total);
    drawStatus(m);
    if (downloadPhoto(img, uid)) { ok++; Serial.printf("[동기화] %s 받음 (%s)\n", uid, img); }
  }

  Serial.printf("[동기화] 새로 받음 %d · 그대로 %d · 실패 %d (모두 %d)\n", ok, same, total - ok - same, total);
  char m[48]; snprintf(m, sizeof(m), "완료 — 새 사진 %d 장", ok);
  Serial.printf("[동기화] %s\n", m);
  drawStatus(m); delay(2000);
  scanPhotos();                                 // 새로 받은 사진까지 넘겨 볼 목록에 넣는다
  goIdle();
}

// ── WebSocket 명령 풀기 ─────────────────────────────────────────────
// 글자 명령 하나 — enable/disable = 활성 화면, toggle/next = 다음 사진, prev = 이전 사진.
static void handleWord(const char *s) {
  if      (!strcmp(s, "enable") || !strcmp(s, "disable")) homePhoto();
  else if (!strcmp(s, "toggle") || !strcmp(s, "next"))    nextPhoto();
  else if (!strcmp(s, "prev"))                            prevPhoto();
}

// JSON 에 "uid" 가 있으면 NFC 알림, 아니면 대기 화면 전환 명령으로 본다.
static void handleCommand(const char *text, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, text, len) == DeserializationError::Ok) {
    if (doc["uid"].is<const char *>()) {               // NFC 태그
      const char *uid  = doc["uid"]  | "";
      const char *name = doc["name"] | "";
      long points      = doc["points"] | 0L;
      showNfc(uid, name, points);
      return;
    }
    if (doc["enabled"].is<bool>()) { homePhoto(); return; }   // 비활성은 없앴다 — 어느 쪽이든 활성 화면
    handleWord(doc["state"] | "");
    return;
  }
  // JSON 이 아니면 그냥 글자 명령(WebSockets 라이브러리가 payload 를 널로 끝맺는다)
  handleWord(text);
}

static void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] #%u 붙음 %s\n", num, ip.toString().c_str());
      sendState(num);                        // 붙자마자 지금 상태를 알려 준다
      break;
    }
    case WStype_DISCONNECTED:
      Serial.printf("[WS] #%u 떨어짐\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("[WS] #%u 받음: %.*s\n", num, (int)length, (const char *)payload);
      handleCommand((const char *)payload, length);
      break;
    default:
      break;                                 // BIN·PING 등은 쓰지 않는다
  }
}

// ── 와이파이 (우선순위 3개, config.h) ───────────────────────────────
// 0→1→2 순서로 붙어 본다. 한 곳에 WIFI_TRY_MS 만큼 붙어 보고 안 되면 다음 순위로,
// 셋 다 안 되면 처음(0순위)으로 돌아가 계속 반복한다(루프). WiFi.begin() 은 기다리지 않고
// 바로 돌아오므로 붙는 동안에도 loop 가 멈추지 않는다 — 터치로 그림이 바뀌어야 하기 때문이다.
struct WifiAp { const char *ssid; const char *pass; };
static const WifiAp WIFI_APS[] = {
  { WIFI_SSID_0, WIFI_PASS_0 },
  { WIFI_SSID_1, WIFI_PASS_1 },
  { WIFI_SSID_2, WIFI_PASS_2 },
};
static const int WIFI_AP_COUNT = sizeof(WIFI_APS) / sizeof(WIFI_APS[0]);

static const uint32_t WIFI_TRY_MS = 8000;   // 한 순위에 이만큼 붙어 보고 안 되면 다음 순위로
static int      wifiIdx   = 0;              // 지금 시도 중인 순위(0 부터)
static uint32_t wifiTryAt = 0;
static bool     wifiTried = false;

static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (wifiTried && millis() - wifiTryAt < WIFI_TRY_MS) return;   // 지금 순위에 붙는 중 — 더 기다린다

  if (wifiTried) wifiIdx = (wifiIdx + 1) % WIFI_AP_COUNT;        // 실패 → 다음 순위(끝이면 처음으로)
  WiFi.disconnect();
  WiFi.begin(WIFI_APS[wifiIdx].ssid, WIFI_APS[wifiIdx].pass);
  wifiTryAt = millis();
  wifiTried = true;
  Serial.printf("[와이파이] %d순위 '%s' 에 붙어 보는 중\n", wifiIdx + 1, WIFI_APS[wifiIdx].ssid);
}

// 와이파이가 붙으면 mDNS 와 WebSocket 서버를 켠다(한 번만).
static void startServers() {
  if (serversUp) return;
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("ws", "tcp", WS_PORT);
    Serial.printf("[mDNS] %s.local 준비됨\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[mDNS] 시작 실패");
  }
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.printf("[WS] 서버 열림 — ws://%s.local:%d/  (또는 ws://%s:%d/)\n",
                MDNS_HOSTNAME, WS_PORT, WiFi.localIP().toString().c_str(), WS_PORT);
  serversUp = true;
}

// ── 터치 (v1.0 과 동일) ─────────────────────────────────────────────
static void touchBegin() {
  bus->pinMode(1, OUTPUT);       // 확장칩 1번 = 터치 리셋(TP_RES)
  bus->digitalWrite(1, 0);
  delay(30);
  bus->digitalWrite(1, 1);
  delay(200);

  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0xFE);              // 자동 잠들기 끄기
  Wire.write(0x01);
  Serial.printf("터치 칩 %s\n", Wire.endTransmission() == 0 ? "준비됨" : "응답 없음");
}

// 손가락이 닿아 있으면 true. 좌표는 쓰지 않는다(화면 어디를 눌러도 된다).
static bool touching() {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);              // 손가락 수부터 읽는다
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDR, 5) != 5) return false;

  const uint8_t fingers = Wire.read();
  for (int i = 0; i < 4; i++) Wire.read();   // X·Y 네 바이트는 버린다
  return fingers == 1;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== nfcProject — WebSocket 디스플레이 ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  // 1~3) 확장칩 → RGB 패널 → 초기화 표. begin() 한 번이 다 한다.
  if (!gfx->begin()) {
    Serial.println("화면 시작 실패 — PSRAM(OPI) 설정을 확인하세요");
    return;
  }

  frame = (uint16_t *)ps_malloc(480 * 480 * sizeof(uint16_t));
  Serial.printf("합쳐 그리기 버퍼 %s\n", frame ? "준비됨(PSRAM 460KB)" : "못 잡음 — 아이콘/띠가 깜빡일 수 있다");

  photoBuf = (uint16_t *)ps_malloc(480 * 480 * sizeof(uint16_t));   // SD 사진을 읽어 둘 자리
  Serial.printf("사진 버퍼 %s\n", photoBuf ? "준비됨(PSRAM 460KB)" : "못 잡음 — SD 사진을 못 쓸 수 있다");

  sdReady = sdBegin();           // microSD (config.h 의 SD_* 핀). 없으면 내장 사진만 쓴다
  listSD();                      // SD 에 들어 있는 사진 목록을 시리얼에 찍는다(확인용)
  scanPhotos();                  // 터치로 넘겨 볼 사진 목록

  drawIdle();                    // 기본 화면 — 활성 그림, 아이콘 없음(아직 안 붙었다)

  // 4) 백라이트를 마지막에 켠다 — 먼저 켜면 그리는 동안 빈 화면이 번쩍인다.
  pinMode(PIN_BL, OUTPUT);
  backlightOn(BL_LEVEL);
  Serial.printf("밝기 %d/16 단계\n", BL_LEVEL);

  touchBegin();                  // 화면을 켠 뒤에 — 확장칩(bus)이 begin() 에서 준비된다

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // 서버라 잠들지 않게 — 명령을 놓치지 않는다
  wifiService();                 // 붙어 보기 시작(기다리지 않는다)
}

void loop() {
  // 와이파이 — 붙거나 끊기는 순간에만 다시 그린다(매번 그리면 화면이 계속 쓸린다)
  wifiService();
  const bool nowOnline = (WiFi.status() == WL_CONNECTED);
  if (nowOnline != online) {
    online = nowOnline;
    if (online) {
      Serial.printf("[와이파이] 연결됨 %s (%s)\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      startServers();            // 붙은 뒤에 mDNS·WebSocket 을 켠다
      if (!bootSyncDone) {       // 부팅 후 처음 붙었을 때 서버에서 사진을 모두 받아 SD 에 저장
        bootSyncDone = true;
        syncPhotos();
      }
    } else {
      Serial.println("[와이파이] 끊김");
    }
    if (mode == MODE_IDLE) drawIdle();   // 대기 화면일 때만 아이콘을 갱신(NFC 화면은 건드리지 않는다)
    sendState();
  }

  if (serversUp) webSocket.loop();   // WebSocket 을 굴린다(받은 것을 처리)

  // client 연결 상태 점 — client 가 붙거나 떨어져 색이 바뀔 때만 다시 그린다(화면 갱신 없이도)
  if (linkState() != shownLink) drawLinkDot();

  // 시리얼 명령 — 한 글자씩(줄바꿈은 무시). 화면 넘기기는 터치와 똑같이 동작한다.
  while (Serial.available()) {
    const int c = Serial.read();
    switch (c) {
      case 'n': case 'N': Serial.println("[명령] 다음 사진"); if (mode == MODE_NFC) goIdle(); else nextPhoto(); break;
      case 'p': case 'P': Serial.println("[명령] 이전 사진"); prevPhoto(); break;
      case 'h': case 'H': Serial.println("[명령] 활성 화면"); homePhoto(); break;
      case 's': case 'S': listPhotos(); break;
      case 'l': case 'L': listSD(); break;
      case 'r': case 'R': Serial.println("[명령] 재부팅합니다..."); Serial.flush(); ESP.restart(); break;
      case '?':           Serial.println("[명령] n 다음 · p 이전 · h 활성 · s 사진 목록 · l SD 목록 · r 재부팅"); break;
      default: break;
    }
  }

  // NFC 화면은 몇 초 뒤 자동으로 대기 화면으로 돌아간다
  if (mode == MODE_NFC && (int32_t)(millis() - nfcUntil) >= 0) goIdle();

  // 터치 — 짧게: (NFC 화면이면 대기로 / 대기 화면이면 다음 사진),  길게: 서버에서 사진 동기화
  const bool down = touching();
  if (down && !wasDown) { downAt = millis(); longFired = false; }     // 누르기 시작
  if (down && !longFired && (millis() - downAt) >= LONG_PRESS_MS) {
    longFired = true;                                                 // 누르는 동안 한 번만
    syncPhotos();
  }
  if (!down && wasDown && !longFired) {                               // 짧게 눌렀다 뗌
    Serial.println("[터치] 짧게 누름");
    if (mode == MODE_NFC) goIdle();
    else                  nextPhoto();
  }
  wasDown = down;

  delay(5);                      // WebSocket 반응을 위해 v1.0(20ms)보다 촘촘히 돈다
}
