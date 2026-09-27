#pragma once
// nfcProject 설정 — 이 파일만 고치면 된다(코드는 건드리지 않는다).

// ── 와이파이 (우선순위 4개) ───────────────────────────────────────
// 0→1→2→3 순서대로 붙어 본다. 한 곳에 WIFI_TRY_MS 만큼 붙어 보고 안 되면 다음으로,
// 넷 다 안 되면 처음으로 돌아가 반복(루프).
// ※ ESP32 는 2.4GHz 만 본다. "주향한교회_5G" 가 5GHz 전용이면 못 붙고 다음 순위로 넘어간다.
#define WIFI_SSID_0 "olleh_WiFi_1E5C"   // 1순위 — 기기를 두는 곳
#define WIFI_PASS_0 "0000000124"
#define WIFI_SSID_1 "주향한교회_5G"     // 2순위 — 교회
#define WIFI_PASS_1 "ju123456"
#define WIFI_SSID_2 "3hans_home"         // 3순위
#define WIFI_PASS_2 "!Yehan1229"
#define WIFI_SSID_3 "용민 iPhone"        // 4순위
#define WIFI_PASS_3 "12345678"

// ── mDNS ──────────────────────────────────────────────────────────
// 붙은 뒤 http/ws 로 이 이름으로 찾을 수 있다: nfc-display.local
// (같은 망의 PC·폰에서 IP 를 몰라도 이 이름으로 접속된다)
#define MDNS_HOSTNAME "nfc-display"

// ── WebSocket 서버 포트 ───────────────────────────────────────────
// 외부(NFC 시스템)가 여기에 붙어 화면 상태를 보낸다: ws://nfc-display.local:81/
#define WS_PORT       81

// ── 리더와 직접 잇는 선 (I2C 마스터) ──────────────────────────────
// 리더(nfcProjectClient, ESP32-C3)를 옆면 4핀 커넥터에 물린다. WebSocket 서버는 그대로
// 열어 둔다 — 브라우저·폰은 계속 ws 로 붙고, 리더만 선으로 온다(둘 다 받는다).
//
// **UART 가 아니라 I2C 인 이유** — 이 보드가 밖으로 내주는 건 옆면 1.25mm 4핀 커넥터
// 하나뿐이다. RGB 패널이 2·3·5·6·7·9~18·21·41·42·45·47 을, SD 가 38·39·40 을, 백라이트가
// 46 을, OPI PSRAM 이 35~37 을 먹는다. 43·44 는 커넥터에 나오지 않고(네이티브 USB 라
// UART 브리지가 없다), 1 은 터치 INT, 4 는 배터리 전압 측정이다. 남는 건 그 커넥터의
// GPIO8(SDA)·GPIO48(SCL) 뿐인데 이미 I2C 다 — 그래서 리더를 이 버스에 슬레이브로 붙인다.
//
//   4핀 커넥터            리더(C3)
//     GND   ───────────    GND          ← 필수
//     GPIO8 (SDA) ─────    GPIO20 (SDA)
//     GPIO48(SCL) ─────    GPIO21 (SCL)
//     3V3   ── 잇지 마라 ─ (리더는 자기 USB 로 먹인다 — 두 레귤레이터를 맞물리지 않는다)
//
// 이 버스에는 터치(0x15)와 화면 확장칩 XL9535(0x20)가 이미 붙어 있다. 리더는 0x30 이라
// 겹치지 않는다. 풀업은 이 보드에 이미 있다.
//
// 슬레이브는 먼저 말을 걸 수 없으니 **이쪽이 마스터로 물어본다** — LINK_POLL_MS 마다
// "줄 것 있나" 하고 읽어 오고, 결과(이름·잔액·출석)는 이쪽이 써 넣어 준다.
#define LINK_I2C_ENABLE  1      // 0 = 선을 안 쓴다(WebSocket 만)
#define LINK_I2C_ADDR 0x30      // 리더의 주소(리더 config.h 의 LINK_I2C_ADDR 과 같아야 한다)
#define LINK_POLL_MS    50      // 리더에게 "줄 것 있나" 를 묻는 주기
#define LINK_ALIVE_MS 3000      // 이 시간 안에 리더가 답했으면 선이 살아 있다고 본다

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
