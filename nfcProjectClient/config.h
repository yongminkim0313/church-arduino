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

// ── PN532 (SPI) 배선 ────────────────────────────────────────────────
//   3V3→VCC · GND→GND
//   GPIO4→SCK · GPIO5→MISO · GPIO6→MOSI · GPIO7→SS(NSS)
//   **모듈 딥스위치를 SPI 로 놓아야 한다** (Elechouse V3 는 보통 1:OFF 2:ON —
//   보드 뒷면 표로 확인). I2C 로 두면 MISO 가 조용해 아무것도 안 읽힌다.
//
//   왜 I2C 가 아니라 SPI 인가 — 2026-09-24 실기에서 이 모듈의 I2C 쪽(SCL)이
//   고장 난 것을 확인했다. 같은 모듈이 SPI 로는 v1.6 으로 멀쩡히 응답하고
//   145초 소크에서 감시 실패 0·재초기화 0 이었다. 전원은 결백하다.
//
//   C3 기본 SPI 핀이 마침 4·5·6·7 이라 I2C 때 쓰던 GPIO4·5 를 그대로 쓴다.
//   GPIO8·9 는 부팅 스트래핑 핀이라 쓰지 않는다.
#define PIN_NFC_SCK   4
#define PIN_NFC_MISO  5
#define PIN_NFC_MOSI  6
#define PIN_NFC_SS    7

// ── 부저 ──────────────────────────────────────────────────────────
// C3 에는 화면이 없어 소리로 알린다.  GPIO10 → 부저(+) · GND → 부저(−)
// **GPIO7 에서 옮겨 왔다** — SPI 의 SS 가 GPIO7 을 쓰기 때문이다. 선을 옮겨 꽂아야 한다.
// GPIO2·8·9 는 스트래핑, 18·19 는 USB, 20·21 은 UART 라 피했다. -1 이면 소리를 끈다.
#define PIN_BUZZER   10
#define BUZZER_ACTIVE 0                // 0 = 수동(패시브) 부저 — 음 높이를 낸다 / 1 = 능동(액티브) 부저 — 켜고 끄기만

// ── 발열 줄이기 ───────────────────────────────────────────────────
// 셋 다 "조금 느려지는 대신 시원해지는" 맞바꿈이다. 더 시원하게 하려면 숫자를
// 키우고(POLL_EVERY_MS), 반응이 굼뜨면 줄인다. 시리얼 `cpu`·`poll`·`rty` 로
// 구운 채로도 바꿔 보고 정할 수 있다.
//
// 열이 어디서 나는지부터 보라 —
//   C3 가 뜨겁다     → WIFI_MODEM_SLEEP 이 가장 크다. 라디오가 늘 깨어 있으면 계속 먹는다
//   PN532 이 뜨겁다  → NFC_POLL_EVERY_MS 다. 훑는 순간마다 RF 필드를 켜서 전류를 쓴다
#define CPU_MHZ             80   // 160 → 80. USB CDC 가 도는 최저다. 그 아래는 시리얼이 끊긴다
#define NFC_POLL_EVERY_MS  120   // 폴링 사이 쉬는 시간. 0 이면 쉬지 않는다(예전 동작)
#define NFC_RETRIES       0x05   // 한 번 훑는 길이. 0x10 → 0x05 로 RF 켜는 시간을 줄인다
#define WIFI_MODEM_SLEEP     1   // 1 = 라디오를 재운다. 디스플레이 응답이 굼뜨면 0 으로

// ── 같은 카드 연속 처리 방지 ──────────────────────────────────────
// 카드를 대고 있는 동안 같은 UID 가 계속 읽힌다. 이 시간 안의 같은 UID 는 한 번으로 친다.
#define SAME_TAG_COOLDOWN_MS 3000
