// GodlifeScheduleNext_JC3248 — 가이션 JC3248W535 판 하루동행 다음 일정 표시기
//
// 원본 GodlifeScheduleNext(TTGO T-Display, 240×135)를 화면·터치가 한 판에 붙어
// 나오는 기성품 보드로 옮긴 것이다. 하는 일은 같다.
//
//   GET https://jesusdream.kr/api/godlife/schedule/next?days=30&limit=4
//
//   보드     JC3248W535C_I_Y — ESP32-S3-WROOM-1 N16R8 (플래시 16MB · PSRAM 8MB)
//   화면     3.5" 320×480 IPS, AXS15231B, QSPI (고정 배선)
//   터치     정전식 — 화면과 같은 AXS15231B 칩을 I2C(0x3B)로 읽는다
//   손으로 잇는 것  없다. USB 만 꽂으면 된다
//
// ── 원본과 다른 곳 ────────────────────────────────────────────────
// 1) **내용을 훨씬 많이 보여준다.** 화면이 240×135 에서 320×480 으로 넓어져
//    픽셀이 열 배다(32,400 → 153,600). 원본은 한 줄에 다 못 넣어 마퀴로 흘리던
//    것을 여기서는 대부분 펼쳐 놓는다.
//      - 제목·메모를 **두 줄로 접어서** 보여준다(넘치면 그때만 마지막 줄이 흐른다)
//      - 준비물을 개수(2/3)만이 아니라 **이름까지** 체크 표시와 함께 적는다
//      - 이어지는 일정을 한 줄에 몰아 흘리지 않고 **일정마다 제 줄**에 놓는다(4건)
//      - 목록 화면도 한 줄이 아니라 **일정마다 세 줄**(제목·때·메모)로 적는다
//    마퀴는 없애지 않았다 — 접고도 넘치는 줄에서만 흐른다. 짧은 줄은 가만히 있는다.
// 2) TFT_eSPI 를 쓰지 않는다. 이 화면은 QSPI 라 TFT_eSPI 가 다루지 못한다.
//    Arduino_GFX 로 그리되 같은 이름의 껍데기를 뒀다(PanelTFT.h/.cpp).
//    한글 VLW 폰트는 VlwFont 가 직접 읽는다(TalentNfcReader_JC3245 와 같은 파일).
// 3) **줄 스프라이트가 없다.** 원본이 스프라이트를 둔 까닭은 깜빡임이었는데,
//    이 보드는 통짜 버퍼(PSRAM 300KB)에 다 그린 뒤 33ms 마다 통째로 밀어서
//    애초에 깜빡이지 않는다. 대신 마퀴가 옆 칸을 침범하지 않게 자를 칸(setClip)을
//    쓴다 — 원본의 스프라이트 뷰포트 자리다.
// 4) **단추가 없다. 화면을 누른다.** 원본의 위·아래 단추와 정전식 터치 핀(T9)은
//    이 보드에 없다 — 대신 진짜 터치 패널이 붙어 있다.
//      위쪽 상태 띠     : 지금 바로 새로고침
//      이어지는 일정 줄 : 그 일정으로 바로 건너뛴다(원본에 없던 것)
//      그 밖 아무 곳    : 다음 일정 → 그다음 → … → 목록 화면 → 처음으로
//      꺼진 화면        : 첫 눌림은 켜기로만 쓴다(그 한 번은 삼킨다)
// 5) 글자를 한 단계 키웠다 — 본문 20px · 제목 24px, 큰 시각 64px · D-day 34px.
//    **크기는 이 네 가지가 전부다.** VLW 는 구운 크기로만 그려져 크기마다 플래시를
//    한 벌씩 먹는다(한글 두 벌이 벌써 2.2MB). 그래서 꾸밈은 글자 크기가 아니라
//    색과 둥근 네모로만 한다 — 아래 6) 이 그 이야기다.
// 6) **유치원 일정표답게 꾸몄다(크레용 상자).** 어두운 밤색 판이던 것을 크림빛
//    바탕에 흰 카드를 얹은 모양으로 바꿨다.
//      - 일정마다 제 색을 준다(크레용 다섯 자루를 돌려 쓴다). 카드 왼쪽 띠 ·
//        D-day 알약 · 이어지는 일정 딱지가 그 색으로 물든다 — 색만 보고도 아까
//        보던 일정인지 안다
//      - 큰 시각 옆 D-day 는 알약에 담아 "오늘 · 내일 · 모레 · D-5" 로 적는다
//        (그래서 34px 숫자 폰트에 한글 여섯 자를 같이 구웠다 — tools/make-fonts.sh)
//      - 준비물 줄은 연한 민트 띠 위에 얹어 눈에 먼저 들어오게 했다
//      - 이어지는 일정은 줄마다 둥근 딱지 — 색이 번갈아 나와 줄이 섞이지 않는다
//    둥근 네모는 Box 하나로 적고 줄마다 잘라 칠한다(ScheduleTypes.h · drawRow).
// 7) CPU 를 80MHz 로 내리지 않는다. 원본의 전력 손잡이 중 이것만 뺐다 —
//    옥탈 PSRAM 과 QSPI 로 300KB 를 33ms 마다 미는 보드라 클럭을 내리면
//    화면이 밀리는 속도부터 영향을 받는다. 나머지 손잡이(모뎀 슬립 · 백라이트
//    듀티 · 무동작 화면 끄기 · 마퀴 바퀴 수)는 그대로 두었다.
//
// ── 빌드 ──────────────────────────────────────────────────────────
//   ./GodlifeScheduleNext_JC3248/build.sh              # 컴파일
//   ./GodlifeScheduleNext_JC3248/build.sh --upload     # 컴파일 + 업로드
//
// 필요 라이브러리: GFX Library for Arduino 1.5 이상, ArduinoJson 7.x, ChurchSecrets
// PSRAM(opi)을 반드시 켜야 한다 — 화면 버퍼 300KB 를 거기에 잡는다.
// 파티션은 앱 4MB 짜리 custom 표를 쓴다(한글 폰트 두 벌이 2.2MB).

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ChurchSecrets.h>
#include "PanelTFT.h"

// ── 서버 ──────────────────────────────────────────────────────────
// ChurchSecrets.h 에 GODLIFE_API_BASE 를 두면 그쪽이 우선한다(개발 중엔 로컬 서버).
#ifndef GODLIFE_API_BASE
#define GODLIFE_API_BASE "https://jesusdream.kr/api/godlife"
#endif
#define SCHEDULE_DAYS    30     // 앞으로 며칠까지 볼지 (서버 최대 90)
#define SCHEDULE_LIMIT    4     // next 뒤에 이어 붙일 개수 (서버 최대 10) — 화면에 네 줄이 들어간다
#define REFRESH_MS   300000UL   // 5분마다 다시 불러온다
#define RETRY_MS      20000UL   // 실패했을 때 다시 시도하는 간격
#define HTTP_TIMEOUT   8000

