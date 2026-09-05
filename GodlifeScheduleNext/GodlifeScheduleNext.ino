// GodlifeScheduleNext — TTGO T-Display (ESP32)
// 하루동행(godlife) 다음 일정을 화면에 띄운다.
//
//   GET https://jesusdream.kr/api/godlife/schedule/next?days=30&limit=3
//
// 서버(jdServer godlife/GodlifeManager.js)가 Firebase RTDB 의 일정을 읽어
// { next: {...}, following: [{...}] } 로 돌려준다. 다음 일정 하나를 크게 보여주고,
// 이어지는 일정은 아래 띠에 이어 붙인다.
//
// ── 흐르는 글자(마퀴) ──────────────────────────────────────────────
// 240px 짜리 화면에 일정 제목·메모가 다 들어갈 리 없다. 그래서 줄마다
// 글 폭을 재서 칸을 넘치면 왼쪽으로 흘린다. 한 바퀴 돌면 잠깐 멈춰
// 앞부분을 읽을 시간을 준다. 넘치지 않는 줄은 흐르지 않는다.
//
// ── 한글과 메모리 ─────────────────────────────────────────────────
// TFT_eSPI 내장 폰트는 ASCII 전용이라 스무스폰트(VLW)를 쓴다.
//   FontKR16.h  — 나눔고딕볼드 16px, KS X 1001 상용 한글 2,350자 + ASCII (590KB)
//   FontNum30.h — 나눔고딕볼드 30px, 큰 시각·D-day 전용 17자 (6KB)
// ChurchDisplayRx 는 음절 전체(11,172자)를 쓰지만 여기서는 상용 2,350자로 줄였다.
// 글리프 메트릭이 RAM 을 먹는데(글리프당 12B), 전체를 올리면 135KB 라
// HTTPS 핸드셰이크(mbedTLS)가 쓸 힙이 남지 않는다. 2,350자면 29KB 로 끝난다.
// 대신 "뷁" 같은 상용 밖 음절은 네모로 그려진다 — 실제 일정 제목에는 거의 없다.
//
// ── 그리는 방식 ───────────────────────────────────────────────────
// 화면에 직접 쓰면 마퀴가 매 프레임 배경을 지웠다 그리느라 깜빡인다.
// 그래서 폰트를 tft 가 아니라 240×20 짜리 줄 스프라이트 하나에 올려 두고,
// 줄 단위로 그려서 한 번에 밀어 넣는다(폰트 사본도 하나로 끝난다).
// 줄 띠는 서로 겹치지 않게 잡았다 — 겹치면 옆 줄을 지운다.
//
// 필요 라이브러리: TFT_eSPI(Setup25) · ArduinoJson 7.x · ChurchSecrets
// 파티션: Huge APP(3MB) — sketch.yaml 에 박아뒀다.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <ChurchSecrets.h>

// ── 서버 ──────────────────────────────────────────────────────────
// ChurchSecrets.h 에 GODLIFE_API_BASE 를 두면 그쪽이 우선한다(개발 중엔 로컬 서버).
#ifndef GODLIFE_API_BASE
#define GODLIFE_API_BASE "https://jesusdream.kr/api/godlife"
#endif
#define SCHEDULE_DAYS    30     // 앞으로 며칠까지 볼지 (서버 최대 90)
#define SCHEDULE_LIMIT    3     // next 뒤에 이어 붙일 개수 (서버 최대 10)
#define REFRESH_MS   300000UL   // 5분마다 다시 불러온다
#define RETRY_MS      20000UL   // 실패했을 때 다시 시도하는 간격
#define HTTP_TIMEOUT   8000

// 자료형과 폰트는 설정 #define 뒤에 둔다. Arduino 전처리기가 만들어 넣는 함수
// 원형이 마지막 전처리 지시문 바로 아래에 놓이므로, 여기가 지나야 구조체를 안다.
#include "ScheduleTypes.h"
#include "FontKR16.h"
#include "FontNum30.h"

#define PIN_BL 4
#define BTN_TOP    35   // 입력 전용 핀(내부 풀업 없음, 보드에 외부 풀업 있음)
#define BTN_BOTTOM  0

static const int16_t SCR_W = 240;
static const int16_t SCR_H = 135;

