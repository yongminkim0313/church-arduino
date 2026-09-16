#include "PanelTFT.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>

PanelTFT tft;

static Arduino_XL9535SWSPI*    s_bus   = nullptr;   // 화면 명령 — XL9535 확장칩을 거친다
static Arduino_ESP32RGBPanel*  s_rgb   = nullptr;
static Arduino_RGB_Display*    s_gfx   = nullptr;

// 정사각 UI 를 원 가운데에 놓는다
static const int16_t OX = (TR_PANEL_W - TR_UI_W) / 2;
static const int16_t OY = (TR_PANEL_H - TR_UI_H) / 2;

uint16_t* PanelTFT::fb() const { return s_gfx ? s_gfx->getFramebuffer() : nullptr; }

void PanelTFT::init() {
  // 화면 명령 줄(CS·SCK·MOSI)과 전원 인에이블이 모두 XL9535 확장칩에 붙어 있다.
  // 전원 인에이블(확장칩 2번)은 배터리로 돌릴 때 반드시 켜야 한다 — 라이브러리가 켜 준다.
  s_bus = new Arduino_XL9535SWSPI(TR_I2C_SDA, TR_I2C_SCL,
                                  2 /* XL 전원 인에이블 */, 3 /* XL CS */,
                                  5 /* XL SCK */, 4 /* XL MOSI */);

  // RGB 병렬 타이밍 — LILYGO 의 Arduino_GFX 예제 값 그대로다.
  // 색이 뒤집혀 보이면(빨강↔파랑) 아래 R 묶음과 B 묶음을 맞바꾼다.
  s_rgb = new Arduino_ESP32RGBPanel(
      45 /* DE */, 41 /* VSYNC */, 47 /* HSYNC */, 42 /* PCLK */,
      21 /* R0 */, 18 /* R1 */, 17 /* R2 */, 16 /* R3 */, 15 /* R4 */,
      14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
      7  /* B0 */, 6  /* B1 */, 5  /* B2 */, 3  /* B3 */, 2  /* B4 */,
      1 /* hsync_polarity */, 50 /* hsync_front_porch */, 1 /* hsync_pulse_width */, 30 /* hsync_back_porch */,
      1 /* vsync_polarity */, 20 /* vsync_front_porch */, 1 /* vsync_pulse_width */, 30 /* vsync_back_porch */,
      1 /* pclk_active_neg */);

  // 2.1인치 원형 화면의 초기화 표는 Arduino_GFX 가 들고 있다(st7701_type4).
  // 2.8인치 판은 type20 이다 — 보드를 바꾸면 여기를 갈아 끼운다.
  s_gfx = new Arduino_RGB_Display(
      TR_PANEL_W, TR_PANEL_H, s_rgb, 0 /* rotation */, true /* auto_flush */,
      s_bus, GFX_NOT_DEFINED /* RST */,
      st7701_type4_init_operations, sizeof(st7701_type4_init_operations));

  Wire.begin(TR_I2C_SDA, TR_I2C_SCL, 400000);

  if (!s_gfx->begin()) {           // PSRAM 이 없으면 460KB 프레임버퍼를 못 잡는다
    Serial.println("[화면] 시작 실패 — PSRAM(opi) 설정을 확인하세요");
    _ready = false;
    return;
  }
  _ready = true;
  s_gfx->fillScreen(TFT_BLACK);

  pinMode(TR_BL, OUTPUT);
  digitalWrite(TR_BL, LOW);        // 본체가 밝기를 정할 때까지 꺼 둔다
  _blSteps = 0;

  pinMode(TR_TOUCH_INT, INPUT_PULLUP);   // 폴링으로 읽지만, 딥슬립 기상에 쓰려면 입력이라야 한다

  // ── 터치 칩을 깨운다 ──
  // CST820 의 리셋은 ESP32 핀이 아니라 **확장칩(XL9535)의 1번**에 붙어 있다(TP_RES).
  // 켜면 리셋에 잡힌 채라 I2C 에 아예 나오지 않는다 — 실기에서 터치가 죽어 있던 이유가
  // 이것이었다(0x20 만 보이다가, 이 펄스 뒤에 0x15 가 나타났다).
  s_bus->pinMode(1, OUTPUT);
  s_bus->digitalWrite(1, 0);
  delay(30);
  s_bus->digitalWrite(1, 1);
  delay(200);

  // 자동 잠들기 끄기(CST816 계열의 0xFE). 그대로 두면 가만히 있는 동안 응답하지 않아
  // 첫 터치를 놓친다. 실패해도 그냥 간다 — 없는 칩이면 어차피 터치가 없는 기기다.
  Wire.beginTransmission(TR_TOUCH_ADDR);
  Wire.write(0xFE);
  Wire.write(0x01);
  const bool awake = (Wire.endTransmission() == 0);
  Serial.printf("[터치] CST820 %s\n", awake ? "준비됨" : "응답 없음 — 배선을 확인하세요");
}

