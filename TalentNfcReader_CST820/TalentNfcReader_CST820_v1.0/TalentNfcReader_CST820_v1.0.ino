// TalentNfcReader_CST820_v1.0 — 실습 1단계: 배경과 가운데 로고만 띄운다
//
// 보드: LILYGO T-RGB 2.1" 원형 (ESP32-S3 · ST7701S RGB 480×480 · CST820 터치)
// 본체(TalentNfcReader_CST820)에서 화면 켜는 부분만 떼어 낸 것이다.
// NFC·와이파이·터치·한글 폰트는 아직 없다 — 화면이 뜨는 원리부터 본다.
//
// ── 화면이 켜지는 순서 ──────────────────────────────────────────────
//   1) XL9535 확장칩   화면에 명령을 보내는 줄(CS·SCK·MOSI)과 전원 인에이블이 여기 붙어 있다
//   2) RGB 패널        ESP32 가 16개 데이터선으로 픽셀을 계속 흘려보낸다(PSRAM 프레임버퍼)
//   3) 초기화 표       ST7701S 에 "이런 화면이다" 를 알려 준다(2.1인치는 type4)
//   4) 백라이트        GPIO46 을 켜야 보인다
//
// ── 빌드 (Arduino IDE 도구 메뉴) ────────────────────────────────────
//   보드: ESP32S3 Dev Module   PSRAM: OPI PSRAM   Flash Size: 16MB
//   USB CDC On Boot: Enabled   ← 꺼져 있으면 다음부터 포트가 안 보인다
//   라이브러리: GFX Library for Arduino
//
// ※ 이 폴더는 본체 폴더(TalentNfcReader_CST820) 안에 있지만 따로 빌드한다.
//   아두이노는 폴더 안의 .ino 를 모두 합쳐 빌드하므로, 본체 폴더 바로 아래에 두면
//   setup()·loop() 가 두 번 생겨 본체가 빌드되지 않는다. 그래서 하위 폴더로 뺐다.

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include "FunFunLogo.h"          // 본체와 같은 로고 그림(RGB565) — FUNFUN_LOGO 109×24

// ── 핀 ────────────────────────────────────────────────────────────
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL  48
#define PIN_BL       46          // 백라이트

// ── 색 ────────────────────────────────────────────────────────────
// 본체의 기본 바탕색 #EBAC42(주황)을 RGB565 로 바꾼 값이다.
//   R 0xEB → 5비트, G 0xAC → 6비트, B 0x42 → 5비트
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static const uint16_t COLOR_BG   = rgb565(0xEB, 0xAC, 0x42);
static const uint16_t COLOR_CARD = 0xFFFF;   // 흰색

// 로고를 몇 배로 키울지. 원본이 109×24 라 둥근 480 화면에서는 너무 작다.
static const int LOGO_SCALE = 3;             // 327×72

// ── 화면 객체 ─────────────────────────────────────────────────────
Arduino_XL9535SWSPI *bus = new Arduino_XL9535SWSPI(
    PIN_I2C_SDA, PIN_I2C_SCL,
    2 /* 전원 인에이블 */, 3 /* CS */, 5 /* SCK */, 4 /* MOSI */);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    45 /* DE */, 41 /* VSYNC */, 47 /* HSYNC */, 42 /* PCLK */,
    21 /* R0 */, 18 /* R1 */, 17 /* R2 */, 16 /* R3 */, 15 /* R4 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    7  /* B0 */, 6  /* B1 */, 5  /* B2 */, 3  /* B3 */, 2  /* B4 */,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 1 /* hsync_pulse_width */, 30 /* hsync_back_porch */,
    1 /* vsync_polarity */, 20 /* vsync_front_porch */, 1 /* vsync_pulse_width */, 30 /* vsync_back_porch */,
    1 /* pclk_active_neg */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0 /* 회전 */, true /* 그릴 때마다 바로 반영 */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_type4_init_operations, sizeof(st7701_type4_init_operations));

// 로고를 LOGO_SCALE 배로 키워 (cx, cy) 가 가운데가 되게 그린다.
// 한 점을 LOGO_SCALE×LOGO_SCALE 네모로 칠하는 가장 단순한 확대(최근접 이웃)다.
static void drawLogoCentered(int cx, int cy) {
  const int w = FUNFUN_LOGO_W * LOGO_SCALE;
  const int h = FUNFUN_LOGO_H * LOGO_SCALE;
  const int x0 = cx - w / 2;
  const int y0 = cy - h / 2;

  for (int y = 0; y < FUNFUN_LOGO_H; y++) {
    for (int x = 0; x < FUNFUN_LOGO_W; x++) {
      const uint16_t c = FUNFUN_LOGO[y * FUNFUN_LOGO_W + x];
      gfx->fillRect(x0 + x * LOGO_SCALE, y0 + y * LOGO_SCALE, LOGO_SCALE, LOGO_SCALE, c);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== CST820 실습 v1.0 — 배경과 로고 ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  // 1~3) 확장칩 → RGB 패널 → 초기화 표. begin() 한 번이 다 한다.
  if (!gfx->begin()) {
    Serial.println("화면 시작 실패 — PSRAM(OPI) 설정을 확인하세요");
    return;
  }

  // 배경 — 둥근 화면이지만 버퍼는 480×480 네모다. 전체를 칠하면 원 안이 모두 이 색이 된다.
  gfx->fillScreen(COLOR_BG);

  // 흰 카드 — 로고 그림은 흰 바탕에 합성돼 있어서(FunFunLogo.h 머리말),
  // 주황 위에 바로 그리면 로고 둘레에 흰 네모가 드러난다. 흰 카드를 먼저 깔아 자연스럽게 만든다.
  const int cardW = FUNFUN_LOGO_W * LOGO_SCALE + 40;
  const int cardH = FUNFUN_LOGO_H * LOGO_SCALE + 40;
  gfx->fillRoundRect(240 - cardW / 2, 240 - cardH / 2, cardW, cardH, 24, COLOR_CARD);

  // 로고 — 화면 한가운데(240, 240)
  drawLogoCentered(240, 240);

  // 4) 백라이트를 마지막에 켠다 — 먼저 켜면 그리는 동안 빈 화면이 번쩍인다.
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  Serial.println("그렸습니다");
}

void loop() {
  delay(1000);                   // 한 번 그리고 나면 할 일이 없다
}