// ── 줄 띠 배치 (겹치지 않게 — 겹치면 옆 줄을 지운다) ───────────────
static const int16_t ROW_H   = 20;
static const int16_t Y_TOP   = 0;    // 상태 띠
static const int16_t Y_BIG   = 20;   // 큰 시각 (숫자 폰트)
static const int16_t H_BIG   = 35;
static const int16_t Y_META  = 55;   // 날짜 / 남은 시간
static const int16_t Y_TITLE = 75;   // 제목 (마퀴)
static const int16_t Y_MEMO  = 95;   // 기관 · 준비물 · 메모 (마퀴)
static const int16_t Y_FOOT  = 115;  // 이어지는 일정 (마퀴)

TFT_eSPI    tft    = TFT_eSPI();
TFT_eSprite row    = TFT_eSprite(&tft);   // 한글 폰트가 상주하는 줄 스프라이트
TFT_eSprite bigSpr = TFT_eSprite(&tft);   // 큰 시각 전용(30px 숫자 폰트)

static bool rowOk = false, bigOk = false, fontOk = false;

// 글리프 2,452개 메트릭 ≈ 29KB. 여유가 없으면 올리지 않고 ASCII 로 버틴다.
static const uint32_t FONT_HEAP_NEED  = 90000;
static const uint32_t FONT_BLOCK_NEED = 32000;

static uint16_t COL_BG, COL_BAR, COL_CARD, COL_DIM, COL_TXT, COL_ACC, COL_OK, COL_ERR;

// 폰트가 못 올라갔을 때를 위해 고정 문구는 ASCII 짝을 같이 둔다.
static inline const char* T(const char* ko, const char* en) { return fontOk ? ko : en; }

// ══════════════════════════════════════════════════════════════════
//  일정
// ══════════════════════════════════════════════════════════════════
static Item    items[MAX_ITEMS];
static uint8_t itemCount = 0;
static uint32_t fetchedAt = 0;      // 마지막 성공 시각(millis)
static bool     haveData  = false;
static char     lastError[40] = "";

static Screen  screen   = SCR_MAIN;
static uint8_t cursor   = 0;        // MAIN 에서 보고 있는 일정 번호
static bool    dirtyAll = true;     // 화면이 바뀌면 전부 다시 그린다

// ══════════════════════════════════════════════════════════════════
//  마퀴 — 칸보다 긴 글을 왼쪽으로 흘린다
// ══════════════════════════════════════════════════════════════════
static const float    MARQ_SPEED = 34.0f;   // 초당 픽셀
static const int16_t  MARQ_GAP   = 44;      // 한 바퀴 사이 여백
static const uint32_t MARQ_HOLD  = 1400;    // 시작·한 바퀴마다 멈춰 있는 시간(ms)

static Marquee marq[M_COUNT];

static void resetMarquees() {
  for (uint8_t i = 0; i < M_COUNT; i++) marq[i] = Marquee{};
}

// UTF-8 경계를 지키며 복사한다. 멀티바이트 한 글자를 반토막 내면 네모로 그려진다.
static void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = 0;
  while (src[n] && n < cap - 1) n++;
  while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// 폰트에 없는 글자를 TFT_eSPI 는 네모로 그린다. 서버에서 오는 일정 제목에는 이모지가
// 섞여 들어온다(실제 응답에 "후문 하차❤️" 가 있었다). 그래서 우리 폰트에 구워 넣은
// 것만 남기고 나머지는 버린다. 한글 음절은 상용 2,350자 밖이면 여전히 네모다.
static bool inFont(uint32_t cp) {
  if (cp >= 0x20   && cp <= 0x7E)   return true;   // ASCII
  if (cp >= 0xAC00 && cp <= 0xD7A3) return true;   // 한글 음절
  switch (cp) {                                    // --chars 로 같이 구운 기호
    case 0x00B7: case 0x2014: case 0x2026:
    case 0x25B6: case 0x25C6: case 0x25CB: case 0x25CF: return true;
  }
  return false;
}

// 위 판정을 통과한 글자만 UTF-8 그대로 옮긴다.
static void copyUtf8Font(char* dst, size_t cap, const char* src) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)src;
  while (*p && o < cap - 1) {
    uint8_t  c   = *p;
    uint8_t  len = 0;
    uint32_t cp  = 0;
    if      (c < 0x80)           { cp = c;         len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F;  len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F;  len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07;  len = 4; }
    else { p++; continue; }                        // 깨진 선두 바이트는 버린다
    for (uint8_t i = 1; i < len; i++) {
      if ((p[i] & 0xC0) != 0x80) { len = 0; break; }
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    if (!len) { p++; continue; }
    if (inFont(cp) && o + len < cap) { memcpy(dst + o, p, len); o += len; }
    p += len;
  }
  while (o > 0 && dst[o - 1] == ' ') o--;          // 지우고 남은 꼬리 공백을 다듬는다
  dst[o] = '\0';
}

