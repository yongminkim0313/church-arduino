// TalentNfcReader_CST820_v1.0 — 실습 4단계: 터치 수로 나누고, 학생 사진을 받아 본다
//
// 보드: LILYGO T-RGB 2.1" 원형 (ESP32-S3 · ST7701S RGB 480×480 · CST820 터치)
// 켜면 useDisable(사용 비활성) 그림이 뜨고, 화면을 누르면
// useEnable(사용 활성) ↔ useDisable 로 번갈아 바뀐다.
//
// ── 터치 횟수 ───────────────────────────────────────────────────────
//   한 번   그림 바꾸기 (0.45초 뒤에 — 두 번·세 번이 올지 봐야 한다)
//   두 번   학생 사진 받기 — 아직 없는 것만
//   세 번   받아 둔 사진을 모두 지우고 처음부터 다시 받기
//
// 사진을 화면에 띄우지는 않는다. **받는 동안 화면이 자글거리는지** 보려고 옮겨 온 것이다 —
// 본체에서 그 현상을 쫓던 자리라, 군더더기 없는 이 스케치에서 다시 보면 원인을 가리기 쉽다.
//
// ── 화면이 켜지는 순서 ──────────────────────────────────────────────
//   1) XL9535 확장칩   화면 명령 줄(CS·SCK·MOSI)과 전원 인에이블이 여기 붙어 있다
//   2) RGB 패널        ESP32 가 16개 데이터선으로 픽셀을 계속 흘려보낸다(PSRAM 프레임버퍼)
//   3) 초기화 표       ST7701S 에 "이런 화면이다" 를 알려 준다(2.1인치는 type4)
//   4) 백라이트        GPIO46 을 켜야 보인다. 밝기는 PWM 이 아니라 '흔든 횟수' 로 정한다(BL_LEVEL)
//
// ── 터치가 살아나는 순서 ────────────────────────────────────────────
//   1) 터치 칩 리셋 풀기   CST820 의 리셋은 ESP32 핀이 아니라 확장칩 1번에 붙어 있다.
//                          켜면 리셋에 잡힌 채라 I2C 에 아예 안 나온다 — 내렸다 올려서 깨운다
//   2) 자동 잠들기 끄기    0xFE 에 1 을 쓴다. 안 그러면 가만히 둘 때 응답을 안 해 첫 터치를 놓친다
//   3) 좌표 읽기           0x02 부터 5바이트: 손가락 수 · X(12비트) · Y(12비트)
//
// ── 빌드 (Arduino IDE 도구 메뉴) ────────────────────────────────────
//   보드: ESP32S3 Dev Module   PSRAM: OPI PSRAM   Flash Size: 16MB
//   Partition Scheme: Custom           ← 이 폴더의 partitions.csv (앱 4MB · LittleFS 11.875MB)
//                                         Huge APP 은 SPIFFS 가 0.875MB 라 사진(한 장 86KB,
//                                         오십 명이면 4.3MB)이 들어가지 않는다
//   USB CDC On Boot: Enabled           ← 꺼져 있으면 다음부터 포트가 안 보인다
//   라이브러리: GFX Library for Arduino · ArduinoJson
//
//   ※ LilyGo-T-RGB 라이브러리(`LV_Helper.h`·`LilyGo_RGBPanel.h`)는 **넣지 말 것.** 한 줄만
//     include 해도 lvgl 과 SensorLib 이 딸려 들어오는데, LilyGo-T-RGB 1.0.5 가 SensorLib
//     0.4.1 과 맞지 않아 빌드가 깨진다(`transfer9` 없음 · setGpioCallback 인자 형 불일치).
//     이 실습은 Arduino_GFX 로 직접 그리므로 그 라이브러리가 필요 없다.
//
// ── 그림 바꾸기 ─────────────────────────────────────────────────────
// 그림 헤더는 tools/img2rgb565_dither.py 로 만든다. **--var 이름을 그림마다 다르게** 줘야 한다 —
// 같은 이름(예: 둘 다 BACKGROUND)이면 두 헤더를 함께 넣을 때 이름이 겹쳐 빌드가 안 된다.
//   cd /Users/kimyongmin/workspace/arduino
//   python3 tools/img2rgb565_dither.py TalentNfcReader_CST820/TalentNfcReader_CST820_v1.0/사용비활성.jpeg \
//       --width 480 --height 480 --fit cover --var USEDISABLE \
//       -o TalentNfcReader_CST820/TalentNfcReader_CST820_v1.0/useDisable.h
//   python3 tools/img2rgb565_dither.py TalentNfcReader_CST820/TalentNfcReader_CST820_v1.0/사용활성.jpeg \
//       --width 480 --height 480 --fit cover --var USEENABLE \
//       -o TalentNfcReader_CST820/TalentNfcReader_CST820_v1.0/useEnable.h
//
// 아이콘(투명 PNG)은 **--transparent** 를 꼭 준다. 빼면 투명한 곳이 흰색으로 채워지고,
// 스케치가 쓰는 CONNECTED_TRANSPARENT 이름도 생기지 않아 빌드가 안 된다.
// --bg 는 아이콘이 놓일 배경색 — 반투명 가장자리를 이 색과 섞는다.
//   python3 tools/img2rgb565_dither.py TalentNfcReader_CST820/TalentNfcReader_CST820_v1.0/연결.png \
//       --width 50 --height 35 --fit contain --transparent --bg "#FFAE31" --var CONNECTED \
//       -o TalentNfcReader_CST820/TalentNfcReader_CST820_v1.0/connected.h
//
// ※ 이 폴더는 본체 폴더(TalentNfcReader_CST820) 안에 있지만 따로 빌드한다.
//   아두이노는 폴더 안의 .ino 를 모두 합쳐 빌드하므로, 본체 폴더 바로 아래에 두면
//   setup()·loop() 가 두 번 생겨 본체가 빌드되지 않는다. 그래서 하위 폴더로 뺐다.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <ChurchSecrets.h>       // 와이파이·서버 주소·기기 키(~/workspace/arduino/libraries/ChurchSecrets — 커밋 안 됨)
#include "useDisable.h"          // USEDISABLE[480×480] — 사용 비활성(기본 화면)
#include "useEnable.h"           // USEENABLE[480×480]  — 사용 활성(터치하면)
#include "connected.h"           // CONNECTED[50×35]    — 연결 아이콘(와이파이에 붙었을 때만 겹친다)

