#include "PanelTFT.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>

PanelTFT tft;

static Arduino_DataBus* s_bus    = nullptr;
static Arduino_GFX*     s_panel  = nullptr;
static Arduino_Canvas*  s_canvas = nullptr;

// ── QSPI 버스 자물쇠 ──────────────────────────────────────────────
// 버퍼로 미는 일(flush)과 패널 명령(displayOff/On)은 **같은 QSPI 버스**를 쓰는데,
// 미는 쪽은 코어 0 의 태스크고 나머지는 본체(코어 1)다. 둘이 겹치면 ESP-IDF 가
// 그 자리에서 죽는다 — 형제 스케치 GodlifeScheduleNext_JC3248 을 실기에 구웠을 때
// 켜자마자 부팅 루프로 드러났다(그쪽은 setup 에서 flushNow 를 부른다):
//   E spi_master: Cannot send polling transaction while the previous ... not terminated
//   assert failed: spi_device_polling_end spi_master.c:1476 (host->cur_cs == handle->id)
// 이 스케치에서는 아직 터지지 않았다 — flushNow() 를 쓰지 않아서다. 그러나 화면을
// 재우는 writecommand() 는 같은 자리에 있어, 딥슬립을 켠 기기에서 언제든 같은 일이 난다.
// 버스를 만지는 자리를 모두 이 자물쇠로 묶는다. 그리는 일(버퍼에 쓰기)은 메모리뿐이라 상관없다.
static SemaphoreHandle_t s_busLock = nullptr;

static inline void busLock()   { if (s_busLock) xSemaphoreTake(s_busLock, portMAX_DELAY); }
static inline void busUnlock() { if (s_busLock) xSemaphoreGive(s_busLock); }

uint16_t* PanelTFT::fb() const { return s_canvas ? s_canvas->getFramebuffer() : nullptr; }

// ── 미는 태스크 ───────────────────────────────────────────────────
// 그리는 쪽은 버퍼(PSRAM)에만 쓰고, 미는 것은 이 태스크뿐이다 — 버스를 함께 쓰는
// 나머지 자리(패널 명령)는 위의 자물쇠로 묶었다. 그리는 도중에 밀리면 그 프레임만 반쯤 그려진 채 나가는데,
// 33ms 뒤 다음 프레임에서 바로 메워진다 — 눈에 띄지 않는다.
void PanelTFT::flushTask(void* arg) {
  PanelTFT* self = (PanelTFT*)arg;
  for (;;) {
    if (self->_dirty) {
      self->_dirty = false;
      busLock();
      s_canvas->flush();
      busUnlock();
    }
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

void PanelTFT::init() {
  s_bus    = new Arduino_ESP32QSPI(JC_LCD_CS, JC_LCD_SCK, JC_LCD_D0, JC_LCD_D1, JC_LCD_D2, JC_LCD_D3);
  s_panel  = new Arduino_AXS15231B(s_bus, GFX_NOT_DEFINED /* RST 없음 */, 0, false, 320, 480);
  s_canvas = new Arduino_Canvas(320, 480, s_panel, 0, 0, 0);

  if (!s_canvas->begin()) {           // PSRAM 이 없으면 300KB 버퍼를 못 잡는다
    Serial.println("[화면] 시작 실패 — PSRAM(opi) 설정을 확인하세요");
    _ready = false;
    return;
  }
  _ready = true;
  s_canvas->fillScreen(TFT_BLACK);
  s_canvas->flush();

  // 백라이트는 본체가 analogWrite 로 밝기를 조절한다 — 여기서는 핀만 잡아 둔다.
  pinMode(JC_LCD_BL, OUTPUT);
  digitalWrite(JC_LCD_BL, LOW);

  Wire.begin(JC_TOUCH_SDA, JC_TOUCH_SCL);
  Wire.setClock(400000);
  pinMode(JC_TOUCH_INT, INPUT_PULLUP);   // 폴링으로 읽지만, 딥슬립 기상에 쓰려면 입력이라야 한다

  // 코어 0 — 본체(loop)는 코어 1 에서 돈다. 미는 동안 본체가 멈추지 않는다.
  // 자물쇠는 태스크를 띄우기 전에 만든다 — 먼저 띄우면 첫 flush 가 자물쇠 없이 돈다.
  s_busLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(flushTask, "gfxflush", 4096, this, 1, nullptr, 0);
}

void PanelTFT::setRotation(uint8_t r) {
  // AXS15231B 는 하드웨어 회전이 없고, 통짜 버퍼를 돌리려면 매 프레임 300KB 를
  // 다시 쓰는 셈이라 값이 비싸다. 이 기기는 세워 두고 쓰므로 세로만 지원한다.
  if (r != 0) Serial.printf("[화면] 회전 %u 는 이 보드에서 지원하지 않습니다 — 세로(0)로 둡니다\n", r);
}

void PanelTFT::fillScreen(uint16_t color) {
  if (!_ready) return;
  s_canvas->fillScreen(color);
  markDirty();
}

void PanelTFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (!_ready) return;
  s_canvas->fillRect(x, y, w, h, color);
  markDirty();
}

void PanelTFT::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (!_ready) return;
  s_canvas->drawRect(x, y, w, h, color);
  markDirty();
}

