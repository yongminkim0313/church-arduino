// TalentNfcReader — ESP32-S3 NFC 키링 리더 (달란트 시스템)
//
// NTAG-213 키링을 PN532(I2C)로 읽어 2.8" ILI9341 TFT 에 잔액을 보여준다.
// 평소에는 딥슬립으로 대기하다가 터치 패드로 깨어나고, 무입력 15초 뒤 다시 잠든다.
//
// ── 조작 ──────────────────────────────────────────────────────────
//   터치(GPIO4) 짧게  : 잠든 상태면 깨우기 / 깨어 있으면 적립↔소모 모드 전환
//   키링 태깅         : 현재 모드대로 적립 또는 소모하고 결과를 표시
//   15초 무입력       : 전원 OFF 음 → 백라이트 끄고 딥슬립
//
// ── 빌드 (TFT_eSPI 설정을 이 스케치에만 적용) ──────────────────────
// TFT_eSPI 는 라이브러리 전역 설정(User_Setup_Select.h)을 쓰기 때문에, 그대로 두면
// 같은 PC 의 TTGO T-Display 스케치들과 설정이 충돌한다. 그래서 라이브러리를 건드리지
// 않고 빌드 플래그로 이 스케치에만 설정을 주입한다. build.sh 를 쓰면 된다.
//
//   ./TalentNfcReader/build.sh              # 컴파일
//   ./TalentNfcReader/build.sh --upload     # 컴파일 + 업로드
//
// 필요 라이브러리: TFT_eSPI, Adafruit PN532 (+ Adafruit BusIO)

#include <TFT_eSPI.h>
#include "FontKR22.h"
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Preferences.h>
#include <esp_sleep.h>

// ── 핀 (핀맵은 여기 한 곳에서만 관리한다) ─────────────────────────
// 디스플레이 SPI 핀은 TFT_eSPI 가 빌드 플래그로 받는다(build.sh 참고).
// 여기서는 라이브러리가 직접 다루지 않는 핀만 정의한다.
#define PIN_TFT_BL    14    // 백라이트 (PWM)
#define PIN_NFC_SDA   16
#define PIN_NFC_SCL   17
#define PIN_TOUCH      4    // 전원 버튼 (T4)
#define PIN_BUZZER     5    // 패시브 부저

// ── 동작 상수 ─────────────────────────────────────────────────────
#define SLEEP_TIMEOUT_MS  15000   // 무입력 후 딥슬립까지
#define TOUCH_DEBOUNCE_MS   400   // 터치 연속 인식 방지
#define TAG_COOLDOWN_MS    1500   // 같은 태그 연속 처리 방지

// 터치 임계값. ESP32-S3 는 터치하면 값이 "커진다"(구형 ESP32 와 반대).
// 보드·패드마다 다르므로 CALIBRATE_TOUCH 로 실측한 뒤 중간값을 넣을 것.
#define TOUCH_THRESH  40000

// 1 로 두면 터치 원시값만 시리얼로 찍는다. 임계값을 정할 때 쓴다.
#define CALIBRATE_TOUCH 0

// 한 번 태깅에 오르내리는 달란트
#define TALENT_STEP 1

// ── 달란트 저장 위치 ──────────────────────────────────────────────
// CLAUDE.md 의 미결정 항목. 지금은 "리더 측 저장(옵션 B)"으로 두었다.
//   · 태그(옵션 A)는 누구나 쓰기 가능해 위변조가 쉽다
//   · 리더 측이면 추가 부품 없이 ESP32 내장 플래시(NVS)만으로 된다
// 태그 저장으로 바꾸려면 talentLoad/talentSave 두 함수만 갈아끼우면 된다.
Preferences prefs;

TFT_eSPI tft = TFT_eSPI();

// TFT_eSPI 내장 폰트는 ASCII 전용이라 한글이 깨진다. 화면에 쓰는 낱말만 골라
// 스무스폰트(VLW)로 구워 넣었다(tools/ttf2vlw.py). 숫자는 큼직하게 보여야 해서
// 내장 폰트를 쓰는데, 스무스폰트가 올라가 있으면 폰트 번호가 무시되므로
// 그 구간만 잠깐 내렸다가 다시 올린다.
static void krFont(bool on) { if (on) tft.loadFont(FontKR22); else tft.unloadFont(); }
// PN532 는 I2C 로만 쓴다. 첫 두 인자는 SDA/SCL 이 아니라 IRQ/RESET 이며,
// 이 회로에서는 둘 다 연결하지 않으므로 -1 을 준다.
// (-1 이면 라이브러리가 reset 펄스를 건너뛰고, 준비 여부는 I2C RDY 바이트로 확인한다)
// SDA/SCL 은 아래 Wire.begin(SDA, SCL) 로 지정한다.
Adafruit_PN532 nfc(-1, -1, &Wire);

