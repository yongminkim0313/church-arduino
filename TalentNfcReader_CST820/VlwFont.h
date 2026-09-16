// VlwFont — TFT_eSPI 의 스무스폰트(VLW)를 우리가 직접 읽는다.
//
// 왜 필요한가: 이 보드(JC3248W535)의 화면은 QSPI 라 TFT_eSPI 가 못 다룬다.
// 그래서 그림은 Arduino_GFX 로 그리는데, Arduino_GFX 에는 VLW 를 읽는 기능이 없다.
// 한글 2,350자를 담은 FontKR14/FontKR20 은 이미 VLW 로 구워 둔 것이라(tools/ttf2vlw.py),
// 폰트를 새로 만들기보다 읽는 쪽을 짧게 짜는 편이 낫다 — 원본 리더와 글자 모양이 같아진다.
//
// ── VLW 파일 모양 (TFT_eSPI Smooth_font.cpp 와 같은 해석) ──────────
//   0..23    머리 6개(빅엔디언 int32): 글리프 수 · 판 번호 · 크기 · (버림) · ascent · descent
//   24..     글리프마다 28바이트: 유니코드 · 높이 · 너비 · xAdvance · dY · dX · (버림)
//   그 뒤    글리프 그림이 차례로. 한 점이 1바이트(0-255 알파), 너비×높이 바이트.
//
// 글자를 놓는 자리와 폭 계산은 TFT_eSPI 와 똑같이 맞췄다. 안 그러면 원본 스케치가
// 잡아 둔 좌표(가운데 정렬·오른쪽 정렬)가 조금씩 어긋난다.
#pragma once
#include <Arduino.h>

class VlwFont {
 public:
  // data 는 플래시에 있는 VLW 통짜 배열(FontKR20 등). 메트릭만 RAM 으로 올린다.
  bool load(const uint8_t* data);
  void unload();
  bool loaded() const { return _ok; }

  // 글자 하나의 자리 정보. 없으면 false.
  struct Glyph {
    uint16_t       unicode;
    uint8_t        w, h, xAdvance;
    int16_t        dY;
    int8_t         dX;
    const uint8_t* bitmap;    // w*h 바이트(알파)
  };
  bool glyph(uint16_t unicode, Glyph& out) const;

  uint16_t yAdvance()   const { return _yAdvance; }    // 한 줄 높이(=maxAscent+maxDescent)
  uint16_t maxAscent()  const { return _maxAscent; }   // 글자 윗변에서 기준선까지
  uint16_t spaceWidth() const { return _spaceWidth; }

  // UTF-8 문자열의 픽셀 폭. TFT_eSPI::textWidth 와 같은 셈이다.
  int16_t textWidth(const char* utf8) const;

  // UTF-8 을 유니코드 하나씩 — 남은 문자열 앞으로 옮긴 포인터를 돌려준다.
  static const char* nextCodepoint(const char* p, uint16_t& cp);

 private:
  const uint8_t* _base = nullptr;   // 파일 첫 바이트
  uint16_t  _gCount = 0;
  uint16_t* _unicode = nullptr;
  uint8_t*  _width = nullptr;
  uint8_t*  _height = nullptr;
  uint8_t*  _xAdvance = nullptr;
  int16_t*  _dY = nullptr;
  int8_t*   _dX = nullptr;
  uint32_t* _bitmapOff = nullptr;   // 파일 첫 바이트에서의 자리
  uint16_t  _maxAscent = 0, _maxDescent = 0, _yAdvance = 0, _spaceWidth = 0;
  bool      _ok = false;

  bool index(uint16_t unicode, uint16_t& gNum) const;
};