void PanelTFT::setRotation(uint8_t r) {
  if (r != 0) Serial.printf("[화면] 회전 %u 는 이 보드에서 지원하지 않습니다 — 세로(0)로 둡니다\n", r);
}

// ── 백라이트 ──────────────────────────────────────────────────────
// PWM 이 아니다. 핀을 짧게 흔든 횟수로 16단계 중 하나를 고른다(LILYGO 방식).
// 0-255 로 받아 0-16 으로 줄여 넣는다 — 서버 설정(backlight)이 0-255 이기 때문이다.
void PanelTFT::setBacklight(uint8_t level255) {
  const uint8_t steps = 16;
  uint8_t value = (uint8_t)(((uint16_t)level255 * steps + 127) / 255);   // 0-16
  if (value > steps) value = steps;
  if (value == _blSteps) return;

  if (value == 0) { digitalWrite(TR_BL, LOW); delay(3); _blSteps = 0; return; }
  if (_blSteps == 0) {                       // 꺼져 있었으면 가장 밝은 단계에서 시작한다
    digitalWrite(TR_BL, HIGH);
    _blSteps = steps;
    delayMicroseconds(30);
  }
  const int from = steps - _blSteps;
  const int to   = steps - value;
  const int num  = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) { digitalWrite(TR_BL, LOW); digitalWrite(TR_BL, HIGH); }
  _blSteps = value;
}

// ── 그리기 ────────────────────────────────────────────────────────
// 본체가 주는 좌표는 정사각(338×338) 기준이다. 원 가운데로 옮겨 그린다.
void PanelTFT::fillScreen(uint16_t color) {
  if (!_ready) return;
  s_gfx->fillScreen(color);        // 둥근 테두리까지 같은 색 — 정사각만 칠하면 테두리에 옛 그림이 남는다
}

void PanelTFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (!_ready) return;
  s_gfx->fillRect(x + OX, y + OY, w, h, color);
}

void PanelTFT::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (!_ready) return;
  s_gfx->drawRect(x + OX, y + OY, w, h, color);
}

void PanelTFT::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
  if (!_ready) return;
  s_gfx->fillRoundRect(x + OX, y + OY, w, h, r, color);
}

void PanelTFT::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint16_t color) {
  if (!_ready) return;
  s_gfx->fillTriangle(x0 + OX, y0 + OY, x1 + OX, y1 + OY, x2 + OX, y2 + OY, color);
}

// 원본은 setSwapBytes(true) 로 쓴다 — uint16 값 그대로가 그릴 색이다. 프레임버퍼도 같다.
void PanelTFT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!_ready) return;
  s_gfx->draw16bitRGBBitmap(x + OX, y + OY, (uint16_t*)data, w, h);
}

void PanelTFT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                         const uint16_t* data, uint16_t transparent) {
  if (!_ready) return;
  s_gfx->draw16bitRGBBitmapWithTranColor(x + OX, y + OY, (uint16_t*)data, transparent, w, h);
}