// 줄 하나를 그린다. 글이 바뀌었거나 흐르는 중일 때만 실제로 그린다(force 면 무조건).
static void drawRow(int16_t y, uint16_t bg, const Seg* segs, uint8_t n, bool force) {
  const uint32_t now = millis();
  bool needDraw = force;

  // ── 먼저 각 덩이의 마퀴 상태를 갱신하며 다시 그릴 이유가 있는지 본다 ──
  for (uint8_t i = 0; i < n; i++) {
    Marquee& m = marq[segs[i].slot];
    const char* txt = segs[i].text ? segs[i].text : "";

    // 잘라 담은 뒤에 비교해야 한다. 원본과 견주면 긴 글은 늘 "바뀐 글"이 되어
    // 매 프레임 처음으로 되돌아가고, 결국 흐르지 않는다.
    char cut[MARQ_TEXT_MAX];
    copyUtf8(cut, sizeof(cut), txt);

    if (strcmp(m.text, cut) != 0) {
      memcpy(m.text, cut, sizeof(cut));
      m.off       = 0;
      m.holdUntil = now + MARQ_HOLD;   // 흐르기 전에 앞부분을 잠깐 보여준다
      m.lastAdv   = now;
      int16_t tw  = rowOk ? row.textWidth(m.text) : tft.textWidth(m.text);
      int16_t aw  = segs[i].x1 - segs[i].x0;
      m.rolls     = (tw > aw);
      m.period    = tw + MARQ_GAP;
      needDraw    = true;
      continue;
    }
    if (!m.rolls) continue;
    if (now < m.holdUntil) { m.lastAdv = now; continue; }

    m.off += MARQ_SPEED * (now - m.lastAdv) / 1000.0f;
    m.lastAdv = now;
    if (m.off >= m.period) {           // 한 바퀴 — 처음으로 돌아가 잠깐 쉰다
      m.off -= m.period;
      m.holdUntil = now + MARQ_HOLD;
    }
    needDraw = true;
  }
  if (!needDraw) return;

  // ── 스프라이트를 못 만들었으면 화면에 직접 쓴다(마퀴 없이 잘려서 나온다) ──
  if (!rowOk) {
    tft.fillRect(0, y, SCR_W, ROW_H, bg);
    tft.setTextDatum(ML_DATUM);
    for (uint8_t i = 0; i < n; i++) {
      tft.setTextColor(segs[i].fg, bg);
      tft.setViewport(segs[i].x0, y, segs[i].x1 - segs[i].x0, ROW_H);
      tft.drawString(marq[segs[i].slot].text, 0, ROW_H / 2);
      tft.resetViewport();
    }
    return;
  }

  row.fillSprite(bg);
  row.setTextDatum(ML_DATUM);
  for (uint8_t i = 0; i < n; i++) {
    const Marquee& m = marq[segs[i].slot];
    if (!m.text[0]) continue;
    const int16_t aw = segs[i].x1 - segs[i].x0;

    // 칸 밖으로 나가는 글자는 뷰포트가 잘라 준다 — 옆 덩이를 침범하지 않는다.
    row.setViewport(segs[i].x0, 0, aw, ROW_H);
    row.setTextColor(segs[i].fg, bg);
    if (m.rolls) {
      row.drawString(m.text, -(int16_t)m.off, ROW_H / 2);
      // 한 바퀴 뒤를 이어 붙여 끊기지 않게 한다
      row.drawString(m.text, -(int16_t)m.off + m.period, ROW_H / 2);
    } else if (segs[i].align == AL_RIGHT) {
      row.drawString(m.text, aw - row.textWidth(m.text), ROW_H / 2);
    } else {
      row.drawString(m.text, 0, ROW_H / 2);
    }
    row.resetViewport();
  }
  row.pushSprite(0, y);
}

// ══════════════════════════════════════════════════════════════════
//  문구 만들기
// ══════════════════════════════════════════════════════════════════
// 받아온 뒤 흐른 시간을 빼서 지금 기준으로 남은 분을 구한다.
static long minutesLeft(const Item& it) {
  return it.minutesUntil - (long)((millis() - fetchedAt) / 60000UL);
}

