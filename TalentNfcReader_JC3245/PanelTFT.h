// PanelTFT — JC3248W535 의 화면·터치를 TFT_eSPI 와 같은 이름으로 감싼 껍데기.
//
// 이 보드의 3.5" 480×320 패널은 AXS15231B 를 QSPI 로 물린다. TFT_eSPI 는 QSPI 를
// 다루지 못해서(라이브러리의 한계다) 그림은 Arduino_GFX 로 그린다. 그런데 원본
// TalentNfcReader 는 3,800줄 내내 tft.drawString/fillRect/getTouch 를 쓴다.
// 그 호출을 전부 고치는 대신, 같은 이름의 껍데기를 두어 본체를 거의 그대로 옮겼다.
// 화면이 다른 보드로 또 옮겨 갈 때도 이 파일 하나만 갈면 된다.
//
// ── 통짜 버퍼로 그린다 ────────────────────────────────────────────
// AXS15231B 는 일부 물량에서 '창 주소(부분 갱신)' 명령을 무시한다고 알려져 있다.
// 그래서 Arduino_Canvas(320×480×2 = 300KB, PSRAM)에 다 그린 뒤 통째로 민다.
// 미는 일은 33ms 마다 도는 전용 태스크가 맡는다 — 그리는 쪽(본체)은 버퍼에만 쓰므로
// QSPI 버스를 다투지 않고, 본체 코드에 flush() 호출을 흩뿌리지 않아도 된다.
//
// ── 글자 ─────────────────────────────────────────────────────────
// 한글은 VLW 스무스폰트를 VlwFont 로 직접 읽어 그린다(원본과 같은 FontKR14/20).
// 자리 잡는 규칙(datum)과 폭 계산은 TFT_eSPI 와 같게 맞췄다.
//
// ── 터치 ─────────────────────────────────────────────────────────
// AXS15231B 는 화면과 터치가 한 칩이다. 터치는 I2C(0x3B, SDA=4 · SCL=8)로 읽는다.
// 정전식이라 원본의 XPT2046 처럼 보정(setTouch)이 필요 없다 — 그 함수는 빈 껍데기다.
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

// 보드 핀 — JC3248W535C_I_Y 고정 배선이다(바꿀 수 없다).
#define JC_LCD_CS    45
#define JC_LCD_SCK   47
#define JC_LCD_D0    21
#define JC_LCD_D1    48
#define JC_LCD_D2    40
#define JC_LCD_D3    39
#define JC_LCD_BL     1
#define JC_TOUCH_SDA  4
#define JC_TOUCH_SCL  8
#define JC_TOUCH_INT  3     // 가이션 회로도의 TP_INT. 폴링만 쓰면 없어도 된다
#define JC_TOUCH_ADDR 0x3B

class PanelTFT {
 public:
  void init();                       // 화면을 켜고 버퍼를 잡고 미는 태스크를 띄운다
  void setRotation(uint8_t r);       // 0(세로)만 지원한다 — 그 밖의 값은 무시하고 알린다
  void setSwapBytes(bool) {}         // 버퍼에 그대로 담으므로 할 일이 없다
  void setTouch(uint16_t*) {}        // 정전식이라 보정하지 않는다

  int16_t width()  const { return _w; }
  int16_t height() const { return _h; }

  // ── 그리기 ──
  void fillScreen(uint16_t color);
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
  // 폰트 데이터(수 MB)는 본체(.ino)만 들고 있는다 — 이 파일에서 헤더를 같이 넣으면
  // 같은 배열이 플래시에 두 벌 들어간다. 그래서 주소만 받아 둔다.
  //   basic  : 폰트를 고르지 않았을 때 쓸 한글 폰트(FontKR14)
  //   num4/6 : 원본의 내장 폰트 4·6번 자리에 쓸 숫자 폰트
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
  // 4번째 인자는 원본이 쓰던 TFT_eSPI 내장 폰트 번호다. 내장 폰트는 ASCII 뿐이고
  // 이 화면에는 너무 작아서, 같은 자리에 쓰라고 구운 숫자 VLW 로 바꿔 그린다
  //   4 → FontNum34(원본 26px 자리) · 6 이상 → FontNum64(원본 48px 자리)
  void drawString(const char* s, int32_t x, int32_t y, uint8_t builtinFont = 0);
  void drawString(const String& s, int32_t x, int32_t y, uint8_t builtinFont = 0) {
    drawString(s.c_str(), x, y, builtinFont);
  }

  // ── 그 밖 ──
  void writecommand(uint8_t cmd);              // 원본의 슬립 인(0x10) 자리 — 화면을 끈다
  bool getTouch(uint16_t* x, uint16_t* y);     // 누르고 있는 동안 true
  void flushNow();                             // 지금 바로 민다(부팅 화면처럼 급할 때)

 private:
  void      markDirty() { _dirty = true; }
  uint16_t* fb() const;
  static void flushTask(void* arg);

  // 폰트는 네 칸까지 올려 둔다(한글 14·20 과 숫자 34·64). 칸을 두지 않고 매번
  // 다시 읽으면 화면 한 장에 한글 2,350자 메트릭을 여러 번 훑게 된다 — 원본에서도
  // 그것 때문에 띠 하나 그리는 데 64ms 가 걸린 적이 있다.
  static const uint8_t FONT_SLOTS = 4;
  VlwFont* slotFor(const uint8_t* vlw);

  int16_t  _w = 320, _h = 480;
  uint8_t  _datum = TL_DATUM;
  uint16_t _fg = 0xFFFF, _bg = 0x0000;
  bool     _fillBg = false;
  VlwFont        _slot[FONT_SLOTS];
  const uint8_t* _slotKey[FONT_SLOTS] = { nullptr, nullptr, nullptr, nullptr };
  uint8_t        _slotNext = 0;
  VlwFont*       _cur = nullptr;               // 지금 글자를 그릴 폰트(없으면 기본 한글 14px)
  const uint8_t* _basicFont = nullptr;
  const uint8_t* _num4Font = nullptr;
  const uint8_t* _num6Font = nullptr;
  volatile bool _dirty = false;
  bool     _ready = false;
};

extern PanelTFT tft;
