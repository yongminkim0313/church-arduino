// ChurchLogoDisplay — LILYGO TTGO T-Display (ESP32)
// 부팅 시 교회 로고(디더링 RGB565)를 카드에 띄우고, 비둘기가 날아드는 애니메이션을 재생한다.
//
// 하드웨어: TTGO T-Display / ESP32-D0WDQ6 / ST7789V 240x135 IPS
// 라이브러리: TFT_eSPI (Bodmer) — User_Setup_Select.h 에서 Setup25_TTGO_T_Display.h 활성화 필요
// 화면 방향: setRotation(1) → 가로 240x135
// 백라이트: GPIO4 (LED_BUILTIN 없음)
// 색상: RGB565(16비트). 스프라이트 pushImage 에 배열 넘길 땐 (uint16_t *) 캐스팅.
//
// 자세한 보드/업로드 설정은 ../README.md 참고.

#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include "logo.h"

// ── 핀 (Setup25 가 TFT 핀을 정의하므로 여기선 참고/제어용만) ──
#define PIN_BL    4     // LCD 백라이트
#define BTN_TOP   35    // 위쪽 버튼(입력 전용, 풀업 없음)
#define BTN_BOT   0     // 아래쪽 버튼(부팅 모드 겸용)

static const int16_t SCR_W = 240;
static const int16_t SCR_H = 135;

TFT_eSPI  tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);   // 전체화면 프레임버퍼(플리커 방지)

// ── RGB565 두 색 보간(그라디언트용) ──
static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r  = ar + (int)((br - ar) * t);
  int g  = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// ── 배경: 세로 그라디언트 하늘 ──
static void drawBackground() {
  uint16_t top = tft.color565(12, 22, 58);    // 짙은 남색
  uint16_t bot = tft.color565(58, 120, 190);  // 하늘색
  for (int y = 0; y < SCR_H; y++) {
    float t = (float)y / (SCR_H - 1);
    spr.drawFastHLine(0, y, SCR_W, lerp565(top, bot, t));
  }
}

// ── 로고 카드 ──
// 실제 교회 로고는 tools/img2rgb565_dither.py 로 logo.h 를 생성하면
// HAVE_LOGO_IMAGE 가 1 이 되어 pushImage 경로를 탄다.
static void drawLogoCard(int cx, int cy) {
  const int w = 100, h = 100;
  spr.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 14, TFT_WHITE);
  spr.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 14, tft.color565(205, 216, 232));

#if HAVE_LOGO_IMAGE
  // 디더링된 RGB565 로고. 배열 인자에는 (uint16_t *) 캐스팅이 필요하다.
  spr.pushImage(cx - LOGO_W / 2, cy - LOGO_H / 2, LOGO_W, LOGO_H, (uint16_t *)logo_data);
#else
  // 로고 헤더가 없을 때의 플레이스홀더: 심플한 십자가
  uint16_t col = tft.color565(28, 60, 120);
  spr.fillRect(cx - 6,  cy - 36, 12, 72, col);
  spr.fillRect(cx - 24, cy - 12, 48, 12, col);
#endif
}

// ── 비둘기(성령/평화) 실루엣. wingUp 으로 날갯짓 프레임 전환 ──
static void drawDove(int cx, int cy, bool wingUp) {
  const uint16_t white = TFT_WHITE;
  const uint16_t shade = tft.color565(210, 224, 240);
  // 꼬리
  spr.fillTriangle(cx - 13, cy, cx - 24, cy - 5, cx - 24, cy + 5, white);
  // 몸통
  spr.fillEllipse(cx, cy, 12, 6, white);
  spr.drawEllipse(cx, cy, 12, 6, shade);
  // 머리
  spr.fillCircle(cx + 11, cy - 4, 4, white);
  // 부리
  spr.fillTriangle(cx + 15, cy - 4, cx + 22, cy - 3, cx + 15, cy - 1, tft.color565(240, 170, 40));
  // 눈
  spr.fillCircle(cx + 12, cy - 5, 1, TFT_BLACK);
  // 날개(위/아래로 펄럭)
  if (wingUp) spr.fillTriangle(cx - 2, cy - 2, cx + 9, cy - 22, cx + 10, cy - 2, white);
  else        spr.fillTriangle(cx - 2, cy + 2, cx + 9, cy + 22, cx + 10, cy + 2, white);
}

// ── 한 프레임 합성 후 화면에 전송 ──
static void composeScene(int doveX, int doveY, bool wingUp) {
  drawBackground();
  drawLogoCard(120, 82);
  drawDove(doveX, doveY, wingUp);
  spr.pushSprite(0, 0);
}

// ── 비둘기 날아드는 인트로 ──
static void playIntro() {
  const int targetX = 120;
  const int baseY   = 18;
  for (int x = -30; x <= targetX; x += 3) {
    bool wing = ((x / 6) % 2) == 0;         // 날갯짓
    int  y    = baseY + (int)(5.0 * sinf(x * 0.12f));
    composeScene(x, y, wing);
    delay(28);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);              // 백라이트 ON
  pinMode(BTN_TOP, INPUT);
  pinMode(BTN_BOT, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);                      // 가로 240x135
  // 현재 구성에선 invertDisplay 를 넣지 않는 것이 정상(색 반전 시에만 tft.invertDisplay(1)).
  tft.fillScreen(TFT_BLACK);

  spr.setColorDepth(16);                   // RGB565
  if (!spr.createSprite(SCR_W, SCR_H)) {   // 240x135x2 ≈ 63KB
    // 스프라이트 할당 실패 시 최소한 배경만이라도 표시
    tft.fillScreen(tft.color565(58, 120, 190));
    Serial.println("[WARN] createSprite 실패 — 메모리 확인");
    return;
  }

  playIntro();
}

void loop() {
  // 위쪽 버튼(GPIO35)을 누르면 인트로 재생
  if (digitalRead(BTN_TOP) == LOW) {
    playIntro();
    delay(150);
  }

  // 평상시: 비둘기가 로고 위에서 부드럽게 부유 + 날갯짓
  static float phase = 0.0f;
  phase += 0.16f;
  int  y    = 18 + (int)(5.0 * sinf(phase));
  bool wing = ((int)(phase * 1.6f) % 2) == 0;
  composeScene(120, y, wing);
  delay(55);
}