static void fmtCountdown(const Item& it, char* out, size_t cap) {
  long m = minutesLeft(it);
  if (m < 0)        snprintf(out, cap, "%s", T("진행 중", "now"));
  else if (m == 0)  snprintf(out, cap, "%s", T("곧 시작", "soon"));
  else if (m < 60)  snprintf(out, cap, T("%ld분 뒤", "in %ldm"), m);
  else if (m < 1440) {
    if (m % 60) snprintf(out, cap, T("%ld시간 %ld분 뒤", "in %ldh %ldm"), m / 60, m % 60);
    else        snprintf(out, cap, T("%ld시간 뒤", "in %ldh"), m / 60);
  } else            snprintf(out, cap, T("%ld일 %ld시간 뒤", "in %ldd %ldh"), m / 1440, (m % 1440) / 60);
}

static void fmtDday(const Item& it, char* out, size_t cap) {
  if (it.dDay == 0)      snprintf(out, cap, "%s", T("오늘", "today"));
  else if (it.dDay == 1) snprintf(out, cap, "%s", T("내일", "tomorrow"));
  else if (it.dDay == 2) snprintf(out, cap, "%s", T("모레", "in 2 days"));
  else                   snprintf(out, cap, "D-%d", it.dDay);
}

// 큰 숫자 칸에 쓸 글. 시각이 있으면 "09:30", 종일이면 날짜 "10.07".
static void fmtBig(const Item& it, char* out, size_t cap) {
  if (it.allDay || it.time[0] < '0' || it.time[0] > '9')
    snprintf(out, cap, "%02d.%02d", it.month, it.day);
  else
    copyUtf8(out, cap, it.time);
}

// "10월 7일 (화)" — 칸(122px)에 들어가는 길이라 흐르지 않는다.
static void fmtWhen(const Item& it, char* out, size_t cap) {
  if (fontOk) snprintf(out, cap, "%d월 %d일 (%s)", it.month, it.day, it.weekday);
  else        snprintf(out, cap, "%d/%d", it.month, it.day);
}

// 기관 + 준비물 진행 + 메모를 한 줄로. 길면 이 줄이 흐른다.
static void fmtMemo(const Item& it, char* out, size_t cap) {
  char prep[32] = "";
  if (it.prepTotal > 0)
    snprintf(prep, sizeof(prep), T("준비물 %d/%d", "prep %d/%d"), it.prepDone, it.prepTotal);

  out[0] = '\0';
  size_t used = 0;
  const char* parts[3] = { it.label, prep, it.memo };
  for (uint8_t i = 0; i < 3; i++) {
    if (!parts[i][0]) continue;
    int wrote = snprintf(out + used, cap - used, "%s%s", used ? " · " : "", parts[i]);
    if (wrote < 0) break;
    used += (size_t)wrote;
    if (used >= cap - 1) break;
  }
}

// 한 줄 요약 — 이어지는 일정 띠와 목록 화면에서 쓴다.
static void fmtBrief(const Item& it, char* out, size_t cap) {
  char dd[16], wd[10] = "";
  fmtDday(it, dd, sizeof(dd));
  if (fontOk && it.weekday[0]) snprintf(wd, sizeof(wd), "(%s)", it.weekday);
  snprintf(out, cap, "%s %d/%d%s %s %s", dd, it.month, it.day, wd, it.time, it.title);
}

// ══════════════════════════════════════════════════════════════════
//  화면 그리기
// ══════════════════════════════════════════════════════════════════
static void drawStatusRow(bool force) {
  char left[48], right[48];

  if (screen == SCR_LIST)
    snprintf(left, sizeof(left), T("일정 %u개", "%u items"), itemCount);
  else if (cursor == 0)
    snprintf(left, sizeof(left), "%s", T("다음 일정", "NEXT"));
  else
    snprintf(left, sizeof(left), T("일정 %u/%u", "NEXT %u/%u"), cursor + 1, itemCount);

  uint16_t col = COL_DIM;
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(right, sizeof(right), "%s", T("무선 끊김", "no wifi"));
    col = COL_ERR;
  } else if (lastError[0]) {
    copyUtf8(right, sizeof(right), lastError);
    col = COL_ERR;
  } else if (!haveData) {
    snprintf(right, sizeof(right), "%s", T("불러오는 중", "loading"));
  } else {
    uint32_t sec = (millis() - fetchedAt) / 1000;
    if (sec < 60) snprintf(right, sizeof(right), T("%lu초 전 갱신", "%lus ago"), (unsigned long)sec);
    else          snprintf(right, sizeof(right), T("%lu분 전 갱신", "%lum ago"), (unsigned long)(sec / 60));
    col = COL_OK;
  }

  const Seg segs[] = {
    { M_TOP_L, left,  6, 118, AL_LEFT,  COL_TXT },
    { M_TOP_R, right, 120, 234, AL_RIGHT, col   },
  };
  drawRow(Y_TOP, COL_BAR, segs, 2, force);
}

