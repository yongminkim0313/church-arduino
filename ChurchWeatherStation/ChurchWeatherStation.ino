// ChurchWeatherStation — TTGO T-Display (ESP32)
// 부팅 시 교회 로고 + 비둘기 애니메이션을 재생하고,
// 이후 SHT41 온습도 화면으로 전환해 값을 표시 + 서버로 JSON POST(10초 주기).
//
// = ChurchLogoDisplay(부팅 로고) + SHT41Monitor(온습도) 통합본.
//
// 필요 라이브러리 (라이브러리 매니저):
//   · TFT_eSPI (Bodmer)     — User_Setup_Select.h 에서 Setup25_TTGO_T_Display.h 활성화
//   · Sensirion I2C SHT4x   — SensirionI2CSht4x
//   · (WiFi/HTTPClient/Wire/SPI 는 ESP32 코어 내장)
//
// 배선: SHT41 → SDA=GPIO21, SCL=GPIO22, VCC=3V3, GND=GND
// 보드/업로드 설정은 ../README.md, TFT_eSPI 설정은 ../docs/TFT_eSPI_setup.md 참고.

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include "logo.h"
#include <ChurchSecrets.h>   // WiFi/서버 인증정보 (저장소 밖)

// ── 설정 ──
const char* serverUrl = SENSOR_POST_URL;   // ChurchSecrets.h

#define PIN_BL 4     // LCD 백라이트
#define BTN_TOP 35   // 위쪽 버튼(누르면 부팅 로고 재생)

static const int16_t SCR_W = 240;
static const int16_t SCR_H = 135;
static const int     DOVE_BASE_Y = 18;

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);   // 부팅 애니메이션용 전체화면 버퍼(이후 해제)
SensirionI2cSht4x sht4x;

// ══════════════════════════════════════════════════════════════════
//  부팅 로고 / 비둘기 애니메이션 (ChurchLogoDisplay 기반)
// ══════════════════════════════════════════════════════════════════
static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r  = ar + (int)((br - ar) * t);
  int g  = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void drawBackground() {
  uint16_t top = tft.color565(12, 22, 58);
  uint16_t bot = tft.color565(58, 120, 190);
  for (int y = 0; y < SCR_H; y++) {
    float t = (float)y / (SCR_H - 1);
    spr.drawFastHLine(0, y, SCR_W, lerp565(top, bot, t));
  }
}

static void drawLogoCard(int cx, int cy) {
  const int w = 100, h = 100;
  spr.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 14, TFT_WHITE);
  spr.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 14, tft.color565(205, 216, 232));
#if HAVE_LOGO_IMAGE
  // 디더링된 RGB565 로고.
  // TFT_eSprite 는 내부 버퍼를 빅엔디언으로 들고 있고(pushSprite 가 바이트를 그대로 흘려보낸다),
  // 일반 uint16_t 배열은 리틀엔디언이다. setSwapBytes(true) 를 켜야 복사하며 순서를 맞춰준다.
  // 켜지 않으면 0xF800(빨강)이 0x00F8(짙은 파랑)으로 뒤바뀐다.
  bool oldSwap = spr.getSwapBytes();
  spr.setSwapBytes(true);
  spr.pushImage(cx - LOGO_W / 2, cy - LOGO_H / 2, LOGO_W, LOGO_H, (uint16_t *)logo_data);
  spr.setSwapBytes(oldSwap);
#else
  uint16_t col = tft.color565(28, 60, 120);
  spr.fillRect(cx - 6,  cy - 36, 12, 72, col);
  spr.fillRect(cx - 24, cy - 12, 48, 12, col);
#endif
}

static void drawDove(int cx, int cy, bool wingUp) {
  const uint16_t white = TFT_WHITE;
  const uint16_t shade = tft.color565(210, 224, 240);
  spr.fillTriangle(cx - 13, cy, cx - 24, cy - 5, cx - 24, cy + 5, white);
  spr.fillEllipse(cx, cy, 12, 6, white);
  spr.drawEllipse(cx, cy, 12, 6, shade);
  spr.fillCircle(cx + 11, cy - 4, 4, white);
  spr.fillTriangle(cx + 15, cy - 4, cx + 22, cy - 3, cx + 15, cy - 1, tft.color565(240, 170, 40));
  spr.fillCircle(cx + 12, cy - 5, 1, TFT_BLACK);
  if (wingUp) spr.fillTriangle(cx - 2, cy - 2, cx + 9, cy - 22, cx + 10, cy - 2, white);
  else        spr.fillTriangle(cx - 2, cy + 2, cx + 9, cy + 22, cx + 10, cy + 2, white);
}