// ── 글자 ──────────────────────────────────────────────────────────
VlwFont* PanelTFT::slotFor(const uint8_t* vlw) {
  if (!vlw) return nullptr;
  for (uint8_t i = 0; i < FONT_SLOTS; i++)
    if (_slotKey[i] == vlw && _slot[i].loaded()) return &_slot[i];

  uint8_t pick = FONT_SLOTS;
  for (uint8_t i = 0; i < FONT_SLOTS; i++) if (!_slotKey[i]) { pick = i; break; }
  if (pick == FONT_SLOTS) { pick = _slotNext; _slotNext = (_slotNext + 1) % FONT_SLOTS; }

  if (!_slot[pick].load(vlw)) { _slotKey[pick] = nullptr; return nullptr; }
  _slotKey[pick] = vlw;
  return &_slot[pick];
}

void PanelTFT::loadFont(const uint8_t* vlw) { _cur = slotFor(vlw); }
void PanelTFT::unloadFont() { _cur = nullptr; }

int16_t PanelTFT::textWidth(const char* s) {
  VlwFont* f = _cur ? _cur : slotFor(_basicFont);
  return (f && f->loaded()) ? f->textWidth(s) : 0;
}

int16_t PanelTFT::fontHeight() {
  VlwFont* f = _cur ? _cur : slotFor(_basicFont);
  return (f && f->loaded()) ? f->yAdvance() : 0;
}

// 알파(0-255)로 앞색과 뒷색을 섞는다. 뒷색은 프레임버퍼에 이미 있는 점이다.
static inline uint16_t blend565(uint8_t alpha, uint16_t fg, uint16_t bg) {
  if (alpha == 0xFF) return fg;
  if (alpha == 0x00) return bg;
  const uint8_t a = (alpha >> 3) + 1;          // 0-32
  const uint8_t fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  const uint8_t br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const uint8_t r = (fr * a + br * (32 - a)) >> 5;
  const uint8_t g = (fgc * a + bgc * (32 - a)) >> 5;
  const uint8_t b = (fb * a + bb * (32 - a)) >> 5;
  return (uint16_t)(r << 11) | (g << 5) | b;
}