// 큰 시각 칸. 왼쪽에 시각(또는 날짜), 오른쪽에 D-day 를 숫자 폰트로 쓴다.
static void drawBig(const Item* it, bool force) {
  static char lastBig[32] = "\x01";
  char big[16] = "", dd[12] = "";

  if (it) {
    fmtBig(*it, big, sizeof(big));
    if (it->dDay == 0) snprintf(dd, sizeof(dd), "D-DAY");
    else               snprintf(dd, sizeof(dd), "D-%d", it->dDay);
  }

  char key[32];
  snprintf(key, sizeof(key), "%s|%s", big, dd);
  if (!force && strcmp(key, lastBig) == 0) return;
  snprintf(lastBig, sizeof(lastBig), "%s", key);

  if (!bigOk) {                       // 숫자 스프라이트가 없으면 그 칸은 비워 둔다
    tft.fillRect(0, Y_BIG, SCR_W, H_BIG, COL_BG);
    return;
  }
  bigSpr.fillSprite(COL_BG);
  bigSpr.setTextDatum(ML_DATUM);
  bigSpr.setTextColor(COL_ACC, COL_BG);
  bigSpr.drawString(big, 6, H_BIG / 2);
  bigSpr.setTextDatum(MR_DATUM);
  bigSpr.setTextColor(COL_DIM, COL_BG);
  bigSpr.drawString(dd, SCR_W - 8, H_BIG / 2);
  bigSpr.pushSprite(0, Y_BIG);
}

static void renderMain(bool force) {
  drawStatusRow(force);

  if (!haveData || itemCount == 0) {
    drawBig(nullptr, force);
    const char* msg = haveData ? T("다가오는 일정이 없습니다", "no upcoming schedule")
                               : T("일정을 불러오는 중입니다", "loading schedule...");
    const Seg meta[] = { { M_META_L, "",  6, 234, AL_LEFT, COL_DIM } };
    const Seg body[] = { { M_TITLE,  msg, 6, 234, AL_LEFT, COL_TXT } };
    const Seg mm[]   = { { M_MEMO,   "",  6, 234, AL_LEFT, COL_DIM } };
    const Seg foot[] = { { M_FOOT,   lastError[0] ? lastError : "", 6, 234, AL_LEFT, COL_DIM } };
    drawRow(Y_META,  COL_BG,   meta, 1, force);
    drawRow(Y_TITLE, COL_CARD, body, 1, force);
    drawRow(Y_MEMO,  COL_CARD, mm,   1, force);    // 카드 아래쪽은 비워 둔다
    drawRow(Y_FOOT,  COL_BAR,  foot, 1, force);
    return;
  }

  const Item& it = items[cursor];
  drawBig(&it, force);

  char when[64], remain[48], memo[MARQ_TEXT_MAX], foot[MARQ_TEXT_MAX];
  fmtWhen(it, when, sizeof(when));
  fmtCountdown(it, remain, sizeof(remain));   // D-day 는 큰 숫자 칸에 있으니 여기선 남은 시간만
  fmtMemo(it, memo, sizeof(memo));

  // 이어지는 일정 — 지금 보고 있는 것 다음부터 이어 붙인다.
  foot[0] = '\0';
  size_t used = 0;
  for (uint8_t i = cursor + 1; i < itemCount; i++) {
    char one[MARQ_TEXT_MAX];
    fmtBrief(items[i], one, sizeof(one));
    int wrote = snprintf(foot + used, sizeof(foot) - used, "%s%s",
                         used ? "   ·   " : T("이어서 ▶ ", "next > "), one);
    if (wrote < 0) break;
    used += (size_t)wrote;
    if (used >= sizeof(foot) - 1) break;   // 잘렸다 — 더 붙일 자리가 없다
  }
  if (!foot[0]) snprintf(foot, sizeof(foot), "%s", T("이어지는 일정 없음", "nothing after this"));

  const Seg meta[] = {
    { M_META_L, when,   6, 128, AL_LEFT,  COL_TXT },
    { M_META_R, remain, 130, 234, AL_RIGHT, COL_ACC },
  };
  const Seg title[] = { { M_TITLE, it.title, 6, 234, AL_LEFT, COL_TXT } };
  const Seg mm[]    = { { M_MEMO,  memo,     6, 234, AL_LEFT, COL_DIM } };
  const Seg ft[]    = { { M_FOOT,  foot,     6, 234, AL_LEFT, COL_DIM } };

  drawRow(Y_META,  COL_BG,   meta,  2, force);
  drawRow(Y_TITLE, COL_CARD, title, 1, force);
  drawRow(Y_MEMO,  COL_CARD, mm,    1, force);
  drawRow(Y_FOOT,  COL_BAR,  ft,    1, force);
}