// ── 연결 아이콘 위치 ← 여기 두 줄을 바꿔 위치를 맞춰 보세요 ─────────────
// (CONN_X, CONN_Y) 가 아이콘의 **왼쪽 위 모서리**다. 화면은 480×480, 왼쪽 위가 (0, 0).
// 둥근 화면이라 위쪽 가장자리는 가운데만 보인다 — y 가 작을수록 보이는 폭이 좁아진다.
//   가로 가운데로 두려면:  (480 - CONNECTED_W) / 2 = 215
static const int CONN_X = 307;
static const int CONN_Y = 30;

// ── 와이파이 ──────────────────────────────────────────────────────
// ChurchSecrets.h 에 있는 것을 차례로 시도한다. 붙을 때까지 기다리며 멈추지 않는다 —
// 기다리는 동안에도 터치로 그림이 바뀌어야 하기 때문이다(loop 에서 조금씩 확인한다).
struct WifiCandidate { const char *ssid; const char *pass; };
static const WifiCandidate WIFI_LIST[] = {
#ifdef WIFI_SSID_0
  { WIFI_SSID_0, WIFI_PASSWORD_0 },
#endif
#ifdef WIFI_SSID
  { WIFI_SSID, WIFI_PASSWORD },
#endif
#ifdef WIFI_SSID_2
  { WIFI_SSID_2, WIFI_PASSWORD_2 },
#endif
};
static const int      WIFI_COUNT  = sizeof(WIFI_LIST) / sizeof(WIFI_LIST[0]);
static const uint32_t WIFI_TRY_MS = 10000;   // 한 곳에 붙어 보는 시간 — 안 되면 다음 것

// ── 핀 ────────────────────────────────────────────────────────────
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL  48
#define PIN_BL       46          // 백라이트