void PanelTFT::drawString(const char* s, int32_t x, int32_t y, uint8_t builtinFont) {
  if (!_ready || !s || !*s) return;

  VlwFont* font = _cur;
  if (builtinFont >= 6)      font = slotFor(_num6Font);
  else if (builtinFont >= 4) font = slotFor(_num4Font);
  if (!font) font = slotFor(_basicFont);
  if (!font || !font->loaded()) return;
  const VlwFont& f = *font;

  const int16_t tw = f.textWidth(s);
  const int16_t th = f.yAdvance();

  // TFT_eSPI 의 datum 규칙과 같게
  int32_t px = x, py = y;
  switch (_datum) {
    case TC_DATUM: px -= tw / 2; break;
    case TR_DATUM: px -= tw;     break;
    case ML_DATUM:               py -= th / 2; break;
    case MC_DATUM: px -= tw / 2; py -= th / 2; break;
    case MR_DATUM: px -= tw;     py -= th / 2; break;
    case BL_DATUM:               py -= th;     break;
    case BC_DATUM: px -= tw / 2; py -= th;     break;
    case BR_DATUM: px -= tw;     py -= th;     break;
    default: break;                            // TL_DATUM
  }

  if (_fillBg) fillRect(px, py, tw, th, _bg);  // 이전 글자를 지운다(원본과 같은 규칙)

  uint16_t* buf = fb();
  if (!buf) return;

  int32_t pen = px;
  bool    first = true;
  const char* p = s;
  while (*p) {
    uint16_t cp;
    p = VlwFont::nextCodepoint(p, cp);
    if (!cp) continue;
    if (cp == 0x20) { pen += f.spaceWidth(); first = false; continue; }

    VlwFont::Glyph g;
    if (!f.glyph(cp, g)) { pen += f.spaceWidth() + 1; first = false; continue; }
    if (first && g.dX < 0) pen -= g.dX;
    first = false;

    const int32_t gx = pen + g.dX + OX;        // 프레임버퍼는 480×480 이라 여기서 옮긴다
    const int32_t gy = py + f.maxAscent() - g.dY + OY;

    for (int32_t row = 0; row < g.h; row++) {
      const int32_t dy = gy + row;
      if (dy < 0 || dy >= TR_PANEL_H) continue;
      const uint8_t* src = g.bitmap + (size_t)row * g.w;
      uint16_t*      dst = buf + (size_t)dy * TR_PANEL_W;
      for (int32_t col = 0; col < g.w; col++) {
        const int32_t dx = gx + col;
        if (dx < 0 || dx >= TR_PANEL_W) continue;
        const uint8_t a = src[col];
        if (!a) continue;
        dst[dx] = blend565(a, _fg, dst[dx]);
      }
    }
    pen += g.xAdvance;
  }

  // 캐시를 PSRAM 으로 밀어 낸다.
  //
  // 화면(LCD)은 프레임버퍼를 PSRAM 에서 **직접** 읽어 간다 — CPU 캐시를 거치지 않는다.
  // Arduino_GFX 는 제가 그릴 때마다 그 구역을 밀어 주지만(Cache_WriteBack_Addr), 글자는
  // 우리가 버퍼에 바로 쓰므로 그 일을 하지 않는다. 그대로 두면 글자 픽셀의 일부가
  // 캐시에만 남아 화면에서 깨져 보인다(실기에서 '건너뛰기' 가 뭉개져 나왔다).
  // flush(true) 가 프레임버퍼 전체를 한 번에 밀어 준다 — 캐시 줄 단위 훑기라 값이 싸다.
  s_gfx->flush(true);
}

// 원본은 ILI9341 의 슬립 인(0x10)을 보내 화면을 재운다. RGB 패널에는 그런 명령이 없어
// 백라이트를 내린다 — 눈에 보이는 결과는 같다(딥슬립 직전에만 부른다).
void PanelTFT::writecommand(uint8_t cmd) {
  if (!_ready) return;
  if (cmd == 0x10) setBacklight(0);
}

// ── 터치 ──────────────────────────────────────────────────────────
// CST820 은 CST816 계열과 같은 자리에 좌표를 둔다.
//   0x02 짚은 손가락 수 · 0x03 X 상위(하위 4비트) · 0x04 X 하위 · 0x05 Y 상위 · 0x06 Y 하위
// 인터럽트 신호가 이어지지 않는 칩이라(LILYGO 주석) INT 를 보지 않고 늘 읽는다.
bool PanelTFT::getTouch(uint16_t* x, uint16_t* y) {
  if (!_ready) return false;

  Wire.beginTransmission(TR_TOUCH_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)TR_TOUCH_ADDR, 5) != 5) return false;

  const uint8_t fingers = Wire.read();
  const uint8_t xh = Wire.read(), xl = Wire.read();
  const uint8_t yh = Wire.read(), yl = Wire.read();
  if (fingers == 0 || fingers > 1) return false;

  int32_t tx = ((xh & 0x0F) << 8) | xl;
  int32_t ty = ((yh & 0x0F) << 8) | yl;
  if (tx >= TR_PANEL_W || ty >= TR_PANEL_H) return false;

  // 화면 좌표(480 기준) → 본체가 보는 정사각 좌표. 정사각 밖(둥근 테두리)은 누른 것으로 치지 않는다.
  tx -= OX;
  ty -= OY;
  if (tx < 0 || ty < 0 || tx >= TR_UI_W || ty >= TR_UI_H) return false;

  if (x) *x = (uint16_t)tx;
  if (y) *y = (uint16_t)ty;
  return true;
}
