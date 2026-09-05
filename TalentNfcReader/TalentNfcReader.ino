// TalentNfcReader — ESP32-S3 NFC 키링 리더 (달란트 시스템)
//
// NTAG-213 키링을 PN532(I2C)로 읽어 2.8" ILI9341 TFT 에 잔액을 보여준다.
// 평소에는 딥슬립으로 대기하다가 터치 패드로 깨어나고, 무입력 15초 뒤 다시 잠든다.
//
// 달란트 잔액은 youthvision.co.kr 의 yvServer(/api/talent)가 관리한다.
// (같은 API 가 jesusdream.kr 에도 있다. 접속 주소는 ChurchSecrets.h 의 TALENT_API_BASE 하나로 바꾼다)
// 리더는 저장하지 않고 매번 서버에 묻는다 — 리더가 여러 대여도 잔액이 하나로 유지된다.
//
// ── 조작 ──────────────────────────────────────────────────────────
//   터치(GPIO4) 짧게  : 잠든 상태면 깨우기 / 깨어 있으면 적립↔소모 선택 전환
//                       대기 화면 가운데에 두 칸이 뜨고 선택된 쪽만 색이 채워진다
//   키링 태깅         : 선택된 모드대로 적립 또는 소모하고 결과를 표시
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
#include "FontKR14.h"
#include "TalentTypes.h"
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ChurchSecrets.h>   // WiFi/서버 인증정보 (저장소 밖)
#include <Preferences.h>     // 설정 캐시(NVS)
#include <esp_sleep.h>

// ── 핀 (핀맵은 여기 한 곳에서만 관리한다) ─────────────────────────
// 디스플레이 SPI 핀은 TFT_eSPI 가 빌드 플래그로 받는다(build.sh 참고).
// 여기서는 라이브러리가 직접 다루지 않는 핀만 정의한다.
#define PIN_TFT_BL    14    // 백라이트 (PWM)
#define PIN_NFC_SDA   16
#define PIN_NFC_SCL   17
#define PIN_TOUCH      4    // 전원 버튼 (T4)
#define PIN_BUZZER     5    // 패시브 부저

// ── 동작 설정 ─────────────────────────────────────────────────────
// 아래 값들은 서버(GET /api/talent/config)에서 내려받아 덮어쓴다.
// 여기 기본값은 yvServer/talent/talentConfig.js 의 기본값과 같아야 한다 —
// 서버에 못 붙어도 리더가 그대로 동작하게 하는 것이 목적이다.
#define DEF_SLEEP_TIMEOUT_MS  15000   // 무입력 후 딥슬립까지
#define DEF_TOUCH_DEBOUNCE_MS   400   // 터치 연속 인식 방지
#define DEF_TAG_COOLDOWN_MS    1500   // 같은 태그 연속 처리 방지
#define DEF_ROTATION              0   // 거치 방향
#define DEF_BACKLIGHT           255   // 백라이트 밝기
#define DEF_SOUND              true
#define DEF_LABEL          "달란트"   // 화면 왼쪽 위 제목
#define DEF_CONFIG_TTL_SEC      300   // 깨어 있는 동안 설정 재조회 주기

// 터치 임계값. ESP32-S3 는 터치하면 값이 "커진다"(구형 ESP32 와 반대).
// 보드·패드마다 다르므로 CALIBRATE_TOUCH 로 실측한 뒤 중간값을 넣을 것.
#define TOUCH_THRESH  40000

// 1 로 두면 터치 원시값만 시리얼로 찍는다. 임계값을 정할 때 쓴다.
#define CALIBRATE_TOUCH 0

// 한 번 태깅에 오르내리는 달란트
#define DEF_TALENT_STEP 1

// ── 달란트 저장 위치 ──────────────────────────────────────────────
// 서버(yvServer /api/talent)가 단일 출처다. 리더는 캐시하지 않는다.
//   · 리더를 여러 대 놓아도 잔액이 하나로 모인다
//   · 적립/소모 내역이 서버에 남아 "왜 이 잔액인지" 설명할 수 있다
//   · 대신 네트워크가 끊기면 처리할 수 없다 — 이때는 화면과 소리로 실패를 알린다
#define DEF_HTTP_TIMEOUT_MS 6000

