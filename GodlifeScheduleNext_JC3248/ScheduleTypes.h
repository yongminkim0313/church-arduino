#pragma once
// GodlifeScheduleNext_JC3248 가 쓰는 자료형.
//
// .ino 안에 두면 Arduino 전처리기가 만들어 넣는 함수 원형이 구조체 정의보다
// 위에 놓여 컴파일이 깨진다(원본 GodlifeScheduleNext/ScheduleTypes.h 와 같은 이유).
//
// 원본(240×135)과 다른 곳:
//   - 화면이 넓어져 한 화면에 일정 5건(다음 1 + 이어지는 4)이 들어간다
//   - 준비물을 개수만이 아니라 **이름까지** 담는다(prep) — 넓어진 자리에 쓴다
//   - 글 칸이 커진 만큼 글 담는 칸도 늘렸다(title 80→96, memo 120→160)
//   - 유치원 일정표라 생김새를 크레용 상자처럼 꾸몄다 — 그래서 줄마다 바탕에
//     깔 **둥근 네모(Box)** 가 필요하다. 카드·딱지·알약이 다 이것 하나로 그려진다.

#include <Arduino.h>

#ifndef SCHEDULE_LIMIT
#define SCHEDULE_LIMIT 4
#endif
// next 하나 + following 몇 개
#define MAX_ITEMS (SCHEDULE_LIMIT + 1)

// 서버가 준 일정 하나. 화면에 쓸 것만 담는다.
struct Item {
  char  title[96];
  char  label[24];      // institutionLabel — 유치원 / 어린이집 / 가정
  char  weekday[6];
  char  time[12];       // "09:30" 또는 "종일"
  char  memo[160];
  char  prep[160];      // "● 돗자리  ○ 도시락" — 체크 표시를 앞에 붙여 이어 놓은 준비물
  int   month, day;
  bool  allDay;
  int   dDay;
  long  minutesUntil;   // 받아온 시점 기준. 화면에서는 그동안 흐른 시간을 뺀다
  int   prepTotal, prepDone;
};

// 화면
enum Screen { SCR_MAIN, SCR_LIST };

// 폰트 — 본체가 실제 VLW 배열을 물려 준다(useFont).
enum FontId : uint8_t { F_BODY, F_TITLE, F_NUM34, F_NUM64 };

// ── 마퀴 ──────────────────────────────────────────────────────────
#define MARQ_TEXT_MAX 240

// 줄(정확히는 줄 안의 글 덩이)마다 하나씩. 글이 칸을 넘칠 때만 흐른다.
struct Marquee {
  char     text[MARQ_TEXT_MAX];   // 마지막으로 그린 글 — 바뀌면 처음부터 다시
  int16_t  period;                // 글 폭 + 여백. 이만큼 흐르면 한 바퀴
  float    off;                   // 지금 밀린 픽셀
  uint32_t holdUntil;             // 이 시각까지는 멈춰 있는다
  uint32_t lastAdv;               // 마지막으로 민 시각(프레임 간격을 재려고)
  bool     rolls;                 // 넘쳐서 흘러야 하는 줄인가
  uint8_t  rounds;                // 지금까지 돈 바퀴 수
  bool     done;                  // 다 돌아서 멈춰 섰는가 — 더는 다시 그리지 않는다
};

// 마퀴 상태 번호. 줄마다·덩이마다 따로 흐르게 하려고 나눠 둔다.
// 원본보다 줄이 훨씬 많다 — 제목·메모가 두 줄로 접히고, 이어지는 일정이
// 한 줄에 몰리지 않고 일정마다 제 줄을 갖기 때문이다.
enum {
  M_TOP_L, M_TOP_R,              // 상태 띠 좌·우
  M_BIG, M_DD,                   // 큰 시각 · D-day 알약
  M_META_L, M_META_R,            // 날짜 / 남은 시간
  M_TITLE0, M_TITLE1,            // 제목 두 줄(접고 남으면 둘째 줄이 흐른다)
  M_SUB,                         // 기관 · 준비물 개수
  M_MEMO0, M_MEMO1,              // 메모 두 줄
  M_PREP,                        // 준비물 이름
  M_NEXT_H, M_NEXT_HINT,         // '이어지는 일정' 소제목과 터치 안내
  M_FOOT_0,                      // 이어지는 일정 — 줄마다 둘(D-day 칩 · 나머지)
  M_LIST_0 = M_FOOT_0 + SCHEDULE_LIMIT * 2,   // 목록 화면 — 일정마다 세 줄
  M_COUNT  = M_LIST_0 + MAX_ITEMS * 3
};

enum Align { AL_LEFT, AL_RIGHT, AL_CENTER };   // 가운데는 알약·칩 안에 글을 놓을 때

// 줄 바탕에 까는 둥근 네모. 카드처럼 **여러 줄에 걸친** 것도 그대로 적는다 —
// 줄을 그릴 때 그 줄만큼만 잘려 칠해지므로(drawRow 의 자를 칸) 줄마다 같은 Box 를
// 넘기면 이어진 한 장의 카드가 된다. r 이 크면 알약이 된다.
struct Box {
  uint16_t col;
  int16_t  x, y, w, h, r;
};

// 한 줄 안의 글 한 덩이. 줄 하나에 여러 덩이를 나란히 놓을 수 있다.
struct Seg {
  uint8_t     slot;     // 마퀴 상태 번호
  const char* text;
  int16_t     x0, x1;   // 이 덩이가 쓸 수 있는 가로 범위
  Align       align;    // 넘치지 않을 때의 정렬
  uint16_t    fg;
  uint8_t     font;     // FontId
};