// 목록 화면 — 받아온 일정을 한 줄씩. 긴 줄은 여기서도 흐른다.
static void renderList(bool force) {
  drawStatusRow(force);

  // 위 상태 띠(20px) 아래로 26px 간격. 개수가 바뀌면 여기도 같이 손봐야 한다.
  static const int16_t ys[MAX_ITEMS] = { 26, 52, 78, 104 };
  for (uint8_t i = 0; i < MAX_ITEMS; i++) {
    char line[MARQ_TEXT_MAX] = "";
    if (i < itemCount) fmtBrief(items[i], line, sizeof(line));
    const Seg seg[] = {
      { (uint8_t)(M_LIST_0 + i), line, 6, 234, AL_LEFT,
        i == cursor ? COL_ACC : COL_TXT },
    };
    drawRow(ys[i], i % 2 ? COL_BG : COL_CARD, seg, 1, force);
  }
}

static void render() {
  bool force = dirtyAll;
  if (force) {
    tft.fillScreen(COL_BG);
    resetMarquees();
    dirtyAll = false;
  }
  if (screen == SCR_MAIN) renderMain(force);
  else                    renderList(force);
}

// ══════════════════════════════════════════════════════════════════
//  서버에서 일정 받아오기
// ══════════════════════════════════════════════════════════════════
// 쓰지 않는 필드까지 담으면 힙만 먹는다. 필요한 것만 걸러서 읽는다.
static const char FILTER_JSON[] =
  "{\"days\":true,\"next\":{\"title\":true,\"institutionLabel\":true,\"date\":true,\"weekday\":true,"
  "\"time\":true,\"isAllDay\":true,\"memo\":true,\"dDay\":true,\"minutesUntil\":true,"
  "\"preparations\":[{\"checked\":true}]},"
  "\"following\":[{\"title\":true,\"institutionLabel\":true,\"date\":true,\"weekday\":true,"
  "\"time\":true,\"isAllDay\":true,\"memo\":true,\"dDay\":true,\"minutesUntil\":true,"
  "\"preparations\":[{\"checked\":true}]}]}";

static void parseItem(JsonObjectConst o, Item& it) {
  it = Item{};
  // 서버가 준 글은 폰트에 있는 글자만 걸러서 담는다(이모지 → 네모 방지).
  copyUtf8Font(it.title,   sizeof(it.title),   o["title"]            | "");
  copyUtf8Font(it.label,   sizeof(it.label),   o["institutionLabel"] | "");
  copyUtf8Font(it.weekday, sizeof(it.weekday), o["weekday"]          | "");
  copyUtf8Font(it.time,    sizeof(it.time),    o["time"]             | "");
  copyUtf8Font(it.memo,    sizeof(it.memo),    o["memo"]             | "");
  it.allDay       = o["isAllDay"] | false;
  it.dDay         = o["dDay"] | 0;
  it.minutesUntil = o["minutesUntil"] | 0L;

  // "2026-09-07" 에서 월·일만 꺼낸다(연도는 화면에 안 쓴다).
  const char* d = o["date"] | "";
  if (strlen(d) >= 10) { it.month = atoi(d + 5); it.day = atoi(d + 8); }

  for (JsonObjectConst p : o["preparations"].as<JsonArrayConst>()) {
    it.prepTotal++;
    if (p["checked"] | false) it.prepDone++;
  }
  if (!it.title[0]) copyUtf8(it.title, sizeof(it.title), T("제목 없음", "(no title)"));
}