// ── 밝기 ──────────────────────────────────────────────────────────
// PWM 이 아니다. 핀을 짧게 흔든 횟수로 16단계 중 하나를 고른다(LILYGO 보드 방식).
// 켜면(HIGH) 가장 밝은 16단계에서 시작하고, 한 번 흔들 때마다 한 단계씩 어두워진다.
// 16 → 15 → … → 1 로 돌고, 1 에서 한 번 더 흔들면 다시 16 으로 돌아간다.
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
#define TOUCH_ADDR   0x15        // CST820 의 I2C 주소

// ── 화면 객체 ─────────────────────────────────────────────────────
Arduino_XL9535SWSPI *bus = new Arduino_XL9535SWSPI(
    PIN_I2C_SDA, PIN_I2C_SCL,
    2 /* 전원 인에이블 */, 3 /* CS */, 5 /* SCK */, 4 /* MOSI */);

// 뒤의 두 값(픽셀 클럭·중계 버퍼)은 화면이 자글거리는 것을 막는다. 본체(PanelTFT.cpp)와 같은 값이다.
// 이 화면에는 화면쪽 메모리가 없다 — LCD 가 PSRAM 의 프레임버퍼를 **실시간으로** 읽어 뿌리므로,
// PSRAM 이 한순간 늦으면 그 줄이 깨져 나간다(와이파이가 붙을 때, 큰 그림을 옮길 때 그렇다).
//  · 8MHz — 라이브러리 기본값은 옥탈 PSRAM 일 때 12MHz 다. 포치를 더한 561×531 을 그 속도로
//    뿌리면 PSRAM 에서 초당 24MB 를 끌어간다. LILYGO 자기 드라이버도 8MHz 다(16MB/s).
//  · 중계 버퍼 — LCD 가 PSRAM 을 직접 읽지 않고, 내부 SRAM 의 열 줄짜리 버퍼 두 장을 번갈아
//    읽는다. CPU 가 미리 채워 두므로 PSRAM 이 잠깐 늦어도 열 줄만큼 버틴다.
static const int32_t PCLK_HZ      = 8000000L;
static const int     BOUNCE_LINES = 10;      // 480 줄을 48번에 나눠 담는다

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

// ── 상태 ──────────────────────────────────────────────────────────
static bool enabled   = false;   // 지금 '사용 활성' 그림인가 — 켜면 비활성부터
static bool wasDown   = false;   // 직전에 손가락이 닿아 있었나(누르는 순간을 가려내려고)

// 터치를 몇 번 눌렀는지 세는 데 쓴다 — loop 아래 '터치 수 세기' 참고
static const uint32_t TAP_GAP_MS = 450;
static uint8_t  taps  = 0;
static uint32_t tapMs = 0;
static bool online    = false;   // 화면에 그려 둔 연결 상태(아이콘이 떠 있나)

static int      wifiIdx   = -1;  // 지금 시도 중인 WIFI_LIST 번호
static uint32_t wifiTryAt = 0;   // 그 시도를 시작한 시각

// ── 합쳐 그리기용 버퍼 ──────────────────────────────────────────────
// 화면에 배경을 먼저 그리고 아이콘을 나중에 얹으면, 그 사이 틈에 '아이콘 없는 배경' 이
// 화면에 한 번 나와 아이콘이 깜빡인다. RGB 화면은 프레임버퍼를 초당 수십 번 계속 읽어 가기 때문이다.
// 그래서 PSRAM 에 한 장(480×480, 460KB)을 따로 두고, 거기서 배경+아이콘을 **먼저 합친 뒤**
// 화면으로 한 번에 옮긴다. 아이콘 자리는 옮기기 전후 모두 아이콘이라 깜빡이지 않는다.
static uint16_t *frame = nullptr;