// 자료형과 폰트는 설정 #define 뒤에 둔다. Arduino 전처리기가 만들어 넣는 함수
// 원형이 마지막 전처리 지시문 바로 아래에 놓이므로, 여기가 지나야 구조체를 안다.
#include "ScheduleTypes.h"
#include "FontKR20.h"       // 본문·목록 (2,350자)
#include "FontKR24.h"       // 제목 (2,350자)
#include "FontNum34.h"      // D-day
#include "FontNum64.h"      // 큰 시각

// ── 전력 ──────────────────────────────────────────────────────────
// 하루 종일 켜 두는 기기다. 원본에서 150~170mA 를 45~60mA 로 내린 손잡이 가운데
// 이 보드에 그대로 옮긴 것들이다(CPU 클럭만 뺐다 — 맨 위 주석 참고).
static const uint8_t  BL_DUTY_ON = 150;       // 백라이트 밝기 0-255 (약 60%)
static const uint32_t BL_IDLE_MS = 300000UL;  // 이만큼 아무도 안 누르면 화면을 끈다 (0 = 끄지 않음)

static const int16_t SCR_W = 320;
static const int16_t SCR_H = 480;

// ── 가로 자리 ─────────────────────────────────────────────────────
// 카드는 좌우 8px 을 띄운 한 장(8..312)이고, 글은 그 안에서 또 들여 쓴다.
// 왼쪽에 크레용 띠(12px)가 있는 카드는 글이 더 들어와야 해서 두 벌을 둔다.
static const int16_t X_PAD  = 8,   X_CW = 304;   // 카드 자리 (8..312)
static const int16_t X_TL   = 22,  X_TR = 298;   // 띠 없는 카드 안 글 범위
static const int16_t X_CL   = 30,  X_CR = 296;   // 띠 있는 카드 안 글 범위
static const int16_t X_BIG  = 16;                // 큰 시각만 더 왼쪽에서 — "09:30" 이 175px 라 빠듯하다
static const int16_t X_DD   = 198, W_DD  = 104;  // D-day 알약 (198..302, "D-30" 79px 이 들어간다)
static const int16_t X_FDD  = 12,  W_FDD = 64;   // 이어지는 일정 줄의 D-day 칩 ("D-100" 58px)
static const int16_t X_FTX  = 84,  X_FTR = 306;  // 그 줄의 나머지 글 (222px)

// ── 세로 자리 (겹치지 않게 — 겹치면 옆 줄을 지운다) ────────────────
// 세로 480 을 이렇게 나눴다. 카드 한 장은 **여러 줄이 이어 붙은 것**이라,
// 카드 구간을 줄들이 빈틈없이 덮어야 한다(덮지 못한 띠는 바탕색으로 남는다).
static const int16_t Y_TOP   = 0;    static const int16_t H_TOP   = 36;   // 상태 알약
static const int16_t Y_HERO  = 40;   static const int16_t H_HERO  = 108;  // 시각 카드 (40..148)
static const int16_t Y_BIG   = 40;   static const int16_t H_BIG   = 76;   //   큰 시각 · D-day
static const int16_t Y_META  = 116;  static const int16_t H_META  = 32;   //   날짜 · 남은 시간
static const int16_t Y_CARD  = 152;  static const int16_t H_CARD  = 176;  // 일정 카드 (152..328)
static const int16_t Y_TITLE = 152;  static const int16_t H_TITLE = 32;   //   제목 두 줄
static const int16_t Y_SUB   = 216;  static const int16_t H_SUB   = 28;   //   기관 · 준비물 개수 딱지
static const int16_t Y_MEMO  = 244;  static const int16_t H_MEMO  = 26;   //   메모 두 줄
static const int16_t Y_PREP  = 296;  static const int16_t H_PREP  = 32;   //   준비물 이름 띠
static const int16_t Y_NEXT  = 332;  static const int16_t H_NEXT  = 26;   // '이어지는 일정' 소제목
static const int16_t Y_FOOT  = 358;  static const int16_t H_FOOT  = 30;   // 이어지는 일정 네 줄 (358..478)

// 목록 화면 — 상태 알약 아래로 일정마다 세 줄짜리 카드 한 장.
static const int16_t Y_LIST       = H_TOP;
static const int16_t H_LIST_T     = 32;   // 제목
static const int16_t H_LIST_M     = 26;   // 때
static const int16_t H_LIST_X     = 26;   // 메모
static const int16_t H_LIST_CARD  = H_LIST_T + H_LIST_M + H_LIST_X;   // 84 — 카드 한 장
static const int16_t H_LIST_BLOCK = H_LIST_CARD + 4;                  // 88 × 5 = 440 (36..476)

// 둥근 정도 — 카드는 넉넉히, 딱지와 알약은 반원이 되게.
static const int16_t R_CARD = 18, R_TAB = 6, R_PILL = 13, R_DD = 24;

// ── 크레용 상자 ───────────────────────────────────────────────────
// 일정마다 한 자루씩 돌려 쓴다. 연한 쪽은 바탕(딱지·알약), 진한 쪽은 글과 띠다.
static const uint8_t CRAYONS = 5;
static uint16_t CRAYON_LT[CRAYONS], CRAYON_DK[CRAYONS];

static uint16_t COL_BG, COL_BAR, COL_CARD, COL_DIM, COL_TXT, COL_ACC, COL_OK, COL_ERR,
                COL_WHITE, COL_PREP_BG, COL_PREP_TX;

// ── 폰트 ──────────────────────────────────────────────────────────
// VLW 는 구워 넣은 크기로만 그려진다(setTextSize 가 없다) — 크기마다 한 벌씩 든다.
// PanelTFT 가 네 벌을 올려 둔 채로 바꿔 끼우므로 여기서는 고르기만 한다.
static uint8_t curFont = 0xFF;
static void useFont(uint8_t id) {
  if (curFont == id) return;
  curFont = id;
  switch (id) {
    case F_TITLE: tft.loadFont(FontKR24);  break;
    case F_NUM34: tft.loadFont(FontNum34); break;
    case F_NUM64: tft.loadFont(FontNum64); break;
    default:      tft.loadFont(FontKR20);  break;
  }
}