// ══════════════════════════════════════════════════════════════════
//  설정 — 서버에서 받아 NVS 에 캐시한다
// ══════════════════════════════════════════════════════════════════
// 딥슬립에서 깨어날 때마다 setup() 이 통째로 도는 기기라, 서버 응답을 기다렸다가
// 화면을 그리면 터치 후 반응이 눈에 띄게 늦어진다. 그래서 캐시로 먼저 그리고
// 설정 갱신은 그 뒤에 한다. 서버에 못 붙으면 캐시(없으면 기본값)로 계속 간다.
enum ModeLock : uint8_t { LOCK_BOTH = 0, LOCK_EARN = 1, LOCK_SPEND = 2 };

struct Config {
  int32_t  talentStep      = DEF_TALENT_STEP;
  bool     defaultEarn     = true;                  // defaultMode
  uint8_t  modeLock        = LOCK_BOTH;
  uint32_t sleepTimeoutMs  = DEF_SLEEP_TIMEOUT_MS;
  uint32_t tagCooldownMs   = DEF_TAG_COOLDOWN_MS;
  uint32_t touchDebounceMs = DEF_TOUCH_DEBOUNCE_MS;
  uint32_t httpTimeoutMs   = DEF_HTTP_TIMEOUT_MS;
  uint8_t  rotation        = DEF_ROTATION;
  uint8_t  backlight       = DEF_BACKLIGHT;
  bool     sound           = DEF_SOUND;
  uint32_t ttlSec          = DEF_CONFIG_TTL_SEC;
  char     label[40]       = DEF_LABEL;             // 한글은 UTF-8 이라 넉넉히
};

static Config     cfg;
static Preferences prefs;
static uint32_t   lastCfgFetch = 0;
static bool       cfgFromServer = false;

TFT_eSPI tft = TFT_eSPI();

// TFT_eSPI 내장 폰트는 ASCII 전용이라 한글이 깨진다. KS X 1001 상용 2350자를
// 14px 스무스폰트(VLW)로 구워 넣었다(tools/make-fonts.sh). 서버에서 내려주는
// deviceLabel 이 무엇이든 나오게 하려면 낱말 서브셋으로는 부족하다.
// 숫자는 큼직하게 보여야 해서
// 내장 폰트를 쓰는데, 스무스폰트가 올라가 있으면 폰트 번호가 무시되므로
// 그 구간만 잠깐 내렸다가 다시 올린다.
static void krFont(bool on) { if (on) tft.loadFont(FontKR14); else tft.unloadFont(); }
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

// 서버가 이미 범위를 조여서 주지만, 기기에서도 한 번 더 조인다.
// 응답이 어긋났을 때 화면이 꺼지거나(밝기 0) 너무 빨리 잠드는(슬립 1초) 상태로
// 빠지면 현장에서 손댈 방법이 없기 때문이다.
static void cfgClamp() {
  cfg.talentStep      = clampT<int32_t>(cfg.talentStep,           1,   1000);
  cfg.sleepTimeoutMs  = clampT<uint32_t>(cfg.sleepTimeoutMs,   5000, 600000);
  cfg.tagCooldownMs   = clampT<uint32_t>(cfg.tagCooldownMs,     200,  10000);
  cfg.touchDebounceMs = clampT<uint32_t>(cfg.touchDebounceMs,   100,   3000);
  cfg.httpTimeoutMs   = clampT<uint32_t>(cfg.httpTimeoutMs,    1000,  20000);
  cfg.rotation        = clampT<uint8_t>(cfg.rotation,             0,      3);
  cfg.backlight       = clampT<uint8_t>(cfg.backlight,           10,    255);
  cfg.ttlSec          = clampT<uint32_t>(cfg.ttlSec,             30,  86400);
  if (cfg.modeLock > LOCK_SPEND) cfg.modeLock = LOCK_BOTH;
  if (!cfg.label[0]) strlcpy(cfg.label, DEF_LABEL, sizeof(cfg.label));
}