// 지금 상태(사용 활성/비활성 · 연결 여부)에 맞는 한 장을 만들어 화면에 옮긴다.
static void drawCurrent() {
  const uint16_t *bg = enabled ? USEENABLE : USEDISABLE;

  if (!frame) {                  // 버퍼를 못 잡았으면 배경만이라도 바로 그린다
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)bg, 480, 480);
    return;
  }

  // 1) 배경을 버퍼로 복사
  memcpy(frame, bg, 480 * 480 * sizeof(uint16_t));

  // 2) 와이파이에 붙어 있으면 아이콘을 얹는다 — 투명색(자홍)인 점은 건너뛴다
  if (online) {
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

  // 3) 완성된 한 장을 화면으로 한 번에
  gfx->draw16bitRGBBitmap(0, 0, frame, 480, 480);

  Serial.printf("그림: %s · 연결 아이콘 %s\n",
                enabled ? "사용 활성(useEnable)" : "사용 비활성(useDisable)",
                online ? "보임" : "숨김");
}

// 와이파이 — 안 붙어 있으면 WIFI_TRY_MS 마다 다음 후보로 넘어가며 붙어 본다.
// WiFi.begin() 은 기다리지 않고 바로 돌아오므로 loop 가 멈추지 않는다.
static void wifiService() {
  if (WIFI_COUNT == 0) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (wifiIdx >= 0 && millis() - wifiTryAt < WIFI_TRY_MS) return;   // 아직 기다리는 중

  wifiIdx = (wifiIdx + 1) % WIFI_COUNT;
  WiFi.disconnect();
  WiFi.begin(WIFI_LIST[wifiIdx].ssid, WIFI_LIST[wifiIdx].pass);
  wifiTryAt = millis();
  Serial.printf("[와이파이] %s 에 붙어 보는 중\n", WIFI_LIST[wifiIdx].ssid);
}

static bool touching();          // 아래에 있다 — 받는 동안 '멈춤' 을 보려고 여기서도 쓴다

// ══════════════════════════════════════════════════════════════════
//  학생 사진 받기 (본체 TalentNfcReader_CST820 에서 옮겨 온 것)
// ══════════════════════════════════════════════════════════════════
// 서버 주소와 기기 키는 ChurchSecrets.h 에 있다(TALENT_API_BASE · TALENT_DEVICE_KEY).
// 받는 차례는 이렇다.
//   1) GET /roster?px=208   아이 이름과 **사진 파일 이름** 을 받는다
//   2) GET /photo/device/<파일이름>   사진마다 한 번씩. LittleFS 의 /art 에 쌓는다
//
// 파일 이름("ph-<24자>-208.<랜덤>")에 해시가 들어 있어 **있으면 최신**이다. 그래서 두 번
// 터치는 없는 것만 받고, 세 번 터치는 그 믿음을 버리고 다 지운 뒤 처음부터 받는다.
//
// 사진 한 장은 208×208×2 = 86KB 다. 통째로 메모리에 올리지 않고 흘려 쓴다 —
// HTTPS 핸드셰이크가 쓸 힙을 남겨 두어야 한다.

#define PHOTO_PX   208           // 서버(talentPhoto.DEVICE_SIZES)에 있는 크기라야 한다 — 파일 이름에도 들어간다
#define ART_DIR    "/art"
#define ROSTER_MAX 200

struct Student { char name[24]; char img[36]; };   // img — 사진이 없으면 빈 값
static Student  roster[ROSTER_MAX];
static uint16_t rosterCount = 0;

static WiFiClientSecure apiTls;
static HTTPClient       apiHttp;

// 한 번 묻고 답을 받는다. 본문은 부르는 쪽이 읽고 apiEnd() 로 닫는다.
static int apiGet(const char *url) {
  apiTls.setInsecure();                       // 자체 서버라 인증서 검증 생략(본체와 같다)
  apiHttp.setReuse(true);
  apiHttp.setTimeout(8000);
  if (!apiHttp.begin(apiTls, url)) return -1;
  apiHttp.addHeader("x-talent-key", TALENT_DEVICE_KEY);
  return apiHttp.GET();
}

// hard — 본문을 다 읽지 못했으면 true. 남은 조각이 다음 요청에 섞이지 않게 연결을 끊는다.
static void apiEnd(bool hard) {
  apiHttp.end();
  if (hard) apiTls.stop();
}

