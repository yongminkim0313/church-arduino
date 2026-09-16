#include "VlwFont.h"

// 빅엔디언 int32 하나 — VLW 는 자바(Processing)에서 나온 형식이라 빅엔디언이다.
static inline uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// 메트릭은 PSRAM 에 올린다 — 2,452자면 약 20KB 다. PSRAM 이 없으면 내부 힙으로 간다.
static void* alloc(size_t n) {
  void* p = ps_malloc(n);
  return p ? p : malloc(n);
}

bool VlwFont::load(const uint8_t* data) {
  unload();
  if (!data) return false;

  _base           = data;
  _gCount         = (uint16_t)be32(data + 0);
  const uint16_t ascent  = (uint16_t)be32(data + 16);
  const uint16_t descent = (uint16_t)be32(data + 20);
  if (!_gCount) return false;

  _unicode   = (uint16_t*)alloc(_gCount * sizeof(uint16_t));
  _width     = (uint8_t*) alloc(_gCount);
  _height    = (uint8_t*) alloc(_gCount);
  _xAdvance  = (uint8_t*) alloc(_gCount);
  _dY        = (int16_t*) alloc(_gCount * sizeof(int16_t));
  _dX        = (int8_t*)  alloc(_gCount);
  _bitmapOff = (uint32_t*)alloc(_gCount * sizeof(uint32_t));
  if (!_unicode || !_width || !_height || !_xAdvance || !_dY || !_dX || !_bitmapOff) {
    unload();
    return false;
  }

  _maxAscent  = ascent;
  _maxDescent = descent;

  const uint8_t* m   = data + 24;                 // 메트릭이 시작하는 곳
  uint32_t       off = 24 + (uint32_t)_gCount * 28;  // 글리프 그림이 시작하는 곳

  for (uint16_t i = 0; i < _gCount; i++, m += 28) {
    _unicode[i]   = (uint16_t)be32(m + 0);
    _height[i]    = (uint8_t) be32(m + 4);
    _width[i]     = (uint8_t) be32(m + 8);
    _xAdvance[i]  = (uint8_t) be32(m + 12);
    _dY[i]        = (int16_t) be32(m + 16);
    _dX[i]        = (int8_t)  be32(m + 20);
    _bitmapOff[i] = off;
    off += (uint32_t)_width[i] * _height[i];

    // ascent/descent 는 'd'·'p' 기준이라 한글에는 모자란다. TFT_eSPI 와 같은 규칙으로
    // 실제 글리프에서 가장 위·아래를 찾아 넓힌다(제어문자·애매한 값은 건너뛴다).
    if (_dY[i] > (int16_t)_maxAscent &&
        ((_unicode[i] > 0x20 && _unicode[i] < 0x7F) || _unicode[i] > 0xA0)) {
      _maxAscent = _dY[i];
    }
    const int16_t below = (int16_t)_height[i] - _dY[i];
    if (below > (int16_t)_maxDescent &&
        ((_unicode[i] > 0x20 && _unicode[i] < 0xA0 && _unicode[i] != 0x7F) || _unicode[i] > 0xFF)) {
      _maxDescent = below;
    }
  }

  _yAdvance   = _maxAscent + _maxDescent;
  _spaceWidth = (ascent + descent) * 2 / 7;   // TFT_eSPI 의 어림값과 같게
  _ok         = true;
  return true;
}

void VlwFont::unload() {
  free(_unicode);   _unicode = nullptr;
  free(_width);     _width = nullptr;
  free(_height);    _height = nullptr;
  free(_xAdvance);  _xAdvance = nullptr;
  free(_dY);        _dY = nullptr;
  free(_dX);        _dX = nullptr;
  free(_bitmapOff); _bitmapOff = nullptr;
  _base = nullptr;
  _gCount = _maxAscent = _maxDescent = _yAdvance = _spaceWidth = 0;
  _ok = false;
}

// 유니코드는 파일에 오름차순으로 들어 있다 — 이분 탐색.
bool VlwFont::index(uint16_t unicode, uint16_t& gNum) const {
  if (!_ok) return false;
  uint16_t lo = 0, hi = _gCount;
  while (lo < hi) {
    const uint16_t mid = (lo + hi) / 2;
    if (_unicode[mid] == unicode) { gNum = mid; return true; }
    if (_unicode[mid] < unicode) lo = mid + 1;
    else                         hi = mid;
  }
  return false;
}

bool VlwFont::glyph(uint16_t unicode, Glyph& out) const {
  uint16_t i;
  if (!index(unicode, i)) return false;
  out.unicode  = _unicode[i];
  out.w        = _width[i];
  out.h        = _height[i];
  out.xAdvance = _xAdvance[i];
  out.dY       = _dY[i];
  out.dX       = _dX[i];
  out.bitmap   = _base + _bitmapOff[i];
  return true;
}

const char* VlwFont::nextCodepoint(const char* p, uint16_t& cp) {
  const uint8_t* s = (const uint8_t*)p;
  const uint8_t  c = s[0];
  if (c < 0x80)            { cp = c;                                             return p + 1; }
  if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
    cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);                                      return p + 2;
  }
  if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);              return p + 3;
  }
  // 4바이트(이모지 등)는 우리 폰트에 없다 — 건너뛰되 글자 하나로 센다.
  if ((c & 0xF8) == 0xF0) { cp = 0xFFFD;                                         return p + 4; }
  cp = 0xFFFD;                                                                   return p + 1;
}

// TFT_eSPI::textWidth 와 같은 셈: 마지막 글자는 xAdvance 대신 실제 오른쪽 끝까지만 센다.
int16_t VlwFont::textWidth(const char* utf8) const {
  if (!_ok || !utf8) return 0;
  int32_t w = 0;
  const char* p = utf8;
  while (*p) {
    uint16_t cp;
    p = nextCodepoint(p, cp);
    if (!cp) continue;
    if (cp == 0x20) { w += _spaceWidth; continue; }
    uint16_t i;
    if (!index(cp, i)) { w += _spaceWidth + 1; continue; }
    if (w == 0 && _dX[i] < 0) w -= _dX[i];
    if (*p) w += _xAdvance[i];
    else    w += _dX[i] + _width[i];
  }
  return (int16_t)w;
}