// 잠금이 걸려 있으면 그 모드로, 아니면 기본 모드로 맞춘다.
static void cfgApplyMode() {
  if      (cfg.modeLock == LOCK_EARN)  mode = MODE_EARN;
  else if (cfg.modeLock == LOCK_SPEND) mode = MODE_SPEND;
  else                                 mode = cfg.defaultEarn ? MODE_EARN : MODE_SPEND;
}

// ── NVS 캐시 ──
// 블롭이 아니라 키별로 저장한다. 나중에 항목이 늘어도 예전 캐시를 그냥 쓸 수 있다.
static void cfgLoadCache() {
  if (!prefs.begin("talentcfg", true)) return;      // 아직 저장된 적 없음 → 기본값
  cfg.talentStep      = prefs.getInt ("step",   cfg.talentStep);
  cfg.defaultEarn     = prefs.getBool("dmode",  cfg.defaultEarn);
  cfg.modeLock        = prefs.getUChar("lock",  cfg.modeLock);
  cfg.sleepTimeoutMs  = prefs.getUInt("sleepMs", cfg.sleepTimeoutMs);
  cfg.tagCooldownMs   = prefs.getUInt("coolMs",  cfg.tagCooldownMs);
  cfg.touchDebounceMs = prefs.getUInt("debMs",   cfg.touchDebounceMs);
  cfg.httpTimeoutMs   = prefs.getUInt("httpMs",  cfg.httpTimeoutMs);
  cfg.rotation        = prefs.getUChar("rot",    cfg.rotation);
  cfg.backlight       = prefs.getUChar("bl",     cfg.backlight);
  cfg.sound           = prefs.getBool("snd",     cfg.sound);
  cfg.ttlSec          = prefs.getUInt("ttl",     cfg.ttlSec);
  prefs.getString("label", cfg.label, sizeof(cfg.label));
  prefs.end();
  cfgClamp();
}

static void cfgSaveCache() {
  if (!prefs.begin("talentcfg", false)) { Serial.println("[설정] NVS 열기 실패"); return; }
  prefs.putInt  ("step",    cfg.talentStep);
  prefs.putBool ("dmode",   cfg.defaultEarn);
  prefs.putUChar("lock",    cfg.modeLock);
  prefs.putUInt ("sleepMs", cfg.sleepTimeoutMs);
  prefs.putUInt ("coolMs",  cfg.tagCooldownMs);
  prefs.putUInt ("debMs",   cfg.touchDebounceMs);
  prefs.putUInt ("httpMs",  cfg.httpTimeoutMs);
  prefs.putUChar("rot",     cfg.rotation);
  prefs.putUChar("bl",      cfg.backlight);
  prefs.putBool ("snd",     cfg.sound);
  prefs.putUInt ("ttl",     cfg.ttlSec);
  prefs.putString("label",  cfg.label);
  prefs.end();
}