static bool fontOk = false;   // 한글 폰트 메트릭이 올라갔는가(PSRAM 이 없으면 못 올린다)

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
static const int16_t  MARQ_GAP   = 56;      // 한 바퀴 사이 여백
static const uint32_t MARQ_HOLD  = 1400;    // 시작·한 바퀴마다 멈춰 있는 시간(ms)
// 흐르는 글은 30fps 로 그 줄을 다시 그리고, 버퍼가 더러워지면 미는 태스크가 300KB 를
// 민다. 다 읽을 만큼 돌았으면 멈춰 세워 그 비용을 없앤다. 새로 받아오거나(5분)
// 화면을 누르면 dirtyAll 이 resetMarquees() 를 부르므로 다시 처음부터 흐른다.
static const uint8_t  MARQ_ROUNDS = 3;      // 이만큼 돌면 앞으로 돌아가 멈춰 선다

static Marquee marq[M_COUNT];

static void resetMarquees() {
  for (uint16_t i = 0; i < M_COUNT; i++) marq[i] = Marquee{};
}

// UTF-8 경계를 지키며 복사한다. 멀티바이트 한 글자를 반토막 내면 네모로 그려진다.
static void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = 0;
  while (src[n] && n < cap - 1) n++;
  while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// 폰트에 없는 글자는 그려지지 않는다. 서버에서 오는 일정 제목에는 이모지가 섞여
// 들어온다(실제 응답에 "후문 하차❤️" 가 있었다). 그래서 우리 폰트에 구워 넣은
// 것만 남기고 나머지는 버린다. 한글 음절은 상용 2,350자 밖이면 여전히 빈자리다.
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

// 지금 폰트로 폭 maxW 에 들어가는 만큼만 out 에 담고, 남은 글의 시작을 돌려준다.
// 띄어쓰기가 있으면 거기서 끊고, 없으면(한글 제목에는 흔하다) 글자 경계에서 끊는다.
// 폭을 재는 일이 글자 수만큼 반복되므로 **글이 바뀔 때만** 부른다(buildLines).
static const char* wrapLine(const char* src, int16_t maxW, char* out, size_t cap) {
  while (*src == ' ') src++;
  out[0] = '\0';
  size_t      len   = 0;          // 지금까지 담은 바이트
  size_t      cut   = 0;          // 마지막 띄어쓰기 앞까지의 길이
  const char* cutAt = nullptr;    // 그때 남은 글의 시작
  const char* p     = src;

  while (*p) {
    uint16_t    cp;
    const char* q = VlwFont::nextCodepoint(p, cp);
    const size_t n = (size_t)(q - p);
    if (len + n >= cap) break;

    memcpy(out + len, p, n);
    len += n;
    out[len] = '\0';

    if (tft.textWidth(out) > maxW) {     // 이 글자까지 넣으면 넘친다 — 앞에서 끊는다
      len -= n;
      out[len] = '\0';
      if (cutAt) { out[cut] = '\0'; return cutAt; }
      return p;                           // 띄어쓰기가 없었다 — 글자 경계에서
    }
    if (cp == 0x20) { cut = len - n; cutAt = q; }
    p = q;
  }
  return p;                                // 다 들어갔다
}

// 줄 하나를 그린다. 글이 바뀌었거나 흐르는 중일 때만 실제로 그린다(force 면 무조건).
//
// 바탕은 세 겹이다 — 판 색(page)을 깔고, 그 위에 둥근 네모들(boxes)을 얹고, 글을 쓴다.
// 둥근 네모는 줄보다 큰 것을 그대로 적어도 된다. 자를 칸을 이 줄로 걸어 두므로
// **카드의 이 줄 몫만** 칠해진다 — 여러 줄에 같은 Box 를 넘기면 이어진 한 장이 된다.
static void drawRow(int16_t y, int16_t h, uint16_t page,
                    const Box* boxes, uint8_t nb,
                    const Seg* segs, uint8_t n, bool force) {
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
      m.rounds    = 0;
      m.done      = false;
      useFont(segs[i].font);           // 폭은 그 덩이가 쓸 폰트로 재야 한다
      int16_t tw  = tft.textWidth(m.text);
      int16_t aw  = segs[i].x1 - segs[i].x0;
      m.rolls     = (tw > aw);
      m.period    = tw + MARQ_GAP;
      needDraw    = true;
      continue;
    }
    if (!m.rolls || m.done) continue;
    if (now < m.holdUntil) { m.lastAdv = now; continue; }

    m.off += MARQ_SPEED * (now - m.lastAdv) / 1000.0f;
    m.lastAdv = now;
    if (m.off >= m.period) {           // 한 바퀴 — 처음으로 돌아가 잠깐 쉰다
      m.off -= m.period;
      if (++m.rounds >= MARQ_ROUNDS) {  // 다 읽을 만큼 돌았다 — 앞을 보인 채 멈춘다
        m.off  = 0;
        m.done = true;
      } else {
        m.holdUntil = now + MARQ_HOLD;
      }
    }
    needDraw = true;
  }
  if (!needDraw) return;

  tft.fillRect(0, y, SCR_W, h, page);
  tft.setClip(0, y, SCR_W, h);                 // 카드가 이 줄 밖으로 새지 않게
  for (uint8_t i = 0; i < nb; i++)
    tft.fillRoundRect(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].r, boxes[i].col);
  tft.clearClip();
  tft.setTextDatum(ML_DATUM);
  for (uint8_t i = 0; i < n; i++) {
    const Marquee& m = marq[segs[i].slot];
    if (!m.text[0]) continue;
    const int16_t aw = segs[i].x1 - segs[i].x0;

    useFont(segs[i].font);
    tft.setTextColor(segs[i].fg);      // 바탕은 이미 칠했다 — 글자는 그 위에 섞어 얹는다
    // 칸 밖으로 나가는 글자는 자를 칸이 잘라 준다 — 옆 덩이를 침범하지 않는다.
    tft.setClip(segs[i].x0, y, aw, h);
    if (m.rolls) {
      tft.drawString(m.text, segs[i].x0 - (int16_t)m.off, y + h / 2);
      // 한 바퀴 뒤를 이어 붙여 끊기지 않게 한다
      tft.drawString(m.text, segs[i].x0 - (int16_t)m.off + m.period, y + h / 2);
    } else if (segs[i].align == AL_RIGHT) {
      tft.drawString(m.text, segs[i].x1 - tft.textWidth(m.text), y + h / 2);
    } else if (segs[i].align == AL_CENTER) {
      tft.drawString(m.text, segs[i].x0 + (aw - tft.textWidth(m.text)) / 2, y + h / 2);
    } else {
      tft.drawString(m.text, segs[i].x0, y + h / 2);
    }
    tft.clearClip();
  }
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
  if (m < 0)        snprintf(out, cap, "진행 중");
  else if (m == 0)  snprintf(out, cap, "곧 시작");
  else if (m < 60)  snprintf(out, cap, "%ld분 뒤", m);
  else if (m < 1440) {
    if (m % 60) snprintf(out, cap, "%ld시간 %ld분 뒤", m / 60, m % 60);
    else        snprintf(out, cap, "%ld시간 뒤", m / 60);
  } else            snprintf(out, cap, "%ld일 %ld시간 뒤", m / 1440, (m % 1440) / 60);
}