// ── 진행 띠 ──
// 화면 가운데 아래에 몇 장째인지 적는다. 이 실습에는 한글 폰트가 없어서(본체는 VlwFont 를
// 쓴다) 내장 폰트로 영문·숫자만 적는다. 자세한 것은 시리얼로 나간다.
//
// ※ 사진을 받는 동안 화면 전체는 **검정 한 색**이다. 까닭은 photoDownload 의 주석에 있다.
static const int PROG_X = 60, PROG_Y = 320, PROG_W = 360, PROG_H = 72;

// 진행 띠를 지운다 — 플래시에 쓰기 직전에 부른다. 그러면 화면이 온통 같은 색이 된다.
static void clearProgress() {
  gfx->fillRect(PROG_X, PROG_Y, PROG_W, PROG_H, 0x0000);
}

static void drawProgress(const char *tag, uint16_t done, uint16_t total) {
  clearProgress();
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(2);
  gfx->setCursor(100, PROG_Y + 12);
  gfx->printf("%s %u/%u", tag, (unsigned)done, (unsigned)total);

  const int bx = 100, bw = 280, by = PROG_Y + 44, bh = 14;
  gfx->drawRect(bx, by, bw, bh, 0xFFFF);
  if (total) gfx->fillRect(bx + 1, by + 1, (int)((bw - 2) * (uint32_t)done / total), bh - 2, 0x07E0);
}

// ── 이름표 받기 ──
static bool rosterFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[160];
  snprintf(url, sizeof(url), "%s/roster?px=%d", TALENT_API_BASE, PHOTO_PX);
  const int code = apiGet(url);
  if (code != 200) { Serial.printf("[이름표] 서버 응답 %d\n", code); apiEnd(true); return false; }

  // 응답을 String 으로 통째로 받지 않고 스트림에서 바로 파싱한다 — 200명이면 8KB 가 넘는다.
  JsonDocument filter;
  filter["success"]         = true;
  filter["data"][0]["name"] = true;
  filter["data"][0]["img"]  = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, apiHttp.getStream(),
                                             DeserializationOption::Filter(filter));
  apiEnd((bool)err);
  if (err) { Serial.printf("[이름표] 해석 실패 %s\n", err.c_str()); return false; }

  rosterCount = 0;
  uint16_t withPhoto = 0;
  for (JsonObject r : doc["data"].as<JsonArray>()) {
    if (rosterCount >= ROSTER_MAX) break;
    const char *n = r["name"] | "";
    const char *f = r["img"]  | "";
    if (!n[0]) continue;
    strlcpy(roster[rosterCount].name, n, sizeof(roster[0].name));
    // 이름이 길어 잘리면 없는 파일을 찾게 된다 — 칸에 다 안 들어가면 사진 없음으로 둔다
    if (f[0] && strlen(f) < sizeof(roster[0].img)) {
      strlcpy(roster[rosterCount].img, f, sizeof(roster[0].img));
      withPhoto++;
    } else {
      roster[rosterCount].img[0] = '\0';
    }
    rosterCount++;
  }
  Serial.printf("[이름표] %u명 받음 · 사진 %u명\n", rosterCount, withPhoto);
  return true;
}

// ── 사진 한 장 받기 ──
// 본체는 받는 즉시 파일로 흘려 쓴다(힙을 아끼려고). 여기서는 **둘로 나눈다** —
//   1) 네트워크: PSRAM 버퍼(86KB)로 다 받는다. 이 동안 화면은 멀쩡하다
//   2) 플래시  : 버퍼를 한 번에 쓴다. **이 동안에만** 캐시가 꺼진다
//
// 왜 나누나. LittleFS 는 4KB 블록을 먼저 지워야 하고(한 번에 30ms 남짓), 그동안 ESP32-S3 는
// 캐시를 통째로 끈다 — 플래시와 PSRAM 이 같은 캐시라 LCD 도 PSRAM 을 못 읽는다. 중계 버퍼가
// 비면 화면이 밀려 흐르고, 드라이버가 VSYNC 에 맞춰 되돌린다(실기에서 본 그 모양이다).
// 막을 길은 없다 — 아두이노 코어가 SPI_FLASH_AUTO_SUSPEND 와 LCD_RGB_ISR_IRAM_SAFE 를
// 끈 채로 나온다. 대신 **안 보이게** 할 수는 있다: 온통 같은 색인 화면은 밀려도 같은 색이다.
// 그래서 쓰기 직전에 진행 띠를 지워 화면을 검정 한 색으로 만든다(clearProgress).
//
// 나눈 덕에 두 토막이 각각 몇 ms 인지도 잰다 — 무엇이 오래 걸리는지 시리얼로 보인다.
static uint8_t *photoBuf = nullptr;      // PSRAM 86KB

