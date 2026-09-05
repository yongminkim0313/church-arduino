#pragma once
#include <stdint.h>

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
