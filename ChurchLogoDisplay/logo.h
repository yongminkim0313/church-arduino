#pragma once
// logo.h — 교회 로고 RGB565 이미지 헤더 (플레이스홀더)
//
// 이 파일은 아직 실제 로고 데이터를 담고 있지 않습니다(HAVE_LOGO_IMAGE 0).
// 이 상태에서는 스케치가 십자가 플레이스홀더를 그립니다.
//
// ▶ 실제 로고 넣는 법 (Floyd-Steinberg 디더링 → RGB565):
//     python3 ../tools/img2rgb565_dither.py church_logo.png \
//         --width 96 --height 96 --var logo_data -o logo.h
//   실행하면 이 파일이 아래처럼 채워지고 HAVE_LOGO_IMAGE 가 1 이 됩니다:
//
//     #define HAVE_LOGO_IMAGE 1
//     #define LOGO_W 96
//     #define LOGO_H 96
//     const uint16_t logo_data[96*96] PROGMEM = { 0x0000, ... };
//
//   스케치는 이 배열을 spr.pushImage(x, y, LOGO_W, LOGO_H, (uint16_t *)logo_data) 로 그립니다.

#define HAVE_LOGO_IMAGE 0
#define LOGO_W 96
#define LOGO_H 96

// HAVE_LOGO_IMAGE 가 0 이면 logo_data 는 사용되지 않습니다.
// (도구로 생성하면 이 아래에 실제 데이터가 들어갑니다.)