static bool photoDownload(const char *file, uint32_t *msNet, uint32_t *msFlash) {
  *msNet = *msFlash = 0;
  const int want = PHOTO_PX * PHOTO_PX * 2;
  if (!photoBuf) return false;

  char url[192];
  snprintf(url, sizeof(url), "%s/photo/device/%s", TALENT_API_BASE, file);

  const int code = apiGet(url);
  if (code != 200) { Serial.printf("[사진] %s 응답 %d\n", file, code); apiEnd(true); return false; }

  // 크기가 맞지 않으면 받지 않는다 — 버퍼에 담을 때 길이를 믿기 때문이다
  if (apiHttp.getSize() != want) {
    Serial.printf("[사진] %s 크기 불일치 %d != %d\n", file, apiHttp.getSize(), want);
    apiEnd(true);
    return false;
  }

  // 1) 네트워크 — 화면은 그대로 둔다(진행 띠가 보인다)
  uint32_t t0 = millis();
  Stream &st = apiHttp.getStream();
  int got = 0;
  const uint32_t deadline = millis() + 15000;
  while (got < want && millis() < deadline) {
    const int avail = st.available();
    if (avail <= 0) {
      if (!apiHttp.connected()) break;
      delay(1);
      continue;
    }
    const int n = st.readBytes(photoBuf + got, (avail < want - got) ? avail : (want - got));
    if (n <= 0) break;
    got += n;
  }
  apiEnd(got != want);                   // 덜 받았으면 남은 조각이 있다 — 끊는다
  *msNet = millis() - t0;
  if (got != want) { Serial.printf("[사진] %s 덜 받음 %d/%d\n", file, got, want); return false; }

  // 2) 플래시 — 여기서만 캐시가 꺼진다. 화면을 한 색으로 비워 두고 쓴다.
  clearProgress();
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", ART_DIR, file);
  t0 = millis();
  fs::File f = LittleFS.open(path, "w");
  if (!f) return false;
  const size_t wrote = f.write(photoBuf, want);
  f.close();
  *msFlash = millis() - t0;

  if (wrote != (size_t)want) {
    LittleFS.remove(path);               // 반쪽짜리를 남기면 다음에 "있다" 로 오해한다
    Serial.printf("[사진] %s 저장 실패 %u/%d\n", file, (unsigned)wrote, want);
    return false;
  }
  return true;
}

// ── 받아 둔 사진을 모두 지운다 (세 번 터치의 앞단) ──
static uint16_t photoWipe() {
  fs::File dir = LittleFS.open(ART_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  uint16_t n = 0;
  for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char *name = f.name();
    if (strncmp(name, "ph-", 3)) continue;     // 아이 사진만
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, name);
    f.close();
    if (LittleFS.remove(path)) n++;
  }
  return n;
}