static void fmtDday(const Item& it, char* out, size_t cap) {
  if (it.dDay == 0)      snprintf(out, cap, "오늘");
  else if (it.dDay == 1) snprintf(out, cap, "내일");
  else if (it.dDay == 2) snprintf(out, cap, "모레");
  else                   snprintf(out, cap, "D-%d", it.dDay);
}

// 큰 숫자 칸에 쓸 글. 시각이 있으면 "09:30", 종일이면 날짜 "10.07".
static void fmtBig(const Item& it, char* out, size_t cap) {
  if (it.allDay || it.time[0] < '0' || it.time[0] > '9')
    snprintf(out, cap, "%02d.%02d", it.month, it.day);
  else
    copyUtf8(out, cap, it.time);
}

static void fmtWhen(const Item& it, char* out, size_t cap) {
  snprintf(out, cap, "%d월 %d일 (%s)", it.month, it.day, it.weekday);
}

// 기관 · 준비물 개수 — 제목 아래 한 줄. 메모와 준비물 이름은 따로 제 줄을 갖는다.
static void fmtSub(const Item& it, char* out, size_t cap) {
  out[0] = '\0';
  size_t used = 0;
  char   prep[32] = "";
  if (it.prepTotal > 0) snprintf(prep, sizeof(prep), "준비물 %d/%d", it.prepDone, it.prepTotal);
  const char* parts[3] = { it.label, it.allDay ? "종일" : "", prep };
  for (uint8_t i = 0; i < 3; i++) {
    if (!parts[i][0]) continue;
    int wrote = snprintf(out + used, cap - used, "%s%s", used ? " · " : "", parts[i]);
    if (wrote < 0) break;
    used += (size_t)wrote;
    if (used >= cap - 1) break;
  }
}

// 이어지는 일정 한 줄에서 D-day 를 뺀 나머지 — "10/10(금) 09:30  원장 면담"
// 날짜는 붙여 쓴다. 한 줄에 236px 뿐이라 여기서 아낀 12px 이 제목 두 글자다.
static void fmtBrief(const Item& it, char* out, size_t cap) {
  char wd[10] = "";
  if (it.weekday[0]) snprintf(wd, sizeof(wd), "(%s)", it.weekday);
  snprintf(out, cap, "%d/%d%s %s  %s", it.month, it.day, wd, it.time, it.title);
}

// 목록 화면의 가운데 줄 — 때와 기관을 한 줄로.
static void fmtListWhen(const Item& it, char* out, size_t cap) {
  char dd[16];
  fmtDday(it, dd, sizeof(dd));
  snprintf(out, cap, "%s · %d월 %d일 (%s) · %s%s%s",
           dd, it.month, it.day, it.weekday, it.time,
           it.label[0] ? " · " : "", it.label);
}

// ══════════════════════════════════════════════════════════════════
//  접어 둔 줄 — 글이 바뀔 때만 다시 만든다(폭을 재는 일이 비싸다)
// ══════════════════════════════════════════════════════════════════
static char lnTitle[2][MARQ_TEXT_MAX];
static char lnMemo[2][MARQ_TEXT_MAX];
static char lnSub[96];
static char lnFootDd[SCHEDULE_LIMIT][16];
static char lnFoot[SCHEDULE_LIMIT][MARQ_TEXT_MAX];
static char lnListWhen[MAX_ITEMS][MARQ_TEXT_MAX];

// 두 줄에 나눠 담는다. 둘째 줄에는 남은 글을 통째로 둔다 — 넘치면 그 줄이 흐른다.
static void foldTwo(const char* src, int16_t maxW, uint8_t font,
                    char lines[2][MARQ_TEXT_MAX]) {
  lines[0][0] = lines[1][0] = '\0';
  if (!src || !src[0]) return;
  useFont(font);
  const char* rest = wrapLine(src, maxW, lines[0], MARQ_TEXT_MAX);
  while (*rest == ' ') rest++;
  copyUtf8(lines[1], MARQ_TEXT_MAX, rest);
}

static void buildLines() {
  const int16_t w = X_CR - X_CL;   // 카드 안에서 접는다
  lnSub[0] = '\0';
  for (uint8_t i = 0; i < 2; i++) { lnTitle[i][0] = '\0'; lnMemo[i][0] = '\0'; }
  for (uint8_t i = 0; i < SCHEDULE_LIMIT; i++) { lnFootDd[i][0] = '\0'; lnFoot[i][0] = '\0'; }
  for (uint8_t i = 0; i < MAX_ITEMS; i++) lnListWhen[i][0] = '\0';

  if (haveData && cursor < itemCount) {
    const Item& it = items[cursor];
    foldTwo(it.title, w, F_TITLE, lnTitle);
    foldTwo(it.memo,  w, F_BODY,  lnMemo);
    fmtSub(it, lnSub, sizeof(lnSub));
  } else {
    copyUtf8(lnTitle[0], MARQ_TEXT_MAX,
             haveData ? "다가오는 일정이 없습니다" : "일정을 불러오는 중입니다");
    if (lastError[0]) copyUtf8(lnMemo[0], MARQ_TEXT_MAX, lastError);
  }

  // 이어지는 일정 — 지금 보고 있는 것 다음부터
  for (uint8_t i = 0; i < SCHEDULE_LIMIT; i++) {
    const uint8_t idx = cursor + 1 + i;
    if (idx >= itemCount) break;
    fmtDday(items[idx], lnFootDd[i], sizeof(lnFootDd[i]));
    fmtBrief(items[idx], lnFoot[i], sizeof(lnFoot[i]));
  }
  for (uint8_t i = 0; i < itemCount && i < MAX_ITEMS; i++)
    fmtListWhen(items[i], lnListWhen[i], sizeof(lnListWhen[i]));
}