// status 가 있으면 하단에 상태 문구를 얹는다(부팅 진행 표시용).
static void composeScene(int doveX, int doveY, bool wingUp, const char* status) {
  drawBackground();
  drawLogoCard(120, 82);
  drawDove(doveX, doveY, wingUp);
  if (status) {
    spr.setTextDatum(BC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(status, SCR_W / 2, SCR_H - 6, 2);
    spr.setTextDatum(TL_DATUM);
  }
  spr.pushSprite(0, 0);
}

static void playLogoIntro(const char* status) {
  const int targetX = 120;
  for (int x = -30; x <= targetX; x += 3) {
    bool wing = ((x / 6) % 2) == 0;
    int  y    = DOVE_BASE_Y + (int)(5.0 * sinf(x * 0.12f));
    composeScene(x, y, wing, status);
    delay(28);
  }
}

// ══════════════════════════════════════════════════════════════════
//  온습도 화면 (SHT41Monitor 기반)
// ══════════════════════════════════════════════════════════════════
static void startWeatherScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
}

// ══════════════════════════════════════════════════════════════════
//  부팅: 로고 → WiFi/센서 준비 → 온습도 화면
// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);   // 백라이트 ON
  pinMode(BTN_TOP, INPUT);

  tft.init();
  tft.setRotation(1);           // 가로 240x135
  tft.fillScreen(TFT_BLACK);

  // ── 부팅 로고 애니메이션 ──
  spr.setColorDepth(16);
  bool haveSprite = spr.createSprite(SCR_W, SCR_H);  // 240x135x2 ≈ 63KB
  if (haveSprite) {
    playLogoIntro(nullptr);
  }

  // ── 센서 초기화 ──
  Wire.begin();                 // SDA=GPIO21, SCL=GPIO22
  sht4x.begin(Wire, SHT41_I2C_ADDR_44);

  // ── Wi-Fi 연결 (로고 화면 위에 상태 표시) ──
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (haveSprite) {
      int y = DOVE_BASE_Y + (int)(5.0 * sinf(dots * 0.5f));
      composeScene(120, y, (dots % 2) == 0, "Connecting WiFi...");
    }
    Serial.print(".");
    delay(500);
    dots++;
  }
  Serial.println("\nWiFi connected");

  // 부팅 애니메이션 종료 → 스프라이트 해제로 HTTP 용 메모리 확보
  if (haveSprite) spr.deleteSprite();

  startWeatherScreen();
}

void loop() {
  // 위쪽 버튼을 누르면 부팅 로고를 다시 재생(원하면 사용)
  if (digitalRead(BTN_TOP) == LOW) {
    if (spr.createSprite(SCR_W, SCR_H)) {
      playLogoIntro(nullptr);
      spr.deleteSprite();
    }
    startWeatherScreen();
  }

  float temperature = 0.0;
  float humidity = 0.0;

  // SHT41 온습도 측정 (High precision)
  int16_t error = sht4x.measureHighPrecision(temperature, humidity);

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);

  if (error) {
    Serial.print("Error executing measurement: ");
    Serial.println(error);
    tft.println("Sensor Error!");
  } else {
    // 디스플레이 출력
    tft.print("Temp: "); tft.print(temperature, 1); tft.println(" C");
    tft.setCursor(10, 40);
    tft.print("Humi: "); tft.print(humidity, 1); tft.println(" %");

    // 서버 전송
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");

      String jsonPayload = "{\"device\":\"ttgo-t-display\",\"temperature\":" + String(temperature, 1) + ",\"humidity\":" + String(humidity, 1) + "}";

      int httpResponseCode = http.POST(jsonPayload);

      tft.setCursor(10, 80);
      if (httpResponseCode > 0) {
        tft.println("Server: OK");
      } else {
        tft.println("Server: Fail");
      }
      http.end();
    }
  }

  delay(10000); // 10초 주기
}