// ── 사진 받기 (두 번 터치) · 지우고 처음부터 받기 (세 번 터치) ──
// 지우는 것은 **명단을 받은 뒤**다. 끊겼거나 명단을 못 받으면 한 장도 다시 받지 못하므로,
// 그때는 있던 사진을 그대로 두고 물러난다.
// 받는 동안 화면을 누르면 멈춘다.
static void photoSyncAll(bool wipe) {
  Serial.printf("\n=== 사진 %s ===\n", wipe ? "지우고 처음부터 받기(세 번 터치)" : "받기(두 번 터치)");

  // 받는 동안 화면을 검정 한 색으로 둔다. 플래시를 쓰는 사이 화면이 밀리는데(photoDownload
  // 주석 참고), 온통 같은 색이면 밀려도 같은 색이라 보이지 않는다. 그림을 띄워 두면 그림이
  // 통째로 흘러 '망가진' 것처럼 보인다 — 실기에서 본 그 모양이다.
  gfx->fillScreen(0x0000);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[사진] 와이파이에 연결되지 않았습니다");
    drawProgress("NO WIFI", 0, 0);
    delay(1500);
    drawCurrent();
    return;
  }

  drawProgress("ROSTER", 0, 0);
  if (!rosterFetch()) {
    drawProgress("ROSTER FAIL", 0, 0);
    delay(1500);
    drawCurrent();
    return;
  }

  uint16_t wiped = 0;
  if (wipe) {
    drawProgress("WIPE", 0, 0);
    wiped = photoWipe();
    Serial.printf("[사진] 지움 %u장 — 처음부터 다시 받습니다\n", wiped);
  }

  uint16_t total = 0;
  for (uint16_t i = 0; i < rosterCount; i++) if (roster[i].img[0]) total++;

  // 마지막 누름의 손가락이 아직 닿아 있으면 곧바로 '멈춤' 으로 읽힌다 — 뗄 때까지(최대 1초) 기다린다
  for (uint32_t t0 = millis(); touching() && millis() - t0 < 1000;) delay(20);

  const size_t want = (size_t)PHOTO_PX * PHOTO_PX * 2;
  uint16_t done = 0, got = 0, have = 0, failed = 0;
  uint32_t netMs = 0, flashMs = 0;         // 무엇이 오래 걸리는지 — 끝에 합쳐 보인다
  bool stopped = false;
  uint32_t lastDraw = 0;
  const uint32_t t0 = millis();

  for (uint16_t i = 0; i < rosterCount; i++) {
    if (!roster[i].img[0]) continue;
    done++;

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, roster[i].img);
    size_t size = 0;
    if (LittleFS.exists(path)) {
      fs::File f = LittleFS.open(path, "r");
      if (f) { size = f.size(); f.close(); }
    }

    if (size == want) {
      have++;
      // 건너뛰는 것도 가끔 알려 준다 — 다 있으면 막대가 멈춰 보여 "굳었다" 로 읽힌다
      if (millis() - lastDraw > 300) { drawProgress("HAVE", done, total); lastDraw = millis(); }
    } else {
      if (size) LittleFS.remove(path);         // 크기가 틀린 반쪽 파일 — 지우고 새로 받는다
      drawProgress("PHOTO", done, total);
      lastDraw = millis();
      uint32_t msNet = 0, msFlash = 0;
      if (photoDownload(roster[i].img, &msNet, &msFlash)) got++;
      else failed++;
      netMs += msNet; flashMs += msFlash;
      Serial.printf("[사진] %u/%u %s — 받기 %lums · 쓰기 %lums\n", done, total, roster[i].name,
                    (unsigned long)msNet, (unsigned long)msFlash);
    }

    if (touching()) { stopped = true; break; }
  }

  drawProgress(stopped ? "STOPPED" : "DONE", done, total);
  Serial.printf("[사진] 끝 — 지움 %u · 새로 %u · 있음 %u · 실패 %u · %lu초%s\n",
                wiped, got, have, failed, (unsigned long)((millis() - t0) / 1000),
                stopped ? " (멈춤)" : "");
  Serial.printf("[사진] 그중 받기 %lu초 · 플래시 쓰기 %lu초 (쓰는 동안만 화면이 밀린다)\n",
                (unsigned long)(netMs / 1000), (unsigned long)(flashMs / 1000));
  delay(2000);
  drawCurrent();                               // 진행 띠를 걷고 원래 그림으로
}

// 터치 칩을 깨운다 — 위 머리말의 '터치가 살아나는 순서' 1·2.
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