// ══════════════════════════════════════════════════════════════════
//  화면 그리기
// ══════════════════════════════════════════════════════════════════
static void drawStatusRow(bool force) {
  char left[48], right[48];

  if (screen == SCR_LIST)          snprintf(left, sizeof(left), "일정 %u개", itemCount);
  else if (cursor == 0)            snprintf(left, sizeof(left), "다음 일정");
  else                             snprintf(left, sizeof(left), "일정 %u/%u", cursor + 1, itemCount);

  uint16_t col = COL_DIM;
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(right, sizeof(right), "무선 끊김");
    col = COL_ERR;
  } else if (lastError[0]) {
    copyUtf8(right, sizeof(right), lastError);
    col = COL_ERR;
  } else if (!haveData) {
    snprintf(right, sizeof(right), "불러오는 중");
  } else {
    uint32_t sec = (millis() - fetchedAt) / 1000;
    if (sec < 60) snprintf(right, sizeof(right), "%lu초 전 갱신", (unsigned long)sec);
    else          snprintf(right, sizeof(right), "%lu분 전 갱신", (unsigned long)(sec / 60));
    col = COL_OK;
  }

  const Box pill[] = { { COL_BAR, X_PAD, 4, X_CW, 28, 14 } };
  const Seg segs[] = {
    { M_TOP_L, left,  X_TL, 170,  AL_LEFT,  COL_TXT, F_BODY },
    { M_TOP_R, right, 174,  X_TR, AL_RIGHT, col,     F_BODY },
  };
  drawRow(Y_TOP, H_TOP, COL_BG, pill, 1, segs, 2, force);
}

static void renderMain(bool force) {
  drawStatusRow(force);

  const bool   has = (haveData && cursor < itemCount);
  const Item&  it  = items[has ? cursor : 0];
  // 이 일정의 크레용. 카드 띠 · D-day 알약 · 딱지가 모두 이 색으로 물든다.
  const uint8_t  cr    = cursor % CRAYONS;
  const uint16_t light = CRAYON_LT[cr], dark = CRAYON_DK[cr];

  char big[16] = "", dd[16] = "", when[64] = "", remain[48] = "";
  if (has) {
    fmtBig(it, big, sizeof(big));
    fmtDday(it, dd, sizeof(dd));                // "오늘 · 내일 · 모레 · D-5"
    fmtWhen(it, when, sizeof(when));
    fmtCountdown(it, remain, sizeof(remain));   // D-day 는 알약에 있으니 여기선 남은 시간만
  }

  // ── 시각 카드 ── 큰 시각 + D-day 알약 / 날짜 + 남은 시간 알약
  const Box hero = { COL_CARD, X_PAD, Y_HERO, X_CW, H_HERO, R_CARD };

  const Box bigBox[] = { hero,
    { (uint16_t)(it.dDay == 0 ? COL_ERR : dark),   // 오늘이면 빨강 — 색만 봐도 안다
      X_DD, (int16_t)(Y_BIG + (H_BIG - 48) / 2), W_DD, 48, R_DD } };
  const Seg bigSeg[] = {
    { M_BIG, big, X_BIG, (int16_t)(X_DD - 4),    AL_LEFT,   COL_ACC,   F_NUM64 },
    { M_DD,  dd,  X_DD,  (int16_t)(X_DD + W_DD), AL_CENTER, COL_WHITE, F_NUM34 },
  };
  drawRow(Y_BIG, H_BIG, COL_BG, bigBox, has ? 2 : 1, bigSeg, 2, force);

  // 남은 시간 알약은 글에 맞춰 재단한다 — 1분마다 글이 바뀌니 그때 같이 잰다
  useFont(F_BODY);
  const int16_t rw = remain[0] ? (int16_t)(tft.textWidth(remain) + 22) : 0;
  const int16_t rx = X_TR - rw;
  const int16_t wx = (int16_t)(rx - 8 > X_TL + 40 ? rx - 8 : X_TL + 40);
  const Box metaBox[] = { hero,
    { light, rx, (int16_t)(Y_META + 3), rw, (int16_t)(H_META - 6), R_PILL } };
  const Seg metaSeg[] = {
    { M_META_L, when,   X_TL, wx,   AL_LEFT,   COL_TXT, F_BODY },
    { M_META_R, remain, rx,   X_TR, AL_CENTER, dark,    F_BODY },
  };
  drawRow(Y_META, H_META, COL_BG, metaBox, rw ? 2 : 1, metaSeg, 2, force);

  // ── 일정 카드 ── 제목 두 줄 · 딱지 · 메모 두 줄 · 준비물
  // 카드와 왼쪽 크레용 띠는 여섯 줄 모두에 같이 넘긴다 — 줄마다 제 몫만 칠해져
  // 이어 붙으면 한 장이 된다.
  const Box card  = { COL_CARD, X_PAD, Y_CARD, X_CW, H_CARD, R_CARD };
  const Box tab   = { dark, X_PAD, (int16_t)(Y_CARD + 8), 12, (int16_t)(H_CARD - 16), R_TAB };
  const Box cd[]  = { card, tab };

  const Seg t0[] = { { M_TITLE0, lnTitle[0], X_CL, X_CR, AL_LEFT, COL_TXT, F_TITLE } };
  const Seg t1[] = { { M_TITLE1, lnTitle[1], X_CL, X_CR, AL_LEFT, COL_TXT, F_TITLE } };
  drawRow(Y_TITLE,           H_TITLE, COL_BG, cd, 2, t0, 1, force);
  drawRow(Y_TITLE + H_TITLE, H_TITLE, COL_BG, cd, 2, t1, 1, force);

  // 기관 · 준비물 개수 — 글 길이에 맞춘 작은 딱지.
  // 폭은 **본문 폰트로** 재야 한다 — 바로 위 제목 줄이 24px 를 끼워 두고 갔다.
  useFont(F_BODY);
  int16_t sw = lnSub[0] ? (int16_t)(tft.textWidth(lnSub) + 22) : 0;
  if (sw > X_CR - X_CL) sw = X_CR - X_CL;
  const Box sbBox[] = { card, tab,
    { light, X_CL, (int16_t)(Y_SUB + 2), sw, (int16_t)(H_SUB - 4), 12 } };
  const Seg sb[] = { { M_SUB, lnSub, (int16_t)(X_CL + 11), (int16_t)(X_CL + sw - 11),
                       AL_LEFT, dark, F_BODY } };
  drawRow(Y_SUB, H_SUB, COL_BG, sbBox, sw ? 3 : 2, sb, 1, force);

  const Seg m0[] = { { M_MEMO0, lnMemo[0], X_CL, X_CR, AL_LEFT, COL_DIM, F_BODY } };
  const Seg m1[] = { { M_MEMO1, lnMemo[1], X_CL, X_CR, AL_LEFT, COL_DIM, F_BODY } };
  drawRow(Y_MEMO,            H_MEMO, COL_BG, cd, 2, m0, 1, force);
  drawRow(Y_MEMO + H_MEMO,   H_MEMO, COL_BG, cd, 2, m1, 1, force);

  // 준비물 이름 — 연한 민트 띠 위에 ● 챙긴 것 / ○ 아직인 것
  const Box prBox[] = { card, tab,
    { COL_PREP_BG, (int16_t)(X_CL - 6), (int16_t)(Y_PREP + 3),
      (int16_t)(X_CR - X_CL + 12), (int16_t)(H_PREP - 6), R_PILL } };
  const Seg pr[] = { { M_PREP, has ? it.prep : "", X_CL, X_CR, AL_LEFT, COL_PREP_TX, F_BODY } };
  drawRow(Y_PREP, H_PREP, COL_BG, prBox, (has && it.prepTotal > 0) ? 3 : 2, pr, 1, force);

  // ── 이어지는 일정 ──
  const bool none = !(cursor + 1 < itemCount);
  const Box dot[] = { { dark, 14, (int16_t)(Y_NEXT + H_NEXT / 2 - 4), 8, 8, 4 } };
  const Seg nx[] = {
    // 둘 다 고정이라 흐르면 안 된다 — 칸이 글보다 넓어야 한다.
    // ("이어지는 일정" 120px < 140 · "눌러서 넘김 ▶" 126px < 130)
    // 이어지는 일정이 없을 때만 안내를 접고 왼쪽에 자리를 다 준다("…없음" 164px).
    { M_NEXT_H,    none ? "이어지는 일정 없음" : "이어지는 일정",
      28, (int16_t)(none ? 302 : 168), AL_LEFT, COL_DIM, F_BODY },
    { M_NEXT_HINT, none ? "" : "눌러서 넘김 ▶", 172, 302, AL_RIGHT, COL_DIM, F_BODY },
  };
  drawRow(Y_NEXT, H_NEXT, COL_BG, dot, 1, nx, 2, force);

  for (uint8_t i = 0; i < SCHEDULE_LIMIT; i++) {
    const int16_t y  = Y_FOOT + i * H_FOOT;
    const uint8_t c2 = (uint8_t)((cursor + 1 + i) % CRAYONS);
    const Box fb[] = {
      { CRAYON_LT[c2], X_PAD, (int16_t)(y + 2), X_CW,  (int16_t)(H_FOOT - 4),  R_PILL },
      { CRAYON_DK[c2], X_FDD, (int16_t)(y + 5), W_FDD, (int16_t)(H_FOOT - 10), 10 },
    };
    const Seg row[] = {
      { (uint8_t)(M_FOOT_0 + i * 2),     lnFootDd[i], X_FDD, (int16_t)(X_FDD + W_FDD),
        AL_CENTER, COL_WHITE, F_BODY },
      { (uint8_t)(M_FOOT_0 + i * 2 + 1), lnFoot[i],   X_FTX, X_FTR, AL_LEFT, COL_TXT, F_BODY },
    };
    drawRow(y, H_FOOT, COL_BG, fb, lnFootDd[i][0] ? 2 : 0, row, 2, force);
  }
}

