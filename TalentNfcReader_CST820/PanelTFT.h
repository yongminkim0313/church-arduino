// PanelTFT — LILYGO T-RGB(2.1" 원형, ST7701S + CST820)의 화면·터치를
// TFT_eSPI 와 같은 이름으로 감싼 껍데기.
//
// JC3248W535 판(TalentNfcReader_JC3245)과 같은 자리의 파일이다. 본체(.ino)가 부르는
// 이름은 그대로 두고, 이 파일만 보드에 맞게 갈아 끼운다.
//
// ── 이 보드의 화면 ────────────────────────────────────────────────
// ST7701S 480×480 을 RGB 병렬로 문다(QSPI·SPI 가 아니다). 화면을 켜는 명령은
// I2C 확장칩(XL9535)을 거쳐 들어간다 — Arduino_GFX 의 Arduino_XL9535SWSPI 가 그 일을 한다.
// 그린 그림은 PSRAM 의 프레임버퍼(480×480×2 = 460KB)에 있고, LCD 가 그것을 계속 읽어
// 뿌린다. 그래서 JC3248 판처럼 따로 밀어 주는 태스크가 필요 없다.
//
// ── 둥근 화면에 네모 화면을 올린다 ────────────────────────────────
// 이 기기는 지름 480 의 **원형**이다. 원본 리더의 화면은 네모라 그대로 올리면 네 귀퉁이가
// 잘린다. 그래서 원 안에 들어가는 가장 큰 정사각(약 339)을 잡아, 본체에는 그 크기를
// '화면' 이라고 알려 준다. 그리는 좌표와 터치 좌표를 이 껍데기가 가운데로 옮겨 준다.
// 정사각 밖(둥근 테두리)은 바탕색으로 채운다.
//
// ── 터치 ─────────────────────────────────────────────────────────
// CST820(I2C 0x15). 화면 명령용 확장칩·PN532 와 같은 버스(SDA 8 · SCL 48)를 쓴다 —
// 주소가 서로 달라(0x15 · 0x20 · 0x24) 한 버스로 충분하다.
//
// ── 백라이트 ──────────────────────────────────────────────────────
// 이 보드의 백라이트는 PWM 이 아니다. 핀을 짧게 여러 번 흔들어 16단계 중 하나를 고르는
// 방식이다(LILYGO 의 방식을 그대로 따랐다). analogWrite 로는 밝기가 조절되지 않아
// setBacklight(0-255)로 받아 16단계로 바꿔 넣는다.
#pragma once
#include <Arduino.h>
#include "VlwFont.h"

// 원본이 쓰는 TFT_eSPI 이름들 — 색과 정렬 기준.
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

// 보드 핀 — T-RGB 고정 배선이다(바꿀 수 없다).
#define TR_PANEL_W     480          // 실제 화면(원형 지름)
#define TR_PANEL_H     480
// 원 안에 들어가는 가장 큰 정사각은 480/√2 ≈ 339. 짝수로 맞춰 338 을 쓴다.
// 여기를 줄이면 테두리 여유가 늘고, 늘리면 네 귀퉁이가 잘리기 시작한다.
#define TR_UI_W        338
#define TR_UI_H        338

#define TR_BL           46          // 백라이트(16단계 펄스)
#define TR_I2C_SDA       8          // 터치·확장칩·PN532 가 함께 쓴다
#define TR_I2C_SCL      48
#define TR_TOUCH_INT     1          // CST820 INT — 딥슬립 기상(ext0)에도 쓴다
#define TR_TOUCH_ADDR 0x15          // CST820 (CST816 계열과 같은 주소)

class PanelTFT {
 public:
  void init();
  void setRotation(uint8_t r);       // 0(세로)만 — 원형이라 돌릴 이유도 없다
  void setSwapBytes(bool) {}
  void setTouch(uint16_t*) {}        // 정전식이라 보정하지 않는다

  int16_t width()  const { return TR_UI_W; }
  int16_t height() const { return TR_UI_H; }

  // ── 그리기 (좌표는 본체가 보는 정사각 기준) ──
  void fillScreen(uint16_t color);   // 둥근 테두리까지 같은 색으로 채운다
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data);
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data, uint16_t transparent);

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  // ── 글자 ──
  // 폰트 데이터(수 MB)는 본체(.ino)만 들고 있는다 — 여기서 같은 헤더를 넣으면
  // 플래시에 두 벌이 들어간다. 주소만 받아 둔다.
  void setFonts(const uint8_t* basic, const uint8_t* num4, const uint8_t* num6) {
    _basicFont = basic; _num4Font = num4; _num6Font = num6;
  }
  void loadFont(const uint8_t* vlw);
  void unloadFont();
  void setTextDatum(uint8_t d) { _datum = d; }
  void setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; _fillBg = true; }
  void setTextColor(uint16_t fg)              { _fg = fg; _bg = fg; _fillBg = false; }
  int16_t textWidth(const char* s);
  int16_t textWidth(const String& s) { return textWidth(s.c_str()); }
  int16_t fontHeight();
  // 4번째 인자는 원본이 쓰던 TFT_eSPI 내장 폰트 번호다 — 숫자 VLW 로 바꿔 그린다
  //   4 → FontNum34 · 6 이상 → FontNum64
  void drawString(const char* s, int32_t x, int32_t y, uint8_t builtinFont = 0);
  void drawString(const String& s, int32_t x, int32_t y, uint8_t builtinFont = 0) {
    drawString(s.c_str(), x, y, builtinFont);
  }

  // ── 그 밖 ──
  void writecommand(uint8_t cmd);            // 원본의 슬립 인(0x10) 자리 — 화면을 끈다
  bool getTouch(uint16_t* x, uint16_t* y);   // 누르고 있는 동안 true
  void setBacklight(uint8_t level255);       // 0-255 를 이 보드의 16단계로 바꿔 넣는다
  void flushNow() {}                         // RGB 패널은 늘 프레임버퍼를 읽어 간다

 private:
  static const uint8_t FONT_SLOTS = 4;
  VlwFont* slotFor(const uint8_t* vlw);
  uint16_t* fb() const;

  uint8_t  _datum = TL_DATUM;
  uint16_t _fg = 0xFFFF, _bg = 0x0000;
  bool     _fillBg = false;
  VlwFont        _slot[FONT_SLOTS];
  const uint8_t* _slotKey[FONT_SLOTS] = { nullptr, nullptr, nullptr, nullptr };
  uint8_t        _slotNext = 0;
  VlwFont*       _cur = nullptr;
  const uint8_t* _basicFont = nullptr;
  const uint8_t* _num4Font = nullptr;
  const uint8_t* _num6Font = nullptr;
  uint8_t  _blSteps = 0;                     // 지금 백라이트 단계(0-16)
  bool     _ready = false;
};

extern PanelTFT tft;