// ── 상태 ──────────────────────────────────────────────────────────
enum Mode { MODE_EARN, MODE_SPEND };
static Mode     mode         = MODE_EARN;
static uint32_t lastActivity = 0;
static uint32_t lastTouchMs  = 0;
static uint32_t lastTagMs    = 0;
static char     lastUid[24]  = "";
static bool     nfcReady     = false;

// ══════════════════════════════════════════════════════════════════
//  소리 — 방향으로 뜻을 구분한다 (적립은 올라가고, 소모는 내려간다)
// ══════════════════════════════════════════════════════════════════
static void beep(uint16_t freq, uint16_t ms) {
  tone(PIN_BUZZER, freq, ms);
  delay(ms);            // 재생이 끝날 때까지 붙잡는다(딥슬립 직전에도 잘리지 않게)
  noTone(PIN_BUZZER);
}

static void sndPowerOn()  { beep(1000, 90); beep(1500, 120); }              // 상승음
static void sndPowerOff() { beep(1500, 90); beep(1000, 140); }              // 하강음
static void sndEarn()     { beep(1200, 70); beep(1600, 70); beep(2000, 110); } // 밝은 3연음
static void sndSpend()    { beep(1600, 90); beep(900, 160); }               // 떨어지는 음
static void sndFail()     { beep(300, 300); }                               // 낮은 긴 음
static void sndMode()     { beep(1400, 60); }                               // 모드 전환 짧은 음

// ══════════════════════════════════════════════════════════════════
//  달란트 저장 (리더 측 · NVS)
// ══════════════════════════════════════════════════════════════════
// NVS 키는 15자 제한이라 UID 문자열(최대 14자)을 그대로 쓴다.
static int32_t talentLoad(const char* uid)             { return prefs.getInt(uid, 0); }
static void    talentSave(const char* uid, int32_t v)  { prefs.putInt(uid, v); }

// ══════════════════════════════════════════════════════════════════
//  화면
// ══════════════════════════════════════════════════════════════════
static void backlight(bool on) {
  // PWM 으로 서서히 켜면 눈이 편하다. 끌 때는 바로 끈다.
  if (on) { for (int d = 0; d <= 255; d += 15) { analogWrite(PIN_TFT_BL, d); delay(6); } }
  else    { analogWrite(PIN_TFT_BL, 0); }
}

static void drawHeader() {
  tft.fillRect(0, 0, tft.width(), 52, TFT_NAVY);
  krFont(true);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("달란트", 14, 26);

  // 현재 모드를 오른쪽에 배지로
  const char* label = (mode == MODE_EARN) ? "적립" : "소모";
  uint16_t col = (mode == MODE_EARN) ? TFT_GREEN : TFT_ORANGE;
  int w = 76;
  tft.fillRoundRect(tft.width() - w - 12, 12, w, 28, 14, col);
  tft.setTextColor(TFT_BLACK, col);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, tft.width() - w / 2 - 12, 26);
  krFont(false);
}

static void drawIdle() {
  tft.fillRect(0, 52, tft.width(), tft.height() - 52, TFT_BLACK);
  krFont(true);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("키링을 대주세요", tft.width() / 2, 150);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("터치 = 모드 전환", tft.width() / 2, 190);
  krFont(false);
}