static bool fetchSchedule() {
  if (WiFi.status() != WL_CONNECTED) {
    copyUtf8(lastError, sizeof(lastError), T("무선 끊김", "no wifi"));
    return false;
  }

  char url[192];
  snprintf(url, sizeof(url), "%s/schedule/next?days=%d&limit=%d",
           GODLIFE_API_BASE, SCHEDULE_DAYS, SCHEDULE_LIMIT);
#ifdef GODLIFE_OWNER
  strncat(url, "&owner=", sizeof(url) - strlen(url) - 1);
  strncat(url, GODLIFE_OWNER, sizeof(url) - strlen(url) - 1);
#endif

  const bool tls = (strncmp(url, "https:", 6) == 0);
  WiFiClientSecure secure;
  WiFiClient       plain;
  secure.setInsecure();          // 자체 서버라 인증서 검증은 생략(다른 스케치들과 동일)

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT);
  http.setConnectTimeout(HTTP_TIMEOUT);
  http.useHTTP10(true);          // 스트림으로 바로 파싱하려면 청크 인코딩을 꺼야 한다

  // 라우터가 없는 서버는 200 으로 SPA 의 index.html 을 돌려준다. 그런데 필터를 건
  // deserializeJson 은 필터에 안 맞는 입력을 "건너뛰기"만 하고 오류를 내지 않아서,
  // HTML 을 먹여도 빈 문서 + 성공으로 끝난다(실기에서 "일정 0건" 으로 확인).
  // 그래서 파싱 전에 Content-Type 을 본다.
  static const char* WANTED[] = { "Content-Type" };
  http.collectHeaders(WANTED, 1);

  bool begun = tls ? http.begin(secure, url) : http.begin(plain, url);
  if (!begun) {
    copyUtf8(lastError, sizeof(lastError), T("연결 실패", "connect fail"));
    return false;
  }

  Serial.printf("[요청] %s  (여유 힙 %u B)\n", url, ESP.getFreeHeap());
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code <= 0) copyUtf8(lastError, sizeof(lastError), T("서버 연결 실패", "no server"));
    else           snprintf(lastError, sizeof(lastError), T("서버 오류 %d", "HTTP %d"), code);
    Serial.printf("[요청] 실패 code=%d\n", code);
    return false;
  }

  String ctype = http.header("Content-Type");
  if (ctype.indexOf("json") < 0) {
    http.end();
    copyUtf8(lastError, sizeof(lastError), T("JSON 이 아님", "not json"));
    Serial.printf("[요청] JSON 이 아니다: %s\n", ctype.c_str());
    return false;
  }

  JsonDocument filter;
  deserializeJson(filter, FILTER_JSON);
  JsonDocument doc;
  DeserializationError err =
    deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    copyUtf8(lastError, sizeof(lastError), T("응답 형식 오류", "bad json"));
    Serial.printf("[요청] JSON 파싱 실패: %s\n", err.c_str());
    return false;
  }

  if (!doc["days"].is<int>()) {   // 우리 API 의 응답 모양이 아니다
    copyUtf8(lastError, sizeof(lastError), T("응답 형식 오류", "bad json"));
    Serial.println("[요청] 우리 API 응답이 아니다");
    return false;
  }

  itemCount = 0;
  if (doc["next"].is<JsonObject>())
    parseItem(doc["next"].as<JsonObjectConst>(), items[itemCount++]);
  for (JsonObjectConst o : doc["following"].as<JsonArrayConst>()) {
    if (itemCount >= MAX_ITEMS) break;
    parseItem(o, items[itemCount++]);
  }

  fetchedAt   = millis();
  haveData    = true;
  lastError[0] = '\0';
  if (cursor >= itemCount) cursor = 0;

  Serial.printf("[요청] 일정 %u건  다음=%s  (여유 힙 %u B)\n",
                itemCount, itemCount ? items[0].title : "-", ESP.getFreeHeap());
  return true;
}

static uint32_t nextFetchAt = 0;   // 다음 조회 시각(millis)

// ── 버튼 ──────────────────────────────────────────────────────────
//   위(GPIO35)  → 다음 일정 → 그다음 … → 목록 → 처음으로
//   아래(GPIO0) → 지금 바로 새로고침
static bool wantFetch = false;

static void handleButtons() {
  static uint32_t lockUntil = 0;
  if (millis() < lockUntil) return;

  bool top = (digitalRead(BTN_TOP) == LOW);
  bool bot = (digitalRead(BTN_BOTTOM) == LOW);
  if (!top && !bot) return;
  lockUntil = millis() + 250;

  if (top) {
    if (screen == SCR_LIST)              { screen = SCR_MAIN; cursor = 0; }
    else if (cursor + 1 < itemCount)     { cursor++; }
    else                                 { screen = SCR_LIST; cursor = 0; }
    dirtyAll = true;
    Serial.printf("[버튼] 위 → %s %u\n", screen == SCR_LIST ? "목록" : "본화면", cursor);
    render();
  } else {
    wantFetch = true;
    copyUtf8(lastError, sizeof(lastError), "");
    Serial.println("[버튼] 아래 → 새로고침");
  }
}

