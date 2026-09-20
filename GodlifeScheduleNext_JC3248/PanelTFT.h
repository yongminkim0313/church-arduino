// PanelTFT — JC3248W535 의 화면·터치를 TFT_eSPI 와 같은 이름으로 감싼 껍데기.
//
// TalentNfcReader_JC3245 의 같은 이름 파일에서 왔다. 그쪽과 다른 곳은 하나뿐이다 —
// **자를 칸(clip)** 을 더했다. 마퀴가 칸을 넘치는 글자를 옆 칸에 흘리지 않으려면
// 자르는 자리가 있어야 하는데, 원본 GodlifeScheduleNext 는 그것을 TFT_eSprite 의
// 뷰포트로 했다. 이 보드에는 스프라이트가 없다(통짜 버퍼에 바로 그린다).
// → setClip() 으로 칸을 걸고, 그 밖의 점은 drawString·fillRect·fillRoundRect 가 건너뛴다.
// 둥근 카드를 줄 단위로 나눠 그리는 것도 이 자를 칸이 하는 일이다(.ino 의 drawRow).
//
// 이 보드의 3.5" 320×480 패널은 AXS15231B 를 QSPI 로 물린다. TFT_eSPI 는 QSPI 를
// 다루지 못해서(라이브러리의 한계다) 그림은 Arduino_GFX 로 그린다.
// 화면이 또 다른 보드로 옮겨 가도 이 파일 하나만 갈면 된다.
//
// ── 통짜 버퍼로 그린다 ────────────────────────────────────────────
// AXS15231B 는 일부 물량에서 '창 주소(부분 갱신)' 명령을 무시한다고 알려져 있다.
// 그래서 Arduino_Canvas(320×480×2 = 300KB, PSRAM)에 다 그린 뒤 통째로 민다.
// 미는 일은 33ms 마다 도는 전용 태스크가 맡는다 — 그리는 쪽(본체)은 버퍼에만 쓰므로
// QSPI 버스를 다투지 않고, 본체 코드에 flush() 호출을 흩뿌리지 않아도 된다.
//
// 덤으로 **깜빡임이 없다.** 원본이 줄 스프라이트를 따로 둔 까닭이 깜빡임이었는데,
// 여기서는 한 프레임을 다 그린 뒤에야 화면으로 나가므로 그 장치가 필요 없다.
//
// ── 글자 ─────────────────────────────────────────────────────────
// 한글은 VLW 스무스폰트를 VlwFont 로 직접 읽어 그린다.
// 자리 잡는 규칙(datum)과 폭 계산은 TFT_eSPI 와 같게 맞췄다.
//
// ── 터치 ─────────────────────────────────────────────────────────
// AXS15231B 는 화면과 터치가 한 칩이다. 터치는 I2C(0x3B, SDA=4 · SCL=8)로 읽는다.
// 정전식이라 보정이 필요 없다.
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

  int16_t width()  const { return _w; }
  int16_t height() const { return _h; }

  // ── 그리기 ──
  void fillScreen(uint16_t color);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  // r 이 w/2·h/2 만큼 크면 알약(캡슐)이 된다 — D-day 칩과 딱지가 그렇게 그려진다.

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  // ── 자를 칸 ──
  // 건 뒤에 그리는 것은 이 네모 안에서만 점이 찍힌다. 마퀴가 흘리는 글자가 옆
  // 칸을 침범하지 않게 하는 장치다(원본의 TFT_eSprite::setViewport 자리).
  // 좌표는 옮기지 않는다 — 뷰포트와 달리 원점은 화면 왼쪽 위 그대로다.
  void setClip(int32_t x, int32_t y, int32_t w, int32_t h);
  void clearClip();

  // ── 글자 ──
  // 폰트 데이터(수 MB)는 본체(.ino)만 들고 있는다 — 이 파일에서 헤더를 같이 넣으면
  // 같은 배열이 플래시에 두 벌 들어간다. 그래서 주소만 받아 둔다.
  void setBasicFont(const uint8_t* basic) { _basicFont = basic; }
  void loadFont(const uint8_t* vlw);
  void unloadFont();
  bool fontLoaded();
  void setTextDatum(uint8_t d) { _datum = d; }
  void setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; _fillBg = true; }
  void setTextColor(uint16_t fg)              { _fg = fg; _bg = fg; _fillBg = false; }
  int16_t textWidth(const char* s);
  int16_t fontHeight();
  void drawString(const char* s, int32_t x, int32_t y);

  // ── 그 밖 ──
  void writecommand(uint8_t cmd);              // 0x28/0x29(DISPOFF/DISPON) 로 화면을 끄고 켠다
  bool getTouch(uint16_t* x, uint16_t* y);     // 누르고 있는 동안 true
  void flushNow();                             // 지금 바로 민다(부팅 화면처럼 급할 때)

 private:
  void      markDirty() { _dirty = true; }
  uint16_t* fb() const;
  static void flushTask(void* arg);

  // 폰트는 네 칸까지 올려 둔다(한글 20·24 와 숫자 34·64). 칸을 두지 않고 매번
  // 다시 읽으면 화면 한 장에 한글 2,350자 메트릭을 여러 번 훑게 된다.
  static const uint8_t FONT_SLOTS = 4;
  VlwFont* slotFor(const uint8_t* vlw);

  int16_t  _w = 320, _h = 480;
  uint8_t  _datum = TL_DATUM;
  uint16_t _fg = 0xFFFF, _bg = 0x0000;
  bool     _fillBg = false;
  int32_t  _clipX0 = 0, _clipY0 = 0, _clipX1 = 320, _clipY1 = 480;
  VlwFont        _slot[FONT_SLOTS];
  const uint8_t* _slotKey[FONT_SLOTS] = { nullptr, nullptr, nullptr, nullptr };
  uint8_t        _slotNext = 0;
  VlwFont*       _cur = nullptr;               // 지금 글자를 그릴 폰트(없으면 기본 폰트)
  const uint8_t* _basicFont = nullptr;
  volatile bool _dirty = false;
  bool     _ready = false;
};

extern PanelTFT tft;
