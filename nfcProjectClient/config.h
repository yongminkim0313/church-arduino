#pragma once
// nfcProjectClient 설정 — 이 파일만 고치면 된다(코드는 건드리지 않는다).

// ── 와이파이 (우선순위 3개) — nfcProject/config.h 와 같은 값 ──────────
// 0→1→2 순서로 붙어 본다. 디스플레이와 **같은 망**에 붙어야 WebSocket 이 닿는다.
#define WIFI_SSID_0 "주향한교회_5G"     // 1순위 — 교회(기기를 두는 곳)
#define WIFI_PASS_0 "ju123456"
#define WIFI_SSID_1 "3hans_home"         // 2순위
#define WIFI_PASS_1 "!Yehan1229"
#define WIFI_SSID_2 "용민 iPhone"        // 3순위
#define WIFI_PASS_2 "12345678"

// ── 디스플레이 (nfcProject) ───────────────────────────────────────
// mDNS 이름으로 찾는다: nfc-display.local → IP. 못 찾으면 DISPLAY_IP 를 쓴다(비워 두면 계속 찾는다).
#define DISPLAY_HOST  "nfc-display"
#define DISPLAY_IP    ""               // 예: "192.168.0.9"  (mDNS 가 안 되는 공유기일 때)
#define DISPLAY_PORT  81

// ── 디스플레이의 답을 기다리는 시간 ───────────────────────────────
// UID 를 넘기면 디스플레이가 서버에 물어(출석모드면 지급까지 하고) 결과를 돌려준다.
// 이 시간 안에 답이 없으면 실패음을 낸다 — 소리가 없으면 자원봉사자는 못 읽은 줄 알고 또 댄다.
#define ACK_WAIT_MS 4000

// 펀펀포인트 서버 주소·기기 키는 이제 여기에 없다.
// 서버와 이야기하는 쪽은 디스플레이(nfcProject) 하나다 — 이 리더는 UID 만 넘긴다.

// ── PN532 (I2C) 배선 — 배선도(PN532 × ESP32-C3) 그대로 ──────────────
//   3V3→VCC · GPIO4→SDA · GPIO5→SCL · GPIO6→IRQ · GND→GND
//   GPIO8·9 는 부팅 스트래핑 핀이라 쓰지 않는다.
#define PIN_NFC_SDA  4
#define PIN_NFC_SCL  5
#define PIN_NFC_IRQ  6
#define NFC_USE_IRQ  0                 // 쓰는 모듈에 IRQ 핀이 없다 → 늘 폴링. 1 = 있으면 IRQ, 없으면 자동으로 폴링

// ── 부저 ──────────────────────────────────────────────────────────
// C3 에는 화면이 없어 소리로 알린다.  GPIO7 → 부저(+) · GND → 부저(−)
// GPIO2·8·9 는 스트래핑, 18·19 는 USB, 20·21 은 UART 라 피했다. -1 이면 소리를 끈다.
#define PIN_BUZZER    7
#define BUZZER_ACTIVE 0                // 0 = 수동(패시브) 부저 — 음 높이를 낸다 / 1 = 능동(액티브) 부저 — 켜고 끄기만

// ── 같은 카드 연속 처리 방지 ──────────────────────────────────────
// 카드를 대고 있는 동안 같은 UID 가 계속 읽힌다. 이 시간 안의 같은 UID 는 한 번으로 친다.
#define SAME_TAG_COOLDOWN_MS 3000