// ══════════════════════════════════════════════════════════════════
static void initColors() {
  COL_BG   = tft.color565(10, 16, 32);
  COL_BAR  = tft.color565(22, 34, 62);
  COL_CARD = tft.color565(17, 26, 50);
  COL_DIM  = tft.color565(130, 150, 185);
  COL_TXT  = tft.color565(235, 240, 250);
  COL_ACC  = tft.color565(255, 178, 70);
  COL_OK   = tft.color565(80, 220, 130);
  COL_ERR  = tft.color565(255, 95, 95);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(BTN_TOP, INPUT);            // GPIO35 는 입력 전용 — 보드의 외부 풀업을 쓴다
  pinMode(BTN_BOTTOM, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  initColors();
  tft.fillScreen(COL_BG);

  // 스프라이트를 먼저 잡고(작다) 그다음 폰트를 올린다 — 순서를 바꾸면
  // 폰트가 힙을 조각내 스프라이트가 못 잡히는 수가 있다.
  row.setColorDepth(16);
  rowOk = (row.createSprite(SCR_W, ROW_H) != nullptr);
  bigSpr.setColorDepth(16);
  bigOk = (bigSpr.createSprite(SCR_W, H_BIG) != nullptr);
  if (bigOk) bigSpr.loadFont(FontNum30);

  // 줄바꿈을 반드시 꺼야 한다. 켜져 있으면 칸(뷰포트) 폭을 넘는 글자가 다음 줄로
  // 넘어가 버리는데, 줄 스프라이트는 한 줄 높이뿐이라 그대로 사라진다.
  // 마퀴는 넘치는 글자를 "잘라내는" 것이 전제다.
  row.setTextWrap(false, false);
  bigSpr.setTextWrap(false, false);
  tft.setTextWrap(false, false);

  // TFT_eSPI 의 loadFont() 는 malloc 결과를 보지 않는다. 힙이 모자라면 널 포인터에
  // 그대로 써서 부팅 루프에 빠지므로 여기서 먼저 막는다(ChurchDisplayRx 와 동일).
  Serial.printf("폰트 적재 전  여유 힙=%u  최대블록=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (rowOk && ESP.getFreeHeap() >= FONT_HEAP_NEED && ESP.getMaxAllocHeap() >= FONT_BLOCK_NEED) {
    row.loadFont(FontKR16);
    fontOk = true;
  } else {
    Serial.println("힙 부족 — 한글 폰트를 올리지 않는다(ASCII 내장 폰트로 동작).");
  }
  Serial.printf("줄 스프라이트=%s  숫자 스프라이트=%s  한글=%s  여유 힙=%u\n",
                rowOk ? "OK" : "실패", bigOk ? "OK" : "실패", fontOk ? "OK" : "없음",
                ESP.getFreeHeap());

  render();                            // "불러오는 중" 화면부터 띄운다

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t until = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < until) {
    delay(200);
    Serial.print(".");
    render();                          // 기다리는 동안에도 화면은 살아 있게
  }
  Serial.printf("\n무선 %s  ip=%s  여유 힙=%u B\n",
                WiFi.status() == WL_CONNECTED ? "연결됨" : "실패",
                WiFi.localIP().toString().c_str(), ESP.getFreeHeap());

  nextFetchAt = millis() + (fetchSchedule() ? REFRESH_MS : RETRY_MS);
  dirtyAll = true;
  render();
}

void loop() {
  static uint32_t lastDraw = 0;
  static uint32_t lastWifi = 0;

  handleButtons();

  // 무선이 끊겼으면 조용히 다시 붙는다
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifi > 10000) {
    lastWifi = millis();
    WiFi.reconnect();
  }

  if (wantFetch || (int32_t)(millis() - nextFetchAt) >= 0) {
    wantFetch   = false;
    bool ok     = fetchSchedule();
    nextFetchAt = millis() + (ok ? REFRESH_MS : RETRY_MS);
    dirtyAll    = true;
  }

  // 마퀴가 부드럽게 흐르도록 30fps 로 돈다. 바뀐 줄만 실제로 다시 그려진다.
  if (millis() - lastDraw >= 33) {
    lastDraw = millis();
    render();
  }
}