void PanelTFT::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
  if (!_ready) return;
  s_canvas->fillRoundRect(x, y, w, h, r, color);
  markDirty();
}

void PanelTFT::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint16_t color) {
  if (!_ready) return;
  s_canvas->fillTriangle(x0, y0, x1, y1, x2, y2, color);
  markDirty();
}

// 원본은 setSwapBytes(true) 로 쓴다 — 즉 uint16 값 그대로가 그릴 색이다.
// 우리 버퍼도 uint16 값을 그대로 담으므로 옮겨 담기만 하면 된다.
void PanelTFT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!_ready) return;
  s_canvas->draw16bitRGBBitmap(x, y, (uint16_t*)data, w, h);
  markDirty();
}

void PanelTFT::pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                         const uint16_t* data, uint16_t transparent) {
  if (!_ready) return;
  s_canvas->draw16bitRGBBitmapWithTranColor(x, y, (uint16_t*)data, transparent, w, h);
  markDirty();
}

// 이미 올려 둔 칸이 있으면 그것을, 없으면 빈 칸(없으면 가장 오래된 칸)에 새로 올린다.
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

// 원본은 내장 폰트로 돌아가려고 unloadFont 를 부른다. 우리는 내장 폰트가 없으므로
// '기본 한글 폰트' 로 돌아가는 뜻으로 쓴다 — 숫자는 drawString 의 폰트 번호가 정한다.
void PanelTFT::unloadFont() { _cur = nullptr; }

int16_t PanelTFT::textWidth(const char* s) {
  VlwFont* f = _cur ? _cur : slotFor(_basicFont);
  return (f && f->loaded()) ? f->textWidth(s) : 0;
}

int16_t PanelTFT::fontHeight() {
  VlwFont* f = _cur ? _cur : slotFor(_basicFont);
  return (f && f->loaded()) ? f->yAdvance() : 0;
}

// 알파(0-255)로 앞색과 뒷색을 섞는다. 뒷색은 버퍼에 이미 있는 점 — 그림 위에 쓴 글자도
// 테두리가 곱게 나온다.
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

  // 폰트 번호를 준 자리(원본의 내장 폰트 4·6번)는 숫자 폰트로 그린다.
  VlwFont* font = _cur;
  if (builtinFont >= 6)      font = slotFor(_num6Font);
  else if (builtinFont >= 4) font = slotFor(_num4Font);
  if (!font) font = slotFor(_basicFont);
  if (!font || !font->loaded()) return;
  const VlwFont& f = *font;

  const int16_t tw = f.textWidth(s);
  const int16_t th = f.yAdvance();

  // TFT_eSPI 의 datum 규칙과 같게 — 가로는 왼쪽/가운데/오른쪽, 세로는 위/가운데/아래.
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

  // 배경색을 함께 받았으면(setTextColor(fg,bg)) 글자 자리를 먼저 덮는다 —
  // 원본은 이걸로 이전 글자를 지운다.
  if (_fillBg) fillRect(px, py, tw, th, _bg);

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
    if (first && g.dX < 0) pen -= g.dX;         // textWidth 와 같은 보정
    first = false;

    const int32_t gx = pen + g.dX;
    const int32_t gy = py + f.maxAscent() - g.dY;

    for (int32_t row = 0; row < g.h; row++) {
      const int32_t dy = gy + row;
      if (dy < 0 || dy >= _h) continue;
      const uint8_t* src = g.bitmap + (size_t)row * g.w;
      uint16_t*      dst = buf + (size_t)dy * _w;
      for (int32_t col = 0; col < g.w; col++) {
        const int32_t dx = gx + col;
        if (dx < 0 || dx >= _w) continue;
        const uint8_t a = src[col];
        if (!a) continue;
        dst[dx] = blend565(a, _fg, dst[dx]);
      }
    }
    pen += g.xAdvance;
  }
  markDirty();
}

