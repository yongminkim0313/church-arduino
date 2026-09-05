#pragma once
// GodlifeScheduleNext 가 쓰는 자료형.
//
// .ino 안에 두면 Arduino 전처리기가 만들어 넣는 함수 원형이 구조체 정의보다
// 위에 놓여 컴파일이 깨진다(TalentNfcReader/TalentTypes.h 와 같은 이유).

#include <Arduino.h>

#ifndef SCHEDULE_LIMIT
#define SCHEDULE_LIMIT 3
#endif
// next 하나 + following 몇 개
#define MAX_ITEMS (SCHEDULE_LIMIT + 1)

// 서버가 준 일정 하나. 화면에 쓸 것만 담는다.
struct Item {
  char  title[80];
  char  label[20];      // institutionLabel — 유치원 / 어린이집 / 가정
  char  weekday[6];
  char  time[12];       // "09:30" 또는 "종일"
  char  memo[120];
  int   month, day;
  bool  allDay;
  int   dDay;
  long  minutesUntil;   // 받아온 시점 기준. 화면에서는 그동안 흐른 시간을 뺀다
  int   prepTotal, prepDone;
};

// 화면
enum Screen { SCR_MAIN, SCR_LIST };

// ── 마퀴 ──────────────────────────────────────────────────────────
#define MARQ_TEXT_MAX 200

// 줄(정확히는 줄 안의 글 덩이)마다 하나씩. 글이 칸을 넘칠 때만 흐른다.
struct Marquee {
  char     text[MARQ_TEXT_MAX];   // 마지막으로 그린 글 — 바뀌면 처음부터 다시
  int16_t  period;                // 글 폭 + 여백. 이만큼 흐르면 한 바퀴
  float    off;                   // 지금 밀린 픽셀
  uint32_t holdUntil;             // 이 시각까지는 멈춰 있는다
  uint32_t lastAdv;               // 마지막으로 민 시각(프레임 간격을 재려고)
  bool     rolls;                 // 넘쳐서 흘러야 하는 줄인가
};

// 마퀴 상태 번호. 줄마다·덩이마다 따로 흐르게 하려고 나눠 둔다.
enum {
  M_TOP_L, M_TOP_R, M_META_L, M_META_R, M_TITLE, M_MEMO, M_FOOT,
  M_LIST_0, M_LIST_1, M_LIST_2, M_LIST_3, M_COUNT
};

enum Align { AL_LEFT, AL_RIGHT };

// 한 줄 안의 글 한 덩이. 줄 하나에 여러 덩이를 나란히 놓을 수 있다.
struct Seg {
  uint8_t     slot;     // 마퀴 상태 번호
  const char* text;
  int16_t     x0, x1;   // 이 덩이가 쓸 수 있는 가로 범위
  Align       align;    // 넘치지 않을 때의 정렬
  uint16_t    fg;
};