// 손가락이 닿아 있으면 true. 좌표는 이번 실습에서는 쓰지 않는다(화면 어디를 눌러도 된다).
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
  Serial.println("\n=== CST820 실습 v1.0 — 터치로 그림 바꾸기 ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  // 1~3) 확장칩 → RGB 패널 → 초기화 표. begin() 한 번이 다 한다.
  if (!gfx->begin()) {
    Serial.println("화면 시작 실패 — PSRAM(OPI) 설정을 확인하세요");
    return;
  }

  // 사진을 담아 둘 곳. 처음이면 포맷한다(true) — 파티션을 막 바꿨을 때도 여기서 잡힌다.
  if (!LittleFS.begin(true)) Serial.println("LittleFS 를 열지 못했습니다 — 사진을 받아도 저장되지 않습니다");
  else                       LittleFS.mkdir(ART_DIR);

  // 사진 한 장을 받아 둘 곳(86KB) — 다 받은 뒤 한 번에 플래시로 쓴다(photoDownload 주석)
  photoBuf = (uint8_t *)ps_malloc(PHOTO_PX * PHOTO_PX * 2);
  if (!photoBuf) Serial.println("사진 버퍼를 못 잡았습니다 — 사진 받기가 되지 않습니다");

  frame = (uint16_t *)ps_malloc(480 * 480 * sizeof(uint16_t));
  Serial.printf("합쳐 그리기 버퍼 %s\n", frame ? "준비됨(PSRAM 460KB)" : "못 잡음 — 아이콘이 깜빡일 수 있다");

  drawCurrent();                 // 기본 화면 — 사용 비활성, 아이콘 없음(아직 안 붙었다)

  // 4) 백라이트를 마지막에 켠다 — 먼저 켜면 그리는 동안 빈 화면이 번쩍인다.
  pinMode(PIN_BL, OUTPUT);
  backlightOn(BL_LEVEL);
  Serial.printf("밝기 %d/16 단계\n", BL_LEVEL);

  touchBegin();                  // 화면을 켠 뒤에 — 확장칩(bus)이 begin() 에서 준비된다

  WiFi.mode(WIFI_STA);
  wifiService();                 // 첫 후보로 붙어 보기 시작(기다리지 않는다)
}

void loop() {
  // 와이파이 — 붙거나 끊기는 순간에만 다시 그린다(매번 그리면 화면이 계속 쓸린다)
  wifiService();
  const bool nowOnline = (WiFi.status() == WL_CONNECTED);
  if (nowOnline != online) {
    online = nowOnline;
    if (online) Serial.printf("[와이파이] 연결됨 %s (%s)\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    else        Serial.println("[와이파이] 끊김");
    drawCurrent();
  }

  // ── 터치 수 세기: 한 번 = 그림 바꾸기 · 두 번 = 사진 받기 · 세 번 = 지우고 다시 받기 ──
  // '누르는 순간'(떼었다가 다시 누름)만 센다 — 닿아 있는 동안 계속 세면 손을 떼기 전에 몇 번이
  // 되어 버린다. 앞 누름 뒤 TAP_GAP_MS 안에 다시 누르면 이어서 세고, 그 시간이 지나도록
  // 없으면 거기까지로 보고 처리한다. 그래서 **한 번은 0.45초 뒤에** 반응한다 — 두 번·세 번이
  // 올지 봐야 하기 때문이다. 세 번은 더 기다릴 것이 없어 그 자리에서 간다.
  // 120ms 보다 짧게 이어진 누름은 한 번 누르는 사이 터치가 잠깐 끊긴 것으로 보고 세지 않는다.
  const bool down = touching();
  const bool pressEdge = down && !wasDown;
  wasDown = down;

  if (pressEdge && (!taps || millis() - tapMs >= 120)) {
    tapMs = millis();
    if (++taps >= 3) {
      taps = 0;
      photoSyncAll(true);        // 세 번 — 지우고 처음부터
      return;
    }
  }
  if (taps && millis() - tapMs > TAP_GAP_MS) {
    const uint8_t n = taps;
    taps = 0;
    if (n >= 2) {
      photoSyncAll(false);       // 두 번 — 없는 것만
    } else {
      enabled = !enabled;        // 한 번 — 그림 바꾸기
      drawCurrent();
    }
    return;
  }

  delay(20);                     // 초당 50번 확인이면 누름을 놓치지 않는다
}