// 원본은 ILI9341 의 슬립 인(0x10)을 보내 화면을 재운다. 여기서는 패널을 끈다.
void PanelTFT::writecommand(uint8_t cmd) {
  if (!_ready) return;
  busLock();                                   // 미는 태스크와 같은 버스다
  if (cmd == 0x10)      s_panel->displayOff();
  else if (cmd == 0x11) s_panel->displayOn();
  busUnlock();
}

void PanelTFT::flushNow() {
  if (!_ready) return;
  _dirty = false;
  busLock();
  s_canvas->flush();
  busUnlock();
}

// ── 터치 ──────────────────────────────────────────────────────────
// AXS15231B 에 '좌표를 달라'는 11바이트 명령을 쓰고 8바이트를 읽는다.
// data[1] 이 짚은 손가락 수, data[2..5] 가 좌표다(12비트씩).
//
// ※ 이 칩은 **새 좌표가 없는 틈에 0 이 아니라 헛값을 준다**(손가락 수가 1 보다 크게 나온다).
// 실기에서 손가락을 대고 있는 동안에도 11~22ms 마다 '유효 좌표 ↔ 헛값' 이 번갈아 왔고,
// 떼고 나면 헛값만 이어졌다. 헛값을 '뗌' 으로 읽으면 한 번 누르는 사이에 누름이 수십 번
// 들어가, 헤더 띠 한 번 누름이 두 번 누름(사진 모두 받기)으로 세어졌다.
// 그래서 헛값은 '새 소식 없음' 으로 보고, 마지막 유효 좌표 뒤 TOUCH_HOLD_MS 동안은
// 누른 채로 친다. 누르는 동안 유효 좌표 간격이 최대 22ms 라 60ms 면 넉넉하고,
// 뗀 뒤에는 60ms 만에 풀린다.
static const uint32_t TOUCH_HOLD_MS = 60;

// 한 번 읽는다: 1 = 유효 좌표, 0 = 손가락 없음(칩이 분명히 0 이라고 함), -1 = 헛값·읽기 실패
static int8_t readTouchOnce(uint16_t& x, uint16_t& y, int16_t w, int16_t h) {
  static const uint8_t kRead[11] = { 0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 };
  uint8_t data[8] = { 0 };

  Wire.beginTransmission(JC_TOUCH_ADDR);
  Wire.write(kRead, sizeof(kRead));
  if (Wire.endTransmission() != 0) return -1;
  delayMicroseconds(200);
  if (Wire.requestFrom(JC_TOUCH_ADDR, (int)sizeof(data)) != (int)sizeof(data)) return -1;
  for (size_t i = 0; i < sizeof(data); i++) data[i] = Wire.read();

  if (data[1] == 0) return 0;
  if (data[1] > 1)  return -1;                            // 헛값 — 새 보고가 없는 틈

  const uint16_t tx = ((data[2] & 0x0F) << 8) | data[3];
  const uint16_t ty = ((data[4] & 0x0F) << 8) | data[5];
  if (tx == 273 && ty == 273) return -1;                  // 칩이 내는 헛값
  if (tx >= (uint16_t)w || ty >= (uint16_t)h) return -1;
  x = tx;
  y = ty;
  return 1;
}

bool PanelTFT::getTouch(uint16_t* x, uint16_t* y) {
  if (!_ready) return false;

  static uint16_t lastX = 0, lastY = 0;
  static uint32_t lastValidAt = 0;
  static bool     down = false;

  uint16_t tx, ty;
  const int8_t r = readTouchOnce(tx, ty, _w, _h);
  const uint32_t now = millis();

  if (r == 1) {                                   // 새 좌표
    lastX = tx; lastY = ty; lastValidAt = now; down = true;
  } else if (r == 0) {                            // 칩이 손가락 없다고 분명히 말함
    down = false;
  } else if (down && now - lastValidAt > TOUCH_HOLD_MS) {
    down = false;                                 // 헛값이 오래 이어짐 — 뗀 것이다
  }

  if (!down) return false;
  if (x) *x = lastX;
  if (y) *y = lastY;
  return true;
}