// 태깅 결과. delta 0 이면 실패로 본다.
static void drawResult(const char* uid, int32_t balance, int32_t delta) {
  tft.fillRect(0, 52, tft.width(), tft.height() - 52, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  if (delta == 0) {
    krFont(true);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("처리할 수 없습니다", tft.width() / 2, 150);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("잔액이 모자랍니다", tft.width() / 2, 186);
    krFont(false);
    return;
  }

  // 증감은 크게 — 숫자뿐이라 내장 폰트(6번)로 그린다
  tft.setTextColor(delta > 0 ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  char d[16];
  snprintf(d, sizeof(d), "%+ld", (long)delta);
  tft.drawString(d, tft.width() / 2, 108, 6);

  krFont(true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char b[24];
  snprintf(b, sizeof(b), "잔액 %ld", (long)balance);
  tft.drawString(b, tft.width() / 2, 178);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(uid, tft.width() / 2, 220);
  krFont(false);
}

// ══════════════════════════════════════════════════════════════════
//  딥슬립 — 진입 경로는 이 함수 하나로 통일한다
// ══════════════════════════════════════════════════════════════════
static void goToDeepSleep() {
  sndPowerOff();          // beep() 안에서 재생 시간을 기다리므로 잘리지 않는다
  backlight(false);
  tft.writecommand(0x10); // ILI9341 sleep in

  // 터치로만 깨운다. ESP32-S3 는 임계값 "이상"일 때 깨어난다.
  touchSleepWakeUpEnable(PIN_TOUCH, TOUCH_THRESH);
  esp_sleep_enable_touchpad_wakeup();

  Serial.println("딥슬립 진입");
  Serial.flush();
  esp_deep_sleep_start();
}

// ══════════════════════════════════════════════════════════════════
//  태그 처리
// ══════════════════════════════════════════════════════════════════
static void uidToStr(const uint8_t* uid, uint8_t len, char* out, size_t cap) {
  size_t n = 0;
  for (uint8_t i = 0; i < len && n + 2 < cap; i++) n += snprintf(out + n, cap - n, "%02X", uid[i]);
  out[n] = '\0';
}

static void handleTag(const uint8_t* uid, uint8_t len) {
  char s[24];
  uidToStr(uid, len, s, sizeof(s));

  // 같은 태그가 붙어 있는 동안 계속 처리되지 않게 잠시 막는다
  uint32_t now = millis();
  if (!strcmp(s, lastUid) && now - lastTagMs < TAG_COOLDOWN_MS) return;
  strncpy(lastUid, s, sizeof(lastUid) - 1);
  lastTagMs = now;

  int32_t bal = talentLoad(s);

  if (mode == MODE_EARN) {
    bal += TALENT_STEP;
    talentSave(s, bal);
    sndEarn();
    drawResult(s, bal, +TALENT_STEP);
    Serial.printf("적립 %s → %ld\n", s, (long)bal);
  } else if (bal >= TALENT_STEP) {
    bal -= TALENT_STEP;
    talentSave(s, bal);
    sndSpend();
    drawResult(s, bal, -TALENT_STEP);
    Serial.printf("소모 %s → %ld\n", s, (long)bal);
  } else {
    sndFail();                       // 잔액 부족
    drawResult(s, bal, 0);
    Serial.printf("잔액 부족 %s (%ld)\n", s, (long)bal);
  }

  lastActivity = millis();
}

// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== TalentNfcReader 시작 ===");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_TFT_BL, OUTPUT);
  analogWrite(PIN_TFT_BL, 0);

#if CALIBRATE_TOUCH
  Serial.println("[보정] touchRead(T4) 값을 찍습니다. 손을 뗐을 때와 댔을 때의 중간값을 TOUCH_THRESH 로.");
  return;
#endif

  tft.init();
  tft.setRotation(0);          // 240x320 세로
  tft.fillScreen(TFT_BLACK);

  prefs.begin("talent", false);

  // 깨어난 이유를 남겨 두면 현장 디버깅이 쉽다
  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  Serial.printf("기상 원인: %d %s\n", (int)why,
                why == ESP_SLEEP_WAKEUP_TOUCHPAD ? "(터치)" : "(전원/리셋)");

  sndPowerOn();
  backlight(true);

  Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  nfcReady = (ver != 0);
  if (nfcReady) {
    nfc.SAMConfig();
    Serial.printf("PN532 준비됨 (v%d.%d)\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
  } else {
    Serial.println("PN532 를 찾지 못했습니다 — 배선(SDA=16, SCL=17)과 I2C 모드 스위치를 확인하세요.");
  }

  drawHeader();
  if (nfcReady) {
    drawIdle();
  } else {
    krFont(true);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("NFC 모듈 없음", tft.width() / 2, 150);
    krFont(false);
    sndFail();
  }

  lastActivity = millis();
}

void loop() {
#if CALIBRATE_TOUCH
  Serial.println(touchRead(PIN_TOUCH));
  delay(200);
  return;
#endif

  // ── 터치: 모드 전환 ──
  // ESP32-S3 는 터치 시 값이 커진다. 임계값 이상이면 눌린 것으로 본다.
  if (touchRead(PIN_TOUCH) > TOUCH_THRESH && millis() - lastTouchMs > TOUCH_DEBOUNCE_MS) {
    lastTouchMs = millis();
    mode = (mode == MODE_EARN) ? MODE_SPEND : MODE_EARN;
    sndMode();
    drawHeader();
    drawIdle();
    lastActivity = millis();
  }

  // ── 태깅 ──
  if (nfcReady) {
    uint8_t uid[7] = {0};
    uint8_t uidLen = 0;
    // 논블로킹에 가깝게 짧은 타임아웃으로 훑는다(터치 반응이 굼떠지지 않게)
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 80)) {
      handleTag(uid, uidLen);
    }
  }

  // ── 무입력이면 잠든다 ──
  if (millis() - lastActivity > SLEEP_TIMEOUT_MS) goToDeepSleep();

  delay(10);
}
