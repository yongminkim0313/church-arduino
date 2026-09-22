#pragma once
// nfcProject 설정 — 이 파일만 고치면 된다(코드는 건드리지 않는다).

// ── 와이파이 (우선순위 3개) ───────────────────────────────────────
// 0→1→2 순서대로 붙어 본다. 한 곳에 WIFI_TRY_MS 만큼 붙어 보고 안 되면 다음으로,
// 셋 다 안 되면 처음으로 돌아가 반복(루프). ChurchSecrets.h 의 값과 같다.
// ※ ESP32 는 2.4GHz 만 본다. "주향한교회_5G" 가 5GHz 전용이면 못 붙고 다음(2순위)으로 넘어간다.
//   교회에서 2.4GHz SSID 가 따로 있으면 1순위를 그걸로 바꾸는 게 빠르다.
#define WIFI_SSID_0 "주향한교회_5G"     // 1순위 — 교회(기기를 두는 곳)
#define WIFI_PASS_0 "ju123456"
#define WIFI_SSID_1 "3hans_home"         // 2순위
#define WIFI_PASS_1 "!Yehan1229"
#define WIFI_SSID_2 "용민 iPhone"        // 3순위
#define WIFI_PASS_2 "12345678"

// ── mDNS ──────────────────────────────────────────────────────────
// 붙은 뒤 http/ws 로 이 이름으로 찾을 수 있다: nfc-display.local
// (같은 망의 PC·폰에서 IP 를 몰라도 이 이름으로 접속된다)
#define MDNS_HOSTNAME "nfc-display"

// ── WebSocket 서버 포트 ───────────────────────────────────────────
// 외부(NFC 시스템)가 여기에 붙어 화면 상태를 보낸다: ws://nfc-display.local:81/
#define WS_PORT       81

// ── 펀펀포인트 서버 (사진 다운로드) ───────────────────────────────
// 화면을 길게 누르면 이 서버의 이름표(roster)를 받아, 사진이 있는 UID 마다
// 480 원형 사진(RGB565)을 내려받아 microSD 에 /talent/<UID>.565 로 저장한다.
// 그다음 NFC 태그가 오면 SD 에서 그 UID 사진을 읽어 화면에 띄운다.
//   운영: "https://youthvision.co.kr"   개발: "http://192.168.0.10:8100" (자기 PC IP)
#define TALENT_SERVER     "https://youthvision.co.kr"
#define TALENT_DEVICE_KEY "rOtbjceyN5kAXXDGz-cCfsv3knuNs0HA"   // 운영 서버(.env.docker)의 TALENT_DEVICE_KEY 와 일치
#define TALENT_PX         480                          // 원형 480 전체화면 사진(서버 DEVICE_SIZES 에 있어야)

// 이 기기의 이름. 관리자 화면 '리더 설정 → v2.0' 에서 이 ID 로 기기별 설정을 덮어쓸 수 있고,
// 포인트 내역에도 어느 기기에서 올라온 것인지 이 이름으로 남는다.
// 영문·숫자·_·- 만 쓴다(서버가 그 밖의 글자를 지운다).
#define TALENT_DEVICE_ID  "nfc-display"

// ── 설정 다시 받기 ────────────────────────────────────────────────
// 관리자 화면에서 밝기·출석모드를 바꾸면 이 주기로 기기에 반영된다.
// 부팅해서 와이파이에 붙는 순간에도 한 번 받는다. 시리얼 'c' 로 즉시 받을 수 있다.
#define CONFIG_TTL_MS     300000UL      // 5분

// ── 출석모드 ──────────────────────────────────────────────────────
// 같은 키링이 이 시간 안에 또 오면 출석으로 세지 않는다(화면에는 "이미 출석했어요").
// 줄을 선 아이가 두 번 대거나, 대고 있는 동안 리더가 여러 번 읽어도 포인트가 겹치지 않게.
// 기기에 시계(RTC)가 없어 "하루 한 번" 을 알 수 없다 — 시간 간격으로 막는다.
#define ATTEND_COOLDOWN_MS 600000UL     // 10분
#define ATTEND_RECENT_MAX  32           // 최근 출석한 키링을 이만큼 기억한다(넘으면 오래된 것부터 잊는다)

// ── microSD (SDMMC 1-bit) 핀 ──────────────────────────────────────
// LILYGO T-RGB 공식 핀맵 고정값. SD 는 SPI 가 아니라 SDMMC 1-bit 로 붙는다
// (신호 이름이 CLK/CMD/D0 인 이유). 다른 보드면 데이터시트대로 바꾸세요.
#define SD_CLK   39   // BOARD_SDMMC_SCK
#define SD_CMD   40   // BOARD_SDMMC_CMD
#define SD_D0    38   // BOARD_SDMMC_DAT
// SD 전원 인에이블은 ESP32 GPIO 가 아니라 XL9535 확장칩의 7번 핀이다(SD_CS/IO07 로 표기됨).
// SD_MMC.begin 전에 이 핀을 HIGH 로 켜야 카드가 잡힌다 — 코드가 화면 확장칩(bus)으로 켠다.
#define SD_EN_EXPANDER 7

// ── 길게 누르기 ────────────────────────────────────────────────────
// 이 시간 이상 누르고 있으면 '사진 동기화'(서버→SD). 짧게 누르면 다음 사진으로 순환.
#define LONG_PRESS_MS 1200