// 목록 화면 — 받아온 일정을 카드 한 장에 세 줄씩. 긴 줄은 여기서도 흐른다.
static void renderList(bool force) {
  drawStatusRow(force);

  for (uint8_t i = 0; i < MAX_ITEMS; i++) {
    const bool    has = (i < itemCount);
    const int16_t y   = Y_LIST + i * H_LIST_BLOCK;
    const uint8_t cr  = i % CRAYONS;
    const uint8_t s   = M_LIST_0 + i * 3;
    // 고른 일정은 카드째 그 크레용 색으로 물든다 — 다섯 장이 나란해도 금방 찾는다
    const Box bx[] = {
      { (uint16_t)(i == cursor ? CRAYON_LT[cr] : COL_CARD), X_PAD, y, X_CW, H_LIST_CARD, 16 },
      { CRAYON_DK[cr], X_PAD, (int16_t)(y + 8), 12, (int16_t)(H_LIST_CARD - 16), R_TAB },
    };
    const uint8_t nb = has ? 2 : 0;

    const Seg ti[] = { { s,                has ? items[i].title : "", X_CL, X_CR, AL_LEFT,
                         i == cursor ? CRAYON_DK[cr] : COL_TXT, F_TITLE } };
    const Seg wh[] = { { (uint8_t)(s + 1), lnListWhen[i],            X_CL, X_CR, AL_LEFT, COL_TXT, F_BODY } };
    const Seg mo[] = { { (uint8_t)(s + 2), has ? items[i].memo : "", X_CL, X_CR, AL_LEFT, COL_DIM, F_BODY } };
    drawRow(y,                        H_LIST_T, COL_BG, bx, nb, ti, 1, force);
    drawRow(y + H_LIST_T,             H_LIST_M, COL_BG, bx, nb, wh, 1, force);
    drawRow(y + H_LIST_T + H_LIST_M,  H_LIST_X, COL_BG, bx, nb, mo, 1, force);
  }
}

static void render() {
  bool force = dirtyAll;
  if (force) {
    tft.fillScreen(COL_BG);
    resetMarquees();
    buildLines();          // 접는 일은 여기서만 — 매 프레임 폭을 재지 않는다
    dirtyAll = false;
  }
  if (screen == SCR_MAIN) renderMain(force);
  else                    renderList(force);
}

// ══════════════════════════════════════════════════════════════════
//  서버에서 일정 받아오기
// ══════════════════════════════════════════════════════════════════
// 쓰지 않는 필드까지 담으면 힙만 먹는다. 필요한 것만 걸러서 읽는다.
// 원본과 달리 준비물의 이름(text)도 받는다 — 화면이 넓어져 이름을 적을 자리가 있다.
static const char FILTER_JSON[] =
  "{\"days\":true,\"next\":{\"title\":true,\"institutionLabel\":true,\"date\":true,\"weekday\":true,"
  "\"time\":true,\"isAllDay\":true,\"memo\":true,\"dDay\":true,\"minutesUntil\":true,"
  "\"preparations\":[{\"text\":true,\"checked\":true}]},"
  "\"following\":[{\"title\":true,\"institutionLabel\":true,\"date\":true,\"weekday\":true,"
  "\"time\":true,\"isAllDay\":true,\"memo\":true,\"dDay\":true,\"minutesUntil\":true,"
  "\"preparations\":[{\"text\":true,\"checked\":true}]}]}";

