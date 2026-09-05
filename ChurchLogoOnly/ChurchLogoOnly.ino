// ChurchLogoOnly — LILYGO TTGO T-Display (ESP32)
// 교회 로고 한 장만 화면에 띄운다. 애니메이션도 센서도 없다.
//
// 하드웨어: TTGO T-Display / ESP32-D0WDQ6 / ST7789V 240x135 IPS
// 라이브러리: TFT_eSPI (Bodmer) — User_Setup_Select.h 에서 Setup25_TTGO_T_Display.h 활성화
//             (../docs/TFT_eSPI_setup.md 참고)
// 화면 방향: setRotation(1) → 가로 240x135
//
// logo.h 는 tools/img2rgb565_dither.py 로 생성한 RGB565 배열이다.
// 화면 크기(240x135)에 맞춰 뽑았으므로 전체 화면을 채운다.
// 다시 만들려면 ../README.md 또는 이 폴더의 README.md 참고.

#include <TFT_eSPI.h>
#include <SPI.h>
#include "logo.h"

#if !HAVE_LOGO_IMAGE
#error "logo.h 에 로고 데이터가 없습니다. README.md 의 생성 명령을 실행하세요."
#endif

// ── 핀 ──
#define PIN_BL      4      // LCD 백라이트
#define BL_CHANNEL  0      // 백라이트 페이드인용 LEDC 채널 (코어 2.x 전용)

static const int16_t SCR_W = 240;
static const int16_t SCR_H = 135;

// 로고가 화면보다 작을 때 남는 부분을 채울 색. 로고 배경(흰색)과 맞춰둔다.
static const uint16_t BG_COLOR = TFT_WHITE;

// ── 바이트 순서 ──
// tft.pushImage() 는 배열의 바이트를 그대로 SPI 로 흘려보낸다. ESP32 메모리는 리틀엔디언이라
// 0xF800(빨강)이 0x00F8 로 나가서 색이 뒤바뀐다. 그래서 직접 push 할 땐 반드시 켜야 한다.
// (ChurchLogoDisplay 처럼 TFT_eSprite 를 거치면 스프라이트가 내부 버퍼에 이미 뒤집어 저장하므로
//  그쪽에선 이 설정이 필요 없다 — 두 경로의 동작이 다르다.)
// 그래도 빨강↔파랑이 뒤바뀌어 보이면 false 로 바꿔 본다.
#define SWAP_BYTES  true

TFT_eSPI tft = TFT_eSPI();

// ── 백라이트 끄기 ──
// 주의: TFT_eSPI 의 init() 이 TFT_BL(GPIO4) 을 무조건 HIGH 로 만든다(TFT_eSPI.cpp:786).
// 그래서 이 함수는 반드시 tft.init() '다음에' 불러야 한다. 앞에서 끄면 init() 이 도로 켜서
// 로고를 그리기 전의 흰 화면이 그대로 보인다.
static void backlightOff() {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, LOW);
}

// ── 백라이트를 서서히 켠다(전원 인가 직후의 깜빡임을 감춘다) ──
// LEDC API 는 ESP32 코어 3.x 에서 핀 기반으로 바뀌었다.
static void backlightFadeIn(uint16_t ms = 400) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(PIN_BL, 5000, 8)) {      // 5kHz, 8bit
    digitalWrite(PIN_BL, HIGH);            // PWM 을 못 잡으면 그냥 켠다(화면이 검게 남지 않도록)
    Serial.println("[WARN] ledcAttach 실패 — 페이드 없이 켬");
    return;
  }
  #define BL_WRITE(duty) ledcWrite(PIN_BL, (duty))
#else
  if (ledcSetup(BL_CHANNEL, 5000, 8) == 0) {
    digitalWrite(PIN_BL, HIGH);
    Serial.println("[WARN] ledcSetup 실패 — 페이드 없이 켬");
    return;
  }
  ledcAttachPin(PIN_BL, BL_CHANNEL);
  #define BL_WRITE(duty) ledcWrite(BL_CHANNEL, (duty))
#endif
  for (int duty = 0; duty <= 100; duty += 5) {
    BL_WRITE(duty);
    delay(ms / 51);
  }
  BL_WRITE(100);
}

void setup() {
  Serial.begin(115200);
  delay(50);

  tft.init();                              // 이 안에서 백라이트가 켜진다
  backlightOff();                          // 로고를 다 그릴 때까지 다시 꺼 둔다
  tft.setRotation(1);                      // 가로 240x135
  tft.fillScreen(BG_COLOR);

  // 로고가 화면보다 작으면 가운데 정렬, 같으면 (0,0) 부터 꽉 채운다.
  const int16_t x = (SCR_W - LOGO_W) / 2;
  const int16_t y = (SCR_H - LOGO_H) / 2;

  // const uint16_t* 를 그대로 넘기면 PROGMEM 전용 오버로드(pgm_read_word)가 선택된다.
  // 색 전체가 음화처럼 반전돼 보이면 여기서 tft.invertDisplay(1) 을 추가한다.
  tft.setSwapBytes(SWAP_BYTES);
  tft.pushImage(x, y, LOGO_W, LOGO_H, logo_data);

  backlightFadeIn();                       // 로고가 다 올라간 뒤에 화면을 보여준다

  // logo.h 가 제대로 링크됐는지 확인용. 헤더의 첫 값과 같아야 한다.
  Serial.printf("[ChurchLogoOnly] %dx%d 로고를 (%d,%d) 에 표시 (%u bytes), 첫 픽셀 0x%04X\n",
                LOGO_W, LOGO_H, x, y, (unsigned)(LOGO_W * LOGO_H * 2),
                pgm_read_word(&logo_data[0]));
}

void loop() {
  // 정지 화면이라 할 일이 없다. 화면은 그대로 유지된다.
  delay(1000);
}