// ── 서버에서 설정 받기 ──
// GET /api/talent/config?device=<기기ID>   헤더: x-talent-key
// 실패하면 아무것도 바꾸지 않는다. 캐시(또는 기본값)가 그대로 유지된다.
static bool cfgFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();                 // 자체 서버라 인증서 검증 생략(다른 보드들과 동일)
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[192];
  snprintf(url, sizeof(url), "%s/config?device=%s", TALENT_API_BASE, TALENT_DEVICE_ID);
  if (!http.begin(client, url)) return false;
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);

  int code = http.GET();
  String payload = (code > 0) ? http.getString() : String();
  http.end();

  if (code != 200) { Serial.printf("[설정] 서버 응답 %d — 캐시를 씁니다\n", code); return false; }

  JsonDocument doc;
  if (deserializeJson(doc, payload) || !doc["success"].as<bool>()) {
    Serial.println("[설정] 응답을 해석하지 못했습니다 — 캐시를 씁니다");
    return false;
  }
  JsonObject c = doc["config"];
  if (c.isNull()) return false;

  cfg.talentStep      = c["talentStep"]      | cfg.talentStep;
  cfg.sleepTimeoutMs  = c["sleepTimeoutMs"]  | cfg.sleepTimeoutMs;
  cfg.tagCooldownMs   = c["tagCooldownMs"]   | cfg.tagCooldownMs;
  cfg.touchDebounceMs = c["touchDebounceMs"] | cfg.touchDebounceMs;
  cfg.httpTimeoutMs   = c["httpTimeoutMs"]   | cfg.httpTimeoutMs;
  cfg.rotation        = c["rotation"]        | cfg.rotation;
  cfg.backlight       = c["backlight"]       | cfg.backlight;
  cfg.sound           = c["sound"]           | cfg.sound;
  cfg.ttlSec          = c["configTtlSec"]    | cfg.ttlSec;

  const char* dm = c["defaultMode"] | "";
  if      (!strcmp(dm, "earn"))  cfg.defaultEarn = true;
  else if (!strcmp(dm, "spend")) cfg.defaultEarn = false;

  const char* ml = c["modeLock"] | "";
  if      (!strcmp(ml, "both"))  cfg.modeLock = LOCK_BOTH;
  else if (!strcmp(ml, "earn"))  cfg.modeLock = LOCK_EARN;
  else if (!strcmp(ml, "spend")) cfg.modeLock = LOCK_SPEND;

  const char* lb = c["deviceLabel"] | "";
  if (lb[0]) strlcpy(cfg.label, lb, sizeof(cfg.label));

  cfgClamp();
  cfgSaveCache();
  lastCfgFetch  = millis();
  cfgFromServer = true;

  Serial.printf("[설정] 서버 반영: step=%ld sleep=%lums 잠금=%u 밝기=%u 소리=%d 제목=%s\n",
                (long)cfg.talentStep, (unsigned long)cfg.sleepTimeoutMs,
                cfg.modeLock, cfg.backlight, cfg.sound, cfg.label);
  return true;
}

