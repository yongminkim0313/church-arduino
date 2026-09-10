#pragma once
#include <stdint.h>

// 화면 위쪽 탭. 탭이 곧 모드다 — 보이는 것과 태깅이 하는 일을 어긋나게 두지 않는다.
//
// .ino 본문이 아니라 여기 두는 이유는 TalentResult 와 같다: 스케치의 함수
// 프로토타입이 파일 앞쪽에 자동 삽입되는데, tabColor(Tab)·tabHit(...,Tab*) 의
// 프로토타입이 enum 선언보다 먼저 놓이면 "Tab 은 타입이 아니다"로 깨진다.
enum Tab : uint8_t { TAB_EARN, TAB_SPEND, TAB_HISTORY };

// 탭 버튼 한 칸. 그림이 곧 라벨이라 글자를 따로 두지 않는다.
// enum Tab · PointArt 와 같은 이유로 헤더에 둔다(자동 프로토타입보다 먼저 보여야 한다).
// 탭 버튼 한 칸. 그림은 두 곳에서 온다:
//   · 펌웨어에 구워 넣은 것(FunFunLogo.h) — 서버에 아무것도 안 올렸을 때
//   · 서버에서 받아 LittleFS 에 둔 것 — 올렸으면 이쪽이 이긴다
// file 이 비어 있지 않으면 파일 쪽을 쓴다.
struct TabBtn {
  Tab             key;
  const uint16_t* img;      // 고른 상태(색) — 구워 넣은 그림
  const uint16_t* off;      // 고르지 않은 상태(회색)
  int16_t         w, h;
  char            file[40];    // 서버에서 받은 그림(고른 상태). 비어 있으면 구워 넣은 것을 쓴다
  char            fileOff[40]; // 〃 고르지 않은 상태
};

// 서버에서 받은 포인트 그림 한 장. 파일 이름이 곧 해시라 "있으면 최신" 이다.
// enum Tab · PointArt · TabBtn 과 같은 이유로 헤더에 둔다(자동 프로토타입 때문).
struct ArtFile {
  int32_t base;          // 이 그림 한 장이 뜻하는 포인트 (메뉴 그림은 안 쓴다)
  // 가장 긴 이름이 메뉴 그림이다 — "menu-spend-a1b2c3d4-e5f6a7b8.565" 는 32자.
  // 여기가 짧으면 cfgFetch 가 그 그림만 조용히 버린다.
  char    file[36];
  int16_t w, h;
};

// 포인트 그림 한 장. base 포인트를 뜻하고, 배수는 화면에서 "× N" 으로 붙인다.
//
// enum Tab 과 같은 이유로 헤더에 둔다: pickArt() 가 이 타입을 돌려주는데, 스케치의
// 함수 프로토타입이 파일 앞쪽에 자동 삽입되면서 struct 선언보다 먼저 놓여
// "PointArt 는 타입이 아니다" 로 깨진다.
struct PointArt {
  int32_t         base;      // 이 그림 한 장이 뜻하는 포인트
  const uint16_t* img;
  int16_t         w, h;
};

// 서버(/api/talent) 응답을 한 덩어리로 담는다.
// .ino 는 함수 프로토타입을 파일 앞쪽에 자동 삽입하는데, 그 시점에 이 타입이
// 보여야 하므로 스케치 본문이 아니라 헤더에 둔다.
struct TalentResult {
  bool    ok         = false;
  int32_t balance    = 0;
  int32_t delta      = 0;
  bool    lowBalance = false;   // 잔액 부족(서버 409)
  char    reason[32] = "";
};

// 값을 범위 안으로 조인다.
// .ino 본문에 두면 자동 삽입되는 함수 프로토타입이 template 앞에 끼어들어
// "'T' does not name a type" 로 깨진다. 그래서 타입들과 함께 헤더에 둔다.
template <typename T>
static inline T clampT(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 지급/사용 카드 한 장. "무엇을 얼마나" 를 담는다 — 어린이 키링이 "누구" 인 것과 짝이다.
// TalentResult 와 같은 이유로 헤더에 둔다: .ino 는 함수 프로토타입을 파일 앞쪽에
// 자동 삽입하는데, 그 시점에 이 타입이 보여야 한다.
#define CARD_MAX 16
struct CardEntry {
  char     uid[24];
  char     name[24];
  int32_t  amount;
  bool     spend;        // 사용 카드인가(아니면 지급)
  char     img[40];      // 카드 그림 파일 이름(없으면 빈 문자열)
  uint16_t imgW, imgH;
};

// 모바일 조회 링크 한 줄. 키링(NTAG213)에 NDEF URI 로 써 넣을 주소다.
//
// 아이가 자기 휴대전화에 키링을 대면 잔액과 최근 내역이 뜬다. 카드에는 **주소만**
// 들어가고 포인트는 서버에 그대로라, 폰으로 카드를 고쳐 써도 잔액은 변하지 않는다.
//
// 주소를 서버에 물어보고 쓰면 늦는다 — 왕복 수백 ms 사이에 아이는 키링을 치우고,
// PN532 는 카드가 선택된 상태라야 쓸 수 있다. 그래서 /config 로 표를 **미리** 받아
// 두었다가, 카드를 알아본 그 자리에서 바로 쓴다.
//
// CardEntry 와 같은 이유로 헤더에 둔다(자동 프로토타입보다 먼저 보여야 한다).
#define PASS_MAX 32
struct PassEntry {
  char uid[24];      // 카드 UID (16진 대문자). CardEntry 와 같은 크기로 맞춘다
  char token[16];    // 서버가 준 8자 토큰
  bool ok;           // 이 카드에 이미 써 있는 것을 확인했나(부팅 뒤 한 번만 읽어 본다)
};