static void parseItem(JsonObjectConst o, Item& it) {
  it = Item{};
  // 서버가 준 글은 폰트에 있는 글자만 걸러서 담는다(이모지 → 빈자리 방지).
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

  // 준비물 — 개수와 함께 이름을 "● 돗자리  ○ 도시락" 으로 이어 붙인다.
  // 챙긴 것은 ●, 아직인 것은 ○ 다(폰트에 구워 넣은 기호).
  size_t used = 0;
  for (JsonObjectConst p : o["preparations"].as<JsonArrayConst>()) {
    it.prepTotal++;
    const bool checked = p["checked"] | false;
    if (checked) it.prepDone++;

    char name[48];
    copyUtf8Font(name, sizeof(name), p["text"] | "");
    if (!name[0] || used >= sizeof(it.prep) - 1) continue;
    int wrote = snprintf(it.prep + used, sizeof(it.prep) - used, "%s%s %s",
                         used ? "  " : "", checked ? "●" : "○", name);
    if (wrote < 0) break;
    used += (size_t)wrote;
  }
  if (!it.title[0]) copyUtf8(it.title, sizeof(it.title), "제목 없음");
}

static bool fetchSchedule() {
  if (WiFi.status() != WL_CONNECTED) {
    copyUtf8(lastError, sizeof(lastError), "무선 끊김");
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
    copyUtf8(lastError, sizeof(lastError), "연결 실패");
    return false;
  }

  Serial.printf("[요청] %s  (여유 힙 %lu B)\n", url, (unsigned long)ESP.getFreeHeap());
  int code = http.GET();
  if (code != 200) {
    http.end();
    if (code <= 0) copyUtf8(lastError, sizeof(lastError), "서버 연결 실패");
    else           snprintf(lastError, sizeof(lastError), "서버 오류 %d", code);
    Serial.printf("[요청] 실패 code=%d\n", code);
    return false;
  }

  String ctype = http.header("Content-Type");
  if (ctype.indexOf("json") < 0) {
    http.end();
    copyUtf8(lastError, sizeof(lastError), "JSON 이 아님");
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
    copyUtf8(lastError, sizeof(lastError), "응답 형식 오류");
    Serial.printf("[요청] JSON 파싱 실패: %s\n", err.c_str());
    return false;
  }

  if (!doc["days"].is<int>()) {   // 우리 API 의 응답 모양이 아니다
    copyUtf8(lastError, sizeof(lastError), "응답 형식 오류");
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

  Serial.printf("[요청] 일정 %u건  다음=%s  (여유 힙 %lu B)\n",
                itemCount, itemCount ? items[0].title : "-", (unsigned long)ESP.getFreeHeap());
  return true;
}

static uint32_t nextFetchAt = 0;   // 다음 조회 시각(millis)
static bool     wantFetch   = false;

// ══════════════════════════════════════════════════════════════════
//  백라이트와 화면 끄기
// ══════════════════════════════════════════════════════════════════
// 이 보드의 백라이트는 GPIO1 하나다(PanelTFT.h 의 JC_LCD_BL). 코어의 analogWrite 가
// LEDC 를 알아서 잡아 주므로 원본처럼 채널을 직접 잡을 필요가 없다.
static bool     blOn  = true;
static uint32_t lastActivity = 0;   // 마지막 터치 시각

static void backlightSet(uint8_t duty) { analogWrite(JC_LCD_BL, duty); }

// 눌림이 있었다고 알린다. 꺼져 있던 화면을 켰으면 true — 그 눌림은 삼켜야 한다.
static bool screenWake() {
  lastActivity = millis();
  if (blOn) return false;
  blOn = true;
  tft.writecommand(0x29);             // DISPON
  backlightSet(BL_DUTY_ON);
  dirtyAll = true;                    // 남은 시간을 지금 값으로 맞춰 다시 그린다
  Serial.println("[화면] 켬");
  return true;
}

// 백라이트와 패널 구동부를 같이 내린다. WiFi 와 5분 폴링은 그대로 돈다 —
// 깨우면 최신 일정이 이미 들어와 있다. 터치는 화면이 꺼져 있어도 읽힌다
// (I2C 로 도는 칩이라 패널을 꺼도 살아 있다).
static void screenSleepIfIdle() {
  if (!blOn || BL_IDLE_MS == 0) return;
  if (millis() - lastActivity < BL_IDLE_MS) return;
  blOn = false;
  backlightSet(0);
  tft.writecommand(0x28);             // DISPOFF
  Serial.println("[화면] 무동작 — 끔");
}

// ══════════════════════════════════════════════════════════════════
//  터치 — 원본의 위·아래 단추 자리
// ══════════════════════════════════════════════════════════════════
//   위쪽 상태 띠     : 지금 바로 새로고침 (원본의 아래 단추)
//   이어지는 일정 줄 : 그 일정으로 건너뛴다 (원본에 없던 것 — 좌표가 있어 생겼다)
//   그 밖            : 다음 일정 → … → 목록 → 처음으로 (원본의 위 단추)
static void onTap(uint16_t x, uint16_t y) {
  (void)x;
  if (y < H_TOP) {                            // 상태 띠 — 갱신 시각이 적힌 곳을 누르면 갱신
    wantFetch = true;
    lastError[0] = '\0';
    Serial.println("[터치] 상태 띠 → 새로고침");
    return;
  }

  if (screen == SCR_LIST) {
    const int16_t i = (y - Y_LIST) / H_LIST_BLOCK;
    if (i >= 0 && i < itemCount) cursor = (uint8_t)i;
    screen = SCR_MAIN;
    dirtyAll = true;
    Serial.printf("[터치] 목록 %d → 본화면\n", i);
    return;
  }

  if (y >= Y_FOOT) {                          // 이어지는 일정 줄
    const uint8_t i   = (y - Y_FOOT) / H_FOOT;
    const uint8_t idx = cursor + 1 + i;
    if (i < SCHEDULE_LIMIT && idx < itemCount) {
      cursor = idx;
      dirtyAll = true;
      Serial.printf("[터치] 이어지는 일정 %u → 일정 %u\n", i, idx);
      return;
    }
  }

  if (cursor + 1 < itemCount) { cursor++; }
  else                        { screen = SCR_LIST; cursor = 0; }
  dirtyAll = true;
  Serial.printf("[터치] 다음 → %s %u\n", screen == SCR_LIST ? "목록" : "본화면", cursor);
}

// 누르는 동안이 아니라 **누르기 시작한 한 번**만 친다. 250ms 잠금은 칩이 내는
// 헛값(PanelTFT::getTouch 주석)이 두 번 누름으로 세어지지 않게 하는 마지막 빗장이다.
static void handleTouch() {
  static uint32_t lastPoll = 0, lockUntil = 0;
  static bool     wasDown  = false;

  if (millis() - lastPoll < 30) return;       // I2C 한 번에 1ms 남짓 — 30ms 면 넉넉하다
  lastPoll = millis();

  uint16_t tx = 0, ty = 0;
  const bool down = tft.getTouch(&tx, &ty);
  if (!down) { wasDown = false; return; }
  if (wasDown || millis() < lockUntil) return;
  wasDown   = true;
  lockUntil = millis() + 250;

  // 화면이 꺼져 있었다면 이 눌림은 "켜기" 로만 쓴다. 안 그러면 불을 켜려던 손짓이
  // 일정을 넘겨 버리거나 새로고침을 돌린다.
  if (screenWake()) return;
  onTap(tx, ty);
}

// ══════════════════════════════════════════════════════════════════
static void initColors() {
  // 크레용으로 칠한 유치원 알림판 — 크림빛 도화지에 흰 카드를 얹은 모양이다.
  COL_BG      = PanelTFT::color565(255, 247, 234);   // 도화지
  COL_BAR     = PanelTFT::color565(255, 225, 232);   // 맨 위 상태 알약(연분홍)
  COL_CARD    = PanelTFT::color565(255, 255, 255);   // 카드
  COL_TXT     = PanelTFT::color565( 74,  60,  52);   // 진한 코코아 — 검정보다 부드럽다
  COL_DIM     = PanelTFT::color565(150, 136, 126);   // 메모처럼 한 걸음 뒤에 둘 글
  COL_ACC     = PanelTFT::color565(255, 122,  72);   // 큰 시각(살구)
  COL_OK      = PanelTFT::color565( 30, 162, 122);   // 갱신 잘 됨
  COL_ERR     = PanelTFT::color565(232,  76,  96);   // 탈 났을 때 · 오늘인 일정
  COL_WHITE   = PanelTFT::color565(255, 255, 255);   // 알약·칩 위의 글
  COL_PREP_BG = PanelTFT::color565(211, 243, 228);   // 준비물 띠(연민트)
  COL_PREP_TX = PanelTFT::color565( 26, 140, 104);

  // 크레용 다섯 자루 — 분홍 · 하늘 · 민트 · 레몬 · 라일락.
  // 일정마다 한 자루씩 돌아가며 물들인다(연한 쪽은 바탕, 진한 쪽은 띠와 글).
  static const uint8_t LT[CRAYONS][3] = {
    {255, 228, 236}, {219, 238, 255}, {211, 243, 228}, {255, 240, 200}, {234, 227, 255} };
  static const uint8_t DK[CRAYONS][3] = {
    {226,  92, 136}, { 58, 136, 220}, { 30, 162, 122}, {214, 144,  28}, {126, 104, 210} };
  for (uint8_t i = 0; i < CRAYONS; i++) {
    CRAYON_LT[i] = PanelTFT::color565(LT[i][0], LT[i][1], LT[i][2]);
    CRAYON_DK[i] = PanelTFT::color565(DK[i][0], DK[i][1], DK[i][2]);
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // USB 가 꽂혀 있는데 아무도 로그를 읽지 않으면 코어는 쓸 때마다 최대 2초를 기다린다.
  // 로그는 버려도 되니 기다리지 않는다.
  Serial.setTxTimeoutMs(0);
#endif
  delay(200);
  Serial.println("\n=== GodlifeScheduleNext (JC3248W535) 시작 ===");

  // 백라이트는 여기서 건드리지 않는다. tft.init() 이 핀을 잡고 꺼 둔 채로 두므로
  // 첫 그림이 올라갈 때까지 화면은 어둡다 — 켜는 순간의 지저분한 버퍼가 보이지 않는다.
  // (여기서 analogWrite 로 먼저 잡으면 init() 의 digitalWrite 와 서로를 덮는다.)
  //
  // 폰트를 고르지 않았을 때 쓸 기본 폰트를 껍데기에 알려 준다. 폰트 데이터는
  // 이 파일만 들고 있는다 — PanelTFT.cpp 에서 같은 헤더를 넣으면 플래시에 두 벌이 된다.
  tft.setBasicFont(FontKR20);
  tft.init();
  tft.setRotation(0);
  initColors();
  tft.fillScreen(COL_BG);

  // 폰트 메트릭은 PSRAM 에 올라간다(VlwFont::load). 못 올라가면 글자가 하나도
  // 그려지지 않으므로 여기서 한 번 확인해 알린다 — 대개 PSRAM 설정이 빠진 것이다.
  useFont(F_BODY);
  fontOk = tft.fontLoaded();
  Serial.printf("한글 폰트=%s  여유 힙=%lu  PSRAM=%lu\n",
                fontOk ? "OK" : "실패(PSRAM 설정을 보라)",
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());

  lastActivity = millis();
  render();                            // "불러오는 중" 화면부터 띄운다
  tft.flushNow();                      // 미는 태스크를 기다리지 않고 지금 보여준다
  backlightSet(BL_DUTY_ON);

  WiFi.mode(WIFI_STA);
  // 모뎀 슬립을 켠다(기본값 WIFI_PS_MIN_MODEM). 비콘 주기에만 RF 를 깨우므로 평균
  // 전류가 크게 떨어진다. 서버가 밀어 주는 것이 없고 5분마다 이쪽에서 GET 을 거는
  // 구조라, 늦어지는 것은 수신 지연뿐 — 우리 요청에는 영향이 없다.
  WiFi.setSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t until = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < until) {
    delay(200);
    Serial.print(".");
    render();                          // 기다리는 동안에도 화면은 살아 있게
  }
  Serial.printf("\n무선 %s  ip=%s  여유 힙=%lu B\n",
                WiFi.status() == WL_CONNECTED ? "연결됨" : "실패",
                WiFi.localIP().toString().c_str(), (unsigned long)ESP.getFreeHeap());

  nextFetchAt = millis() + (fetchSchedule() ? REFRESH_MS : RETRY_MS);
  dirtyAll = true;
  render();
}

void loop() {
  static uint32_t lastDraw = 0;
  static uint32_t lastWifi = 0;

  handleTouch();

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

  screenSleepIfIdle();

  // 마퀴가 부드럽게 흐르도록 30fps 로 돈다. 바뀐 줄만 실제로 다시 그려진다.
  // 꺼진 화면은 그릴 이유가 없다 — 깨울 때 dirtyAll 로 전부 다시 그린다.
  if (blOn && millis() - lastDraw >= 33) {
    lastDraw = millis();
    render();
  }

  // 남는 시간은 idle 태스크에 넘긴다. 이게 없으면 loopTask 가 CPU 를 놓지 않아
  // WAITI(클럭 게이팅)로 못 내려가고 계속 최고 속도로 돈다.
  delay(blOn ? 5 : 20);
}