// ══════════════════════════════════════════════════════════════════
//  소리 — 방향으로 뜻을 구분한다 (적립은 올라가고, 소모는 내려간다)
// ══════════════════════════════════════════════════════════════════
static void beep(uint16_t freq, uint16_t ms) {
  if (!cfg.sound) return;              // 서버에서 무음으로 설정한 기기
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
//  서버 통신 (yvServer /api/talent)
// ══════════════════════════════════════════════════════════════════
// POST /earn 또는 /spend. path 는 "earn" | "spend".
static TalentResult talentPost(const char* path, const char* uid, int32_t amount) {
  TalentResult r;

  if (WiFi.status() != WL_CONNECTED) {
    snprintf(r.reason, sizeof(r.reason), "%s", "네트워크 없음");
    return r;
  }

  WiFiClientSecure client;
  client.setInsecure();          // 자체 서버라 인증서 검증은 생략(센서 보드들과 동일)
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[128];
  snprintf(url, sizeof(url), "%s/%s", TALENT_API_BASE, path);
  if (!http.begin(client, url)) {
    snprintf(r.reason, sizeof(r.reason), "%s", "연결 실패");
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);   // 서버의 TALENT_DEVICE_KEY 와 대조

  char body[192];
  snprintf(body, sizeof(body),
           "{\"uid\":\"%s\",\"amount\":%ld,\"device\":\"%s\"}",
           uid, (long)amount, TALENT_DEVICE_ID);

  int code = http.POST(body);
  String payload = (code > 0) ? http.getString() : String();
  http.end();

  JsonDocument doc;
  bool parsed = !payload.isEmpty() && !deserializeJson(doc, payload);

  if (code == 200 && parsed && doc["success"].as<bool>()) {
    r.ok      = true;
    r.balance = doc["balance"] | 0;
    r.delta   = doc["delta"]   | 0;
    return r;
  }

  if (code == 409) {             // 잔액 부족 — 서버가 현재 잔액도 함께 준다
    r.lowBalance = true;
    r.balance = parsed ? (doc["balance"] | 0) : 0;
    snprintf(r.reason, sizeof(r.reason), "%s", "잔액이 모자랍니다");
    return r;
  }
  if (code == 401) { snprintf(r.reason, sizeof(r.reason), "%s", "기기 인증 실패"); return r; }
  if (code <= 0)   { snprintf(r.reason, sizeof(r.reason), "%s", "서버 연결 실패"); return r; }

  snprintf(r.reason, sizeof(r.reason), "서버 오류 %d", code);
  return r;
}

// ══════════════════════════════════════════════════════════════════
//  화면
// ══════════════════════════════════════════════════════════════════
static void backlight(bool on) {
  // PWM 으로 서서히 켜면 눈이 편하다. 끌 때는 바로 끈다. 최대 밝기는 서버 설정을 따른다.
  if (on) {
    for (int d = 0; d < cfg.backlight; d += 15) { analogWrite(PIN_TFT_BL, d); delay(6); }
    analogWrite(PIN_TFT_BL, cfg.backlight);
  } else {
    analogWrite(PIN_TFT_BL, 0);
  }
}

static void drawHeader() {
  tft.fillRect(0, 0, tft.width(), 52, TFT_NAVY);
  krFont(true);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(cfg.label, 14, 26);

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

// ── 모드 선택 카드 ────────────────────────────────────────────────
// 입력이 터치 패드 하나뿐이라, 지금 어느 쪽이 선택돼 있는지가 한눈에 보여야 한다.
// 두 칸을 나란히 두고 선택된 쪽만 색을 채운다. 잘못된 모드로 태깅하는 사고를 막는 것이
// 목적이므로, 헤더의 작은 배지에만 의존하지 않고 화면 가운데에서 크게 보여준다.
#define CELL_H      88   // 카드 높이
#define CELL_GAP_Y  34   // 카드 아래 → 안내문
#define HINT_GAP_Y  32   // 안내문 → 힌트

// 카드 + 안내문 두 줄을 한 덩어리로 보고 본문 영역 세로 가운데에 놓는다.
// rotation 설정에 따라 세로/가로가 바뀌므로 높이를 매번 계산한다.
static int cellTop() {
  const int contentTop = 52;
  const int blockH = CELL_H + CELL_GAP_Y + HINT_GAP_Y;
  return contentTop + (tft.height() - contentTop - blockH) / 2;
}

static void drawModeCell(int x, int y, int w, int h, const char* name,
                         int32_t step, bool on, uint16_t accent) {
  const uint16_t bg     = on ? accent : tft.color565(26, 28, 34);
  const uint16_t fg     = on ? TFT_BLACK : TFT_DARKGREY;
  const uint16_t border = on ? TFT_WHITE : tft.color565(60, 64, 74);

  tft.fillRoundRect(x, y, w, h, 12, bg);
  tft.drawRoundRect(x, y, w, h, 12, border);
  if (on) tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, border);   // 선택된 쪽만 테두리 2px

  krFont(true);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(name, x + w / 2, y + h / 2 - 15);
  krFont(false);

  // 한 번에 오르내리는 양(talentStep)은 서버 설정이라 기기마다 다르다. 같이 보여준다.
  // 스무스폰트를 내려야 폰트 번호(4)가 먹는다. 6번은 '+' 글자가 없어 쓸 수 없다.
  char amt[12];
  snprintf(amt, sizeof(amt), "%+ld", (long)step);
  tft.setTextColor(fg, bg);
  tft.drawString(amt, x + w / 2, y + h / 2 + 18, 4);
}

// 카드 두 칸만 다시 칠한다. 모드를 바꿀 때 본문 전체를 지우면 눈에 띄게 깜빡인다.
static void drawModeCells() {
  const int pad = 14, gap = 12;
  const int y   = cellTop();

  // 서버가 모드를 잠근 기기(적립 전용/소모 전용)는 고를 수 없다 —
  // 두 칸을 보여주면 누르면 바뀔 것처럼 보이므로 한 칸만 크게 둔다.
  if (cfg.modeLock != LOCK_BOTH) {
    const bool earn = (mode == MODE_EARN);
    drawModeCell(pad, y, tft.width() - pad * 2, CELL_H,
                 earn ? "적립" : "소모",
                 earn ? cfg.talentStep : -cfg.talentStep,
                 true, earn ? TFT_GREEN : TFT_ORANGE);
    return;
  }

  const int w = (tft.width() - pad * 2 - gap) / 2;
  drawModeCell(pad,           y, w, CELL_H, "적립",  cfg.talentStep, mode == MODE_EARN,  TFT_GREEN);
  drawModeCell(pad + w + gap, y, w, CELL_H, "소모", -cfg.talentStep, mode == MODE_SPEND, TFT_ORANGE);
}

// 지금 화면이 선택 화면인지. 결과/오류 화면에서 터치하면 선택 화면으로 되돌려야 한다.
static bool idleShown = false;

static void drawIdle() {
  tft.fillRect(0, 52, tft.width(), tft.height() - 52, TFT_BLACK);
  drawModeCells();

  const int y = cellTop() + CELL_H;
  krFont(true);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("키링을 대주세요", tft.width() / 2, y + CELL_GAP_Y);
  // 모드가 잠긴 기기에서는 "터치하면 바뀝니다" 가 거짓말이 된다
  if (cfg.modeLock == LOCK_BOTH) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("터치하면 바뀝니다", tft.width() / 2, y + CELL_GAP_Y + HINT_GAP_Y);
  }
  krFont(false);
  idleShown = true;
}

// 서버에 묻는 동안 보여줄 화면. 무반응처럼 보이지 않게 한다.
static void drawWorking() {
  tft.fillRect(0, 52, tft.width(), tft.height() - 52, TFT_BLACK);
  krFont(true);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("확인 중", tft.width() / 2, 150);
  krFont(false);
  idleShown = false;
}

// 네트워크·서버 문제. 잔액은 건드리지 않았다는 뜻이다.
static void drawError(const char* reason) {
  tft.fillRect(0, 52, tft.width(), tft.height() - 52, TFT_BLACK);
  krFont(true);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("처리하지 못했습니다", tft.width() / 2, 145);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(reason, tft.width() / 2, 182);
  krFont(false);
  idleShown = false;
}

// 태깅 결과. delta 0 이면 잔액 부족으로 본다.
static void drawResult(const char* uid, int32_t balance, int32_t delta) {
  tft.fillRect(0, 52, tft.width(), tft.height() - 52, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  idleShown = false;

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
  // 라디오를 먼저 끈다. 켠 채로 잠들면 전류가 크게 새어 배터리가 빨리 준다.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

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
  if (!strcmp(s, lastUid) && now - lastTagMs < cfg.tagCooldownMs) return;
  strncpy(lastUid, s, sizeof(lastUid) - 1);
  lastTagMs = now;

  drawWorking();                       // 서버 왕복이 1초쯤 걸릴 수 있어 진행 표시

  TalentResult r = talentPost(mode == MODE_EARN ? "earn" : "spend", s, cfg.talentStep);

  if (r.ok) {
    (r.delta > 0 ? sndEarn : sndSpend)();
    drawResult(s, r.balance, r.delta);
    Serial.printf("%s %s → %ld\n", r.delta > 0 ? "적립" : "소모", s, (long)r.balance);
  } else if (r.lowBalance) {
    sndFail();
    drawResult(s, r.balance, 0);
    Serial.printf("잔액 부족 %s (%ld)\n", s, (long)r.balance);
  } else {
    sndFail();
    drawError(r.reason);
    Serial.printf("실패 %s — %s\n", s, r.reason);
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

  // 캐시된 설정을 먼저 읽는다. 서버 왕복을 기다리지 않고 바로 화면을 그리기 위함이다.
  cfgLoadCache();
  cfgApplyMode();

#if CALIBRATE_TOUCH
  Serial.println("[보정] touchRead(T4) 값을 찍습니다. 손을 뗐을 때와 댔을 때의 중간값을 TOUCH_THRESH 로.");
  return;
#endif

  tft.init();
  tft.setRotation(cfg.rotation);   // 거치 방향 — 서버 설정(기본 0 = 240x320 세로)
  tft.fillScreen(TFT_BLACK);

  // 깨어난 이유를 남겨 두면 현장 디버깅이 쉽다
  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  Serial.printf("기상 원인: %d %s\n", (int)why,
                why == ESP_SLEEP_WAKEUP_TOUCHPAD ? "(터치)" : "(전원/리셋)");

  sndPowerOn();
  backlight(true);

  // 잔액은 서버가 갖고 있으므로 깨어날 때마다 접속한다.
  // 접속에 몇 초가 걸릴 수 있어 화면에 알린다(멈춘 것처럼 보이지 않게).
  drawHeader();
  krFont(true);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("연결 중", tft.width() / 2, 150);
  krFont(false);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi 연결됨 %s\n", WiFi.localIP().toString().c_str());
    // 설정을 받아온다. 실패해도 캐시(또는 기본값)로 계속 간다.
    if (cfgFetch()) {
      cfgApplyMode();                    // 잠금/기본 모드가 바뀌었을 수 있다
      analogWrite(PIN_TFT_BL, cfg.backlight);  // 밝기만 바로 반영(다시 페이드하면 깜빡인다)
    }
  } else {
    // 연결 못 해도 계속 진행한다. 태깅할 때 실패 사유를 화면에 보여준다.
    Serial.println("WiFi 연결 실패 — 태깅 시 서버 요청이 실패합니다.");
  }
  Serial.printf("[설정] 출처=%s step=%ld 잠금=%u\n",
                cfgFromServer ? "서버" : "캐시/기본값", (long)cfg.talentStep, cfg.modeLock);

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
  if (touchRead(PIN_TOUCH) > TOUCH_THRESH && millis() - lastTouchMs > cfg.touchDebounceMs) {
    lastTouchMs = millis();
    // 서버에서 모드를 잠근 기기(적립 전용/소모 전용)는 전환하지 않는다.
    // 다만 터치는 "활동"으로 쳐서, 만지는 동안 잠들지는 않게 한다.
    if (cfg.modeLock == LOCK_BOTH) {
      mode = (mode == MODE_EARN) ? MODE_SPEND : MODE_EARN;
      sndMode();
      drawHeader();
      // 이미 선택 화면이면 카드만 다시 칠한다(본문을 통째로 지우면 깜빡인다).
      // 결과·오류 화면이었다면 선택 화면으로 되돌린다.
      // NFC 모듈이 없을 때는 본문을 덮지 않는다 — 태깅이 안 되는데
      // "키링을 대주세요" 가 뜨면 고장을 숨기는 셈이 된다.
      if (!nfcReady)      { /* "NFC 모듈 없음" 유지 */ }
      else if (idleShown) drawModeCells();
      else                drawIdle();
    }
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

  // ── 설정 주기 갱신 ──
  // 행사 중에 관리자가 값을 바꾸면 기기를 만지지 않고도 반영되게 한다.
  // 한 번에 1초 남짓 멈추므로 주기는 넉넉히(기본 5분) 잡는다.
  // rotation 은 화면을 다시 그려야 해서 여기서는 반영하지 않는다 — 다음 부팅에 적용된다.
  if (WiFi.status() == WL_CONNECTED && millis() - lastCfgFetch > cfg.ttlSec * 1000UL) {
    bool got = cfgFetch();
    lastCfgFetch = millis();             // 실패해도 매 루프 재시도하지 않게
    // talentStep·modeLock 이 바뀌면 카드 내용도 달라지므로 함께 다시 그린다
    if (got) { cfgApplyMode(); drawHeader(); if (idleShown) drawModeCells(); }
  }

  // ── 무입력이면 잠든다 ──
  if (millis() - lastActivity > cfg.sleepTimeoutMs) goToDeepSleep();

  delay(10);
}
