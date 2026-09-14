// TalentNfcReader — ESP32-S3 NFC 키링 리더 (펀펀포인트)
//
// NTAG-213 키링을 PN532(I2C)로 읽어 2.8" ILI9341 TFT 에 잔액을 보여준다.
// 입력은 디스플레이에 붙은 XPT2046 터치 패널 하나뿐이다 — 화면 위쪽의
// 지급·사용·내역 세 탭을 손으로 눌러 고른다.
//
// 지급이냐 사용이냐는 **카드가 정한다**. 화면에서 고르지 않는다 —
// 지급 카드를 대면 지급, 사용 카드를 대면 사용이다.
//
// 예전에는 위쪽에 지급·사용 탭이 있었다. 그런데 대는 카드에 이미 방향이 적혀 있어,
// 탭은 "카드와 맞는 쪽을 미리 골라 두는" 일만 했다. 어긋나면 "사용 카드입니다" 로
// 되돌려 보냈는데, 아이 입장에서는 맞는 카드를 댔는데 안 되는 화면이었다.
// 탭을 걷어내니 고를 것이 없어지고, 화면은 78px 을 되찾았다.
//
// 부호(+/-)는 쓰지 않는다. 처리를 마치면 테두리와 숫자 색이 방향을 말한다
// (초록 = 지급, 빨강 = 사용). 고르기 전에는 방향이 없으므로 테두리도 회색이다.
//
// 포인트 잔액은 youthvision.co.kr 의 yvServer(/api/talent)가 관리한다.
// (같은 API 가 jesusdream.kr 에도 있다. 접속 주소는 ChurchSecrets.h 의 TALENT_API_BASE 하나로 바꾼다)
// 리더는 저장하지 않고 매번 서버에 묻는다 — 리더가 여러 대여도 잔액이 하나로 유지된다.
//
// ── 조작 ──────────────────────────────────────────────────────────
//   헤더 띠 터치      : 전체 내역 보기 ↔ 대기 화면
//   키링 태깅         : 이름·잔액과 쓸 수 있는 카드 목록을 띄우고 카드를 기다린다
//   카드 태깅         : 그 카드대로 지급/사용하고 결과를 표시
//                       (예: "김용민 / 아이스크림 15 포인트 사용 / 남은 포인트 11")
//   내역에서 태깅     : 잔액을 건드리지 않고 그 사람의 남은 포인트와 최근 4건을 보여준다
//   내역 위·아래 띠   : 위 = 이전 페이지(더 최근), 아래 = 다음 페이지(더 예전)
//   무입력            : sleepEnabled 가 켜진 기기만 딥슬립한다. 기본은 꺼짐 —
//                       상시 전원으로 세워 두는 기기가 대부분이고, 화면이 꺼지면
//                       고장으로 오해받는다.
//   끊김일 때 헤더 터치: 블루투스 와이파이 설정 화면 (아래 참고)
//
// ── 와이파이가 안 될 때 ───────────────────────────────────────────
// 공유기를 바꾸거나 비밀번호가 달라지면 기기가 먹통이 된다. 예전에는 그때마다
// ChurchSecrets.h 를 고쳐 다시 구워야 했다 — 현장에 노트북과 케이블을 들고 가야 한다.
// 이제는 못 붙으면 스스로 블루투스를 열고, 휴대폰에서 이름과 비밀번호를 넣어 준다.
// 받은 것은 NVS 에 저장해 다음 부팅부터 먼저 쓴다(자세한 것은 '블루투스 설정' 절).
//
// ── 빌드 (TFT_eSPI 설정을 이 스케치에만 적용) ──────────────────────
// TFT_eSPI 는 라이브러리 전역 설정(User_Setup_Select.h)을 쓰기 때문에, 그대로 두면
// 같은 PC 의 TTGO T-Display 스케치들과 설정이 충돌한다. 그래서 라이브러리를 건드리지
// 않고 빌드 플래그로 이 스케치에만 설정을 주입한다. build.sh 를 쓰면 된다.
//
//   ./TalentNfcReader/build.sh              # 컴파일
//   ./TalentNfcReader/build.sh --upload     # 컴파일 + 업로드
//
// 필요 라이브러리: TFT_eSPI, Adafruit PN532 (+ Adafruit BusIO), NimBLE-Arduino 2.x
// 터치는 TFT_eSPI 가 XPT2046 을 직접 다룬다 — 별도 라이브러리가 필요 없고
// build.sh 의 -DTOUCH_CS=15 하나로 켜진다. SPI 는 화면과 같은 버스를 쓴다.

#include <TFT_eSPI.h>
#include "FontKR14.h"
#include "FontKR20.h"
#include "FunFunLogo.h"
#include "TalentTypes.h"
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>    // 와이파이가 안 될 때 블루투스로 설정을 받는다
#include <ChurchSecrets.h>   // WiFi/서버 인증정보 (저장소 밖)
#include <Preferences.h>     // 설정 캐시(NVS)
#include <esp_sleep.h>
#include <LittleFS.h>      // 서버에서 받은 포인트 그림을 담아 둔다

// ── 핀 (핀맵은 여기 한 곳에서만 관리한다) ─────────────────────────
// 디스플레이 SPI 핀은 TFT_eSPI 가 빌드 플래그로 받는다(build.sh 참고).
// 여기서는 라이브러리가 직접 다루지 않는 핀만 정의한다.
//
// ── 왜 이런 번호인가 ──
// GPIO 번호는 순서가 없어 보이지만, ESP32-S3-DevKitC 왼쪽 헤더에 꽂는 자리는
// TFT 헤더의 신호 순서와 그대로 맞는다. 손배선에서 중요한 것은 번호가 아니라
// 구멍의 순서다 — 옆으로 나란히 가면 선이 안 꼬인다.
//
//   보드 왼쪽 헤더(위→아래)        TFT 14핀 헤더(위→아래)
//    4번째  GPIO4   ────────────   1  T_IRQ
//    5번째  GPIO5   ────────────   4  T_CS
//                                  2  T_DO  ┐ 모듈 쪽에서 짧게 물린다
//                                  3  T_DIN ┤ (기판 안에서 이어져 있지 않다)
//                                  5  T_CLK ┘
//    6번째  GPIO6   ────────────   6  SDO(MISO)
//    7번째  GPIO7   ────────────   7  LED
//    8번째  GPIO15  ────────────   8  SCK
//    9번째  GPIO16  ────────────   9  SDI(MOSI)
//   10번째  GPIO17  ────────────  10  DC
//   11번째  GPIO18  ────────────  11  RESET
//   12번째  GPIO8   ────────────  12  CS
//                                 13  GND → 헤더 맨 아래 GND
//                                 14  VCC → 헤더 맨 위 3V3
//
// 13·14번째 구멍(GPIO3·GPIO46)은 스트래핑 핀이라 건너뛰고, 그 아래 15~17번째를
// PN532 와 부저에 준다. 회피: 0·3·45·46(스트래핑), 19·20(USB), 43·44(UART0).
#define PIN_TFT_BL     7    // 백라이트 (PWM) — TFT 7번 LED
#define PIN_NFC_SDA    9    // 헤더 15번째
#define PIN_NFC_SCL   10    // 헤더 16번째
#define PIN_BUZZER    11    // 패시브 부저 — 헤더 17번째
// 터치 패널(XPT2046)의 T_CS 는 TFT_eSPI 가 잡는다 — build.sh 의 -DTOUCH_CS=5.
// T_IRQ 는 폴링으로 쓰므로 평소에는 연결하지 않아도 된다. 다만 딥슬립에서
// 화면 터치로 깨우려면 RTC 핀이라야 해서, RTC 인 GPIO4 에 물리도록 해 뒀다.
#define PIN_TOUCH_IRQ  4    // XPT2046 T_IRQ (딥슬립 기상용, ext0 · RTC 핀)

// ── 동작 설정 ─────────────────────────────────────────────────────
// 아래 값들은 서버(GET /api/talent/config)에서 내려받아 덮어쓴다.
// 여기 기본값은 yvServer/talent/talentConfig.js 의 기본값과 같아야 한다 —
// 서버에 못 붙어도 리더가 그대로 동작하게 하는 것이 목적이다.
#define DEF_SLEEP_ENABLED     false   // 딥슬립을 쓸지 — 기본은 끔(상시 전원 기기)
#define DEF_SLEEP_TIMEOUT_MS  15000   // 무입력 후 딥슬립까지 (sleepEnabled 일 때만)
#define DEF_TOUCH_DEBOUNCE_MS   400   // 터치 연속 인식 방지
#define DEF_TAG_COOLDOWN_MS    1500   // 같은 태그 연속 처리 방지
#define DEF_ROTATION              0   // 거치 방향
#define DEF_BACKLIGHT           255   // 백라이트 밝기
#define DEF_SOUND              true
#define DEF_LABEL      "펀펀포인트"   // 헤더의 로고 옆 제목
#define DEF_BG_COLOR   "#EBAC42"      // 내용 영역 바탕색 (#RRGGBB)
#define DEF_CONFIG_TTL_SEC      300   // 깨어 있는 동안 설정 재조회 주기
#define DEF_ATTENDANCE_MODE   false   // 출석모드 — 키링 한 번으로 바로 지급
#define DEF_DONE_MS            3000   // 완료 그림을 띄워 두는 시간

// 터치 패널 보정값. 2026-09-07 에 실제 패널에서 뽑은 값이다(그 전에는 임시값이었다).
//
// 패널마다 다르므로 모듈을 바꾸면 다시 뽑아야 한다 — 그대로 두면 탭이 눌리는 자리가
// 어긋난다. 다섯째 값은 회전·축 플래그라 rotation 이 바뀌어도 다시 재야 한다.
// 뽑는 법: TFT_eSPI 의 Touch_calibrate 예제(또는 같은 일을 하는 짧은 스케치)를
// **이 펌웨어와 같은 빌드 플래그·같은 setRotation(DEF_ROTATION)** 으로 굽고
// 네 모서리를 눌러 시리얼에 찍히는 다섯 값을 여기에 옮긴다.
static const uint16_t TOUCH_CAL[5] = { 444, 3198, 407, 3328, 4 };

// 한 번 태깅에 오르내리는 달란트
#define DEF_TALENT_STEP 1

// ── 달란트 저장 위치 ──────────────────────────────────────────────
// 서버(yvServer /api/talent)가 단일 출처다. 리더는 캐시하지 않는다.
//   · 리더를 여러 대 놓아도 잔액이 하나로 모인다
//   · 지급/사용 내역이 서버에 남아 "왜 이 잔액인지" 설명할 수 있다
//   · 대신 네트워크가 끊기면 처리할 수 없다 — 이때는 화면과 소리로 실패를 알린다
#define DEF_HTTP_TIMEOUT_MS 6000

// ══════════════════════════════════════════════════════════════════
//  설정 — 서버에서 받아 NVS 에 캐시한다
// ══════════════════════════════════════════════════════════════════
// 딥슬립에서 깨어날 때마다 setup() 이 통째로 도는 기기라, 서버 응답을 기다렸다가
// 화면을 그리면 터치 후 반응이 눈에 띄게 늦어진다. 그래서 캐시로 먼저 그리고
// 설정 갱신은 그 뒤에 한다. 서버에 못 붙으면 캐시(없으면 기본값)로 계속 간다.

// ── 메뉴 ──────────────────────────────────────────────────────────
// 항목별 포인트. 서버가 이름을 5자, 금액을 999 까지로 조여서 준다 —
// 화면 오른쪽 108px 칸에 "아이스크림 999" 가 들어가는 한계가 그 근거다.
// 이름은 UTF-8 이라 5자 = 15바이트, 여유를 두어 24바이트로 잡는다.
#define MENU_MAX 8
struct MenuItem {
  char    name[24] = "";
  int32_t amount   = 0;
  // 항목에 딸린 그림. 서버가 설정 응답의 항목에 바로 붙여 준다(없으면 빈 문자열).
  // 그림을 목록이 아니라 항목에 붙이는 이유는 TalentManager.js 의 주석을 볼 것.
  char    img[36]  = "";     // "menu-<탭>-<이름해시8>-<내용해시8>.565"
  int16_t imgW     = 0;
  int16_t imgH     = 0;
};
struct Menu {
  MenuItem item[MENU_MAX];
  uint8_t  count = 0;
};

struct Config {
  Menu     earnMenu;                                // 지급 탭 오른쪽 목록
  Menu     spendMenu;                               // 사용 탭 오른쪽 목록
  int32_t  talentStep      = DEF_TALENT_STEP;       // 카드가 없던 시절의 기본 증감폭
  bool     sleepEnabled    = DEF_SLEEP_ENABLED;
  uint32_t sleepTimeoutMs  = DEF_SLEEP_TIMEOUT_MS;
  uint32_t tagCooldownMs   = DEF_TAG_COOLDOWN_MS;
  uint32_t touchDebounceMs = DEF_TOUCH_DEBOUNCE_MS;
  uint32_t httpTimeoutMs   = DEF_HTTP_TIMEOUT_MS;
  uint8_t  rotation        = DEF_ROTATION;
  uint8_t  backlight       = DEF_BACKLIGHT;
  bool     sound           = DEF_SOUND;
  uint32_t ttlSec          = DEF_CONFIG_TTL_SEC;
  bool     attendanceMode  = DEF_ATTENDANCE_MODE;   // 키링 한 번으로 바로 지급
  char     attendanceCard[24] = "";                 // 그때 쓸 카드 UID
  uint32_t doneMs          = DEF_DONE_MS;
  char     label[40]       = DEF_LABEL;             // 한글은 UTF-8 이라 넉넉히
  char     bgColor[8]      = DEF_BG_COLOR;         // "#RRGGBB"
};

static Config     cfg;
static Preferences prefs;
static uint32_t   lastCfgFetch = 0;
// 직전에 붙어 있었는지. 끊김↔붙음이 바뀌는 순간에만 헤더를 갈아 끼우고,
// 다시 붙었으면 서버에서 받아 올 것들을 한 번 받는다(매 루프 확인하면 화면이 깜빡인다).
static bool       wifiWasOnline = false;
static bool       cfgFromServer = false;

TFT_eSPI tft = TFT_eSPI();

// TFT_eSPI 내장 폰트는 ASCII 전용이라 한글이 깨진다. KS X 1001 상용 2350자를
// 14px 스무스폰트(VLW)로 구워 넣었다(tools/make-fonts.sh). 서버에서 내려주는
// deviceLabel 이 무엇이든 나오게 하려면 낱말 서브셋으로는 부족하다.
// 숫자는 큼직하게 보여야 해서
// 내장 폰트를 쓰는데, 스무스폰트가 올라가 있으면 폰트 번호가 무시되므로
// 그 구간만 잠깐 내렸다가 다시 올린다.
// VLW 스무스폰트는 구워 넣은 크기로만 그려진다 — setTextSize 가 먹지 않아
// 크기별로 한 벌씩 든다. 14px 은 본문·목록, 20px 은 멀리서 읽혀야 하는 줄
// (이름·탭·안내문)에 쓴다. 숫자는 내장 폰트 4·6번이라 폰트를 내려야 한다(0).
//
// loadFont 는 글리프 메트릭 2,445개를 힙에 올린다(약 20KB). 같은 폰트를 다시
// 올리지 않게 막아 두지 않으면 화면 한 장 그리는 동안 열 번 넘게 재할당된다.
static uint8_t curFont = 0;
static void useFont(uint8_t px) {
  if (curFont == px) return;
  if (px == 0)       { tft.unloadFont(); curFont = 0; return; }
  if (px >= 20)      { tft.loadFont(FontKR20); curFont = 20; }
  else               { tft.loadFont(FontKR14); curFont = 14; }
}
// PN532 는 I2C 로만 쓴다. 첫 두 인자는 SDA/SCL 이 아니라 IRQ/RESET 이며,
// 이 회로에서는 둘 다 연결하지 않으므로 -1 을 준다.
// (-1 이면 라이브러리가 reset 펄스를 건너뛰고, 준비 여부는 I2C RDY 바이트로 확인한다)
// SDA/SCL 은 아래 Wire.begin(SDA, SCL) 로 지정한다.
Adafruit_PN532 nfc(-1, -1, &Wire);

// ── 진행 단계 ─────────────────────────────────────────────────────
// 예전에는 키링 한 번으로 곧바로 처리했다. 그러면 "누구에게 무엇을" 을 확인할
// 틈이 없어, 잘못 댄 것을 되돌리려면 관리자가 화면에서 손봐야 했다.
//
//   대기 ── 키링 ──▶ 카드 대기(이름·잔액·카드 목록) ── 카드 ──▶ 완료(그림)
//                     │                                              │
//                    취소 ──▶ 대기 ◀── doneMs 뒤 ────────────────────┘
//
// 확인 단추는 없앴다. 누를 것이 하나 줄면 줄이 그만큼 빨리 빠지고, 확인할 것(누구에게)은
// 이름과 잔액이 카드 대기 화면에 떠 있어 카드를 대기 전에 눈으로 이미 본다.
// 잘못 댄 키링은 '취소' 로 물린다 — 되돌릴 자리는 그대로 남겨 둔 셈이다.
// 카드를 대면 곧바로 처리한다. 무엇을 얼마나는 카드에 적혀 있어 고를 여지가 없다.
//
// 출석모드면 키링을 대는 순간 카드 단계까지 건너뛴다(설정으로 정한 카드).
enum Step : uint8_t {
  STEP_IDLE,      // 키링을 기다린다
  STEP_CARD,      // 누구인지 보여주며 지급/사용 카드를 기다린다
  STEP_DONE,      // 완료 그림
};

// 지금 다루고 있는 사람과 카드. 단계가 대기로 돌아갈 때 함께 비운다.
static Step     step         = STEP_IDLE;
static uint32_t stepAt       = 0;    // 이 단계에 들어온 시각(자동 넘김·시간초과에 쓴다)
static char     curUid[24]   = "";
static char     curName[24]  = "";
static int32_t  curBalance   = 0;
// 잔액을 서버에서 받았나. 이름표에 있는 키링은 이름·사진을 먼저 띄우고 잔액은 곧이어 채운다(handleTag).
// 받기 전에는 잔액 자리에 '…' 를 두고, 모자람 흐림도 하지 않는다.
static bool     curBalanceKnown = true;
static int8_t   curCard      = -1;   // cards[] 의 자리. -1 이면 아직 안 골랐다
static int32_t  doneDelta    = 0;    // 완료 화면에 띄울 값
static int32_t  doneBalance  = 0;

// ── 상태 ──────────────────────────────────────────────────────────
// 화면은 둘뿐이다 — 평소(대기·카드·완료)와 내역 보기.
// (enum Tab 은 TalentTypes.h — 자동 프로토타입보다 먼저 보여야 한다)
static Tab      tab          = TAB_MAIN;
static uint32_t lastActivity = 0;
static uint32_t lastTouchMs  = 0;
static uint32_t lastTagMs    = 0;
static char     lastUid[24]  = "";
static bool     nfcReady     = false;

// ── 서버에서 받은 포인트 그림 ─────────────────────────────────────
// 예전에는 그림을 펌웨어에 구워 넣어, 새 그림을 쓰려면 기기마다 다시 올려야 했다.
// 이제 관리자가 올린 것을 부팅할 때 받아 LittleFS 에 둔다.
//
// 파일 이름이 곧 해시라("10-a1b2c3d4.565") 있으면 최신이다 — 따로 비교하지 않는다.
// 그림이 바뀌면 이름이 달라져 저절로 다시 받고, 옛 파일은 목록에 없으니 지운다.
// 파티션표를 default_16MB 로 바꿔 LittleFS 가 896KB → 3.375MB 가 됐다(build.sh).
// 포인트 그림 한 장이 26KB 남짓이라 이 정도는 넉넉히 들어간다.
#define ART_MAX 12
#define ART_DIR "/art"
// (struct ArtFile 은 TalentTypes.h — 자동 프로토타입보다 먼저 보여야 한다)
static ArtFile artFile[ART_MAX];
static uint8_t artCount = 0;

// 내용 영역 배경. 탭마다 한 장(지급·사용)이고, 없으면 지금처럼 검정으로 채운다.
// base 는 안 쓰고 탭 구분에만 쓴다 — 0=지급, 1=사용.
static ArtFile bgFile[2];
static bool    bgHas[2] = { false, false };

// 켤 때 뜨는 화면. 화면 전체(240x320)를 덮는다.
// 파일 이름을 NVS 에 함께 저장한다 — 켜자마자, 서버에 붙기 전에 그려야 하기 때문이다.
// 그래서 처음 켤 때는 아직 없고, 한 번 받아 둔 다음부터 나온다.
static ArtFile splashFile;
static bool    splashHas = false;

// 헤더 띠 그림. 연결됨·끊김 두 장이 다 있어야 쓴다 — 한 장만 있으면 끊겼을 때
// 화면이 비어 무슨 일인지 알 수 없다. 232x42 를 꽉 채운 그림이다.
static ArtFile headOnFile, headOffFile;
static bool    headHasFile = false;

// 처리를 마쳤을 때 띄우는 그림. 지급·사용 한 장씩(0=지급, 1=사용).
// 글자를 거의 안 쓰고 그림으로 말하는 자리라 포인트 그림보다 크게 받는다.
static ArtFile doneFile[2];
static bool    doneHas[2] = { false, false };

// ── 지급/사용 카드 ────────────────────────────────────────────────
// "무엇을 얼마나" 를 담은 NFC 카드다. 어린이 키링이 "누구" 인 것과 짝을 이룬다.
// 부팅할 때 GET /api/talent/cards 로 통째로 받아 두고, 카드를 대면 여기서 찾는다.
// 목록에 없으면 미등록 — 서버에 알리기만 하고(POST /seen) 아무것도 처리하지 않는다.
//
// 금액을 여기 들고 있는 것은 화면에 미리 보여주기 위해서다. 실제로 오르내리는 양은
// 서버가 카드 UID 로 다시 찾아 정한다 — 리더가 보낸 숫자를 믿지 않는다.
// (struct CardEntry 는 TalentTypes.h — 자동 프로토타입보다 먼저 보여야 한다)
static CardEntry cards[CARD_MAX];
static uint8_t   cardCount = 0;

// ── 모바일 조회 링크 ──────────────────────────────────────────────
// 서버가 /config 로 미리 준다. 태깅한 그 자리에서 카드에 써 넣으려면 주소를
// 이미 알고 있어야 한다 — 서버에 묻고 오면 카드는 벌써 떠난 뒤다.
static PassEntry passes[PASS_MAX];
static uint8_t   passCount  = 0;
static char      passBase[48] = "";     // "youthvision.co.kr/t/" (스킴은 아래 코드로 줄여 담는다)
static uint8_t   passUriId  = 0x04;     // NDEF URI 코드 (0x04 = "https://")

// ── 이름표 ────────────────────────────────────────────────────────
// 부팅할 때 GET /api/talent/roster 로 받아 둔다. 태그를 대는 순간 이름을
// 띄우려면 서버 응답을 기다릴 수 없어서다(왕복이 1초 가까이 걸린다).
#define ROSTER_MAX 200
// img — 그 아이 사진 파일 이름 "ph-<24자>-48.565"(34자). 사진이 없으면 빈 값.
struct RosterEntry { char uid[16]; char name[24]; char img[36]; };
static RosterEntry roster[ROSTER_MAX];
static uint16_t    rosterCount = 0;

// ── 아이 사진 ──────────────────────────────────────────────────────
// 키링을 대는 순간 이름 옆에 사진을 띄운다. 그때 서버에 물으면 늦으므로(이름과 같은 이유)
// 이름표에 딸려 오는 사진 이름을 보고 **미리** 받아 LittleFS 에 둔다. 파일 이름에 사진마다
// 다른 무작위 값이 들어 있어 "있다 = 최신" 이다 — 아이나 선생님이 사진을 바꾸면 이름이
// 바뀌어 새로 받고, 옛 파일은 artPrune 이 지운다.
//
// 한꺼번에 받지 않는다. 오십 명이면 수십 초가 걸리고 그동안 태깅이 굳는다. 대기 화면에서
// 조용할 때 한 장씩 받는다(photoSyncStep) — 한 장이 208x208x2 = 86KB, 오십 명이면 4.3MB 로 LittleFS(11.8MB)에 들어간다.
// 아직 못 받은 아이는 사진 없이 이름만 뜬다(글자만 있는 모양).
//
// 208 은 화면 내용 폭(232)보다 조금 작게 잡은 크기다. 키링을 대면 사진을 크게 띄우고 이름·잔액을
// 그 위에 겹친다(drawWaitCard). 서버는 이름표를 받을 때 ?px=208 이라야 이 크기의 이름을 준다 —
// 없으면 옛 펌웨어용 48px 이름을 준다.
#define PHOTO_PX 208              // 서버(talentPhoto.DEVICE_SIZES)에 있는 크기라야 한다 — 파일 이름에도 들어간다
static uint16_t photoCursor = 0;  // 다음에 확인할 이름표 자리
static uint32_t photoNextMs = 0;  // 다음 한 장을 받아도 되는 시각
// 이번에 이름표를 제대로 받았는가. 못 받았으면(부팅 때 서버가 꺼져 있었다든지) 이름표가 비어
// 있어서, 사진 파일을 "목록에 없음" 으로 보고 모두 지워 버리게 된다 — 그때는 지우지 않는다.
static bool     rosterFresh = false;

// ── 내역 탭 ───────────────────────────────────────────────────────
// 위·아래 페이지 띠를 빼면 여섯 줄쯤 들어간다. 더 받아 봐야 못 그리므로 열 건만 든다.
// 한 화면에 들어가는 줄 수만큼만 달라고 하고(feedRows), 서버는 page 0 을 가장
// 최근으로 두고 한 묶음씩 준다 — 위 화살표로 최근 쪽, 아래 화살표로 예전 쪽으로 넘긴다.
#define FEED_MAX 10
struct FeedEntry { char who[24]; int32_t delta; uint32_t agoSec; };
static FeedEntry feed[FEED_MAX];
static uint8_t   feedCount   = 0;
static uint32_t  feedFetchMs = 0;   // 받은 시점 — agoSec 에 더해 경과를 센다
static bool      feedOk      = false;
static uint8_t   feedPage    = 0;      // 0 이 가장 최근 묶음
static bool      feedMore    = false;  // 더 예전 것이 남아 있는가(서버가 알려 준다)

// ── 내역 탭에서 키링을 댔을 때 ────────────────────────────────────
// 지급도 사용도 하지 않고 "지금 얼마 있는지" 만 확인하는 자리다.
// 그 사람의 잔액과 최근 몇 건을 함께 담아 둔다.
#define WHO_MAX 4
static FeedEntry whoFeed[WHO_MAX];
static uint8_t   whoCount    = 0;
static char      whoUid[24]  = "";
static char      whoName[24] = "";
static int32_t   whoBalance  = 0;
static bool      whoKnown    = false;   // 서버가 아는 키링인가
static uint32_t  whoFetchMs  = 0;       // 받은 시점 — agoSec 에 더해 경과를 센다

// UID 로 이름을 찾는다. 없으면 UID 를 그대로 쓴다(이름을 안 붙인 키링).
static const char* nameOf(const char* uid) {
  for (uint16_t i = 0; i < rosterCount; i++)
    if (!strcmp(roster[i].uid, uid)) return roster[i].name;
  return uid;
}

// 이름표에 있는 키링인가 — 있으면 서버 응답을 기다리지 않고 이름부터 띄운다(handleTag)
static bool rosterHas(const char* uid) {
  for (uint16_t i = 0; i < rosterCount; i++)
    if (!strcmp(roster[i].uid, uid)) return true;
  return false;
}

// UID 의 사진 파일 이름. 이름표에 없거나 사진이 없으면 nullptr.
static const char* photoOf(const char* uid) {
  for (uint16_t i = 0; i < rosterCount; i++)
    if (!strcmp(roster[i].uid, uid)) return roster[i].img[0] ? roster[i].img : nullptr;
  return nullptr;
}

// 이 파일 이름이 지금 이름표의 사진인가(artPrune 이 지워도 되는지 가를 때 쓴다).
static bool photoInRoster(const char* name) {
  for (uint16_t i = 0; i < rosterCount; i++)
    if (roster[i].img[0] && !strcmp(roster[i].img, name)) return true;
  return false;
}

// 서버가 이미 범위를 조여서 주지만, 기기에서도 한 번 더 조인다.
// 응답이 어긋났을 때 화면이 꺼지거나(밝기 0) 너무 빨리 잠드는(슬립 1초) 상태로
// 빠지면 현장에서 손댈 방법이 없기 때문이다.
static void cfgClamp() {
  cfg.talentStep      = clampT<int32_t>(cfg.talentStep,           1,   1000);
  cfg.sleepTimeoutMs  = clampT<uint32_t>(cfg.sleepTimeoutMs,   5000, 600000);
  cfg.tagCooldownMs   = clampT<uint32_t>(cfg.tagCooldownMs,     200,  10000);
  cfg.touchDebounceMs = clampT<uint32_t>(cfg.touchDebounceMs,   100,   3000);
  cfg.httpTimeoutMs   = clampT<uint32_t>(cfg.httpTimeoutMs,    1000,  20000);
  cfg.rotation        = clampT<uint8_t>(cfg.rotation,             0,      3);
  cfg.backlight       = clampT<uint8_t>(cfg.backlight,           10,    255);
  cfg.ttlSec          = clampT<uint32_t>(cfg.ttlSec,             30,  86400);
  cfg.doneMs          = clampT<uint32_t>(cfg.doneMs,           1000,  15000);
  if (!cfg.label[0]) strlcpy(cfg.label, DEF_LABEL, sizeof(cfg.label));
  // "#RRGGBB" 가 아니면 기본색으로 돌린다. 잘못된 값이 오면 화면이 검게 칠해져
  // 고장으로 보이는데, 현장에서 그 원인을 짚기 어렵다.
  if (strlen(cfg.bgColor) != 7 || cfg.bgColor[0] != '#')
    strlcpy(cfg.bgColor, DEF_BG_COLOR, sizeof(cfg.bgColor));
}

// ── NVS 캐시 ──
// 블롭이 아니라 키별로 저장한다. 나중에 항목이 늘어도 예전 캐시를 그냥 쓸 수 있다.
// 메뉴는 스칼라가 아니라 통째로 넣고 뺀다. 항목 수가 바뀌어도 예전 캐시를 쓸 수 있게
// count 까지 포함한 구조체를 그대로 저장한다(크기가 다르면 읽기를 건너뛴다).
static void menuLoad(const char* key, Menu& out) {
  size_t n = prefs.getBytesLength(key);
  if (n == sizeof(Menu)) prefs.getBytes(key, &out, sizeof(Menu));
  if (out.count > MENU_MAX) out.count = 0;          // 캐시가 깨졌으면 버린다
}
static void menuSave(const char* key, const Menu& m) { prefs.putBytes(key, &m, sizeof(Menu)); }

static void cfgLoadCache() {
  if (!prefs.begin("talentcfg", true)) return;      // 아직 저장된 적 없음 → 기본값
  cfg.talentStep      = prefs.getInt ("step",   cfg.talentStep);
  cfg.sleepEnabled    = prefs.getBool("sleepOn", cfg.sleepEnabled);
  cfg.sleepTimeoutMs  = prefs.getUInt("sleepMs", cfg.sleepTimeoutMs);
  cfg.tagCooldownMs   = prefs.getUInt("coolMs",  cfg.tagCooldownMs);
  cfg.touchDebounceMs = prefs.getUInt("debMs",   cfg.touchDebounceMs);
  cfg.httpTimeoutMs   = prefs.getUInt("httpMs",  cfg.httpTimeoutMs);
  cfg.rotation        = prefs.getUChar("rot",    cfg.rotation);
  cfg.backlight       = prefs.getUChar("bl",     cfg.backlight);
  cfg.sound           = prefs.getBool("snd",     cfg.sound);
  cfg.ttlSec          = prefs.getUInt("ttl",     cfg.ttlSec);
  cfg.attendanceMode  = prefs.getBool("att",     cfg.attendanceMode);
  cfg.doneMs          = prefs.getUInt("doneMs",  cfg.doneMs);
  prefs.getString("attCard", cfg.attendanceCard, sizeof(cfg.attendanceCard));
  prefs.getString("label", cfg.label, sizeof(cfg.label));
  prefs.getString("bgcol", cfg.bgColor, sizeof(cfg.bgColor));
  prefs.getString("splash", splashFile.file, sizeof(splashFile.file));
  splashFile.w = prefs.getUShort("splashW", 0);
  splashFile.h = prefs.getUShort("splashH", 0);
  splashHas = splashFile.file[0] && splashFile.w > 0 && splashFile.h > 0;
  menuLoad("mEarn",  cfg.earnMenu);
  menuLoad("mSpend", cfg.spendMenu);
  prefs.end();
  cfgClamp();
}

// ── 와이파이 인증정보 ──
// 세 군데서 온다. 먼저 NVS 에 저장된 것(블루투스로 받아 둔 것), 없으면
// ChurchSecrets.h 에 구워 넣은 것. 둘 다 안 되면 블루투스 설정 모드로 간다.
//
// 구워 넣은 값을 지우지 않는 이유: 교회 공유기가 그대로인 기기는 손댈 일이 없어야 하고,
// NVS 를 지워도 늘 돌아갈 자리가 하나는 남아야 한다.
static char wifiSsid[33] = "";      // 저장된 것. 비어 있으면 구워 넣은 것을 쓴다
static char wifiPass[64] = "";

static void wifiLoadSaved() {
  if (!prefs.begin("talentwifi", true)) return;     // 저장된 적 없음
  prefs.getString("ssid", wifiSsid, sizeof(wifiSsid));
  prefs.getString("pass", wifiPass, sizeof(wifiPass));
  prefs.end();
  if (wifiSsid[0]) Serial.printf("[와이파이] 저장된 설정 %s\n", wifiSsid);
}

static void wifiSaveCreds(const char* ssid, const char* pass) {
  if (!prefs.begin("talentwifi", false)) { Serial.println("[와이파이] NVS 열기 실패"); return; }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  strlcpy(wifiSsid, ssid, sizeof(wifiSsid));
  strlcpy(wifiPass, pass, sizeof(wifiPass));
  Serial.printf("[와이파이] 저장했습니다 — %s\n", ssid);
}

static void wifiForgetCreds() {
  if (prefs.begin("talentwifi", false)) { prefs.clear(); prefs.end(); }
  wifiSsid[0] = wifiPass[0] = '\0';
  Serial.println("[와이파이] 저장해 둔 설정을 지웠습니다");
}

static void ledUpdate(uint32_t now);   // 상태 LED(아래 '상태 LED' 절) — 붙기를 기다리는 동안 노랑을 깜빡인다

// 한 번 붙어 본다. 실패해도 라디오는 켜 둔 채로 둔다 — 곧 다른 값으로 다시 시도한다.
static bool wifiTry(const char* ssid, const char* pass, uint32_t waitMs) {
  if (!ssid || !ssid[0]) return false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
  const uint32_t t0 = millis();
  // 기다리는 동안 loop 가 돌지 않아 LED 가 멈춘다 — 짧게 끊어 기다리며 여기서 노랑을 깜빡인다
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < waitMs) {
    ledUpdate(millis());
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

// 알고 있는 것으로: 저장된 것 → 구워 넣은 것.
//
// FORCE_WIFI_SETUP 으로 구우면 둘 다 건너뛰고 곧장 실패한다 — 블루투스 설정 화면을
// 손으로 확인하려고 둔 시험용 문이다(NO_WIFI=1 ./build.sh). 공유기를 꺼 보지 않고도
// "못 붙는 기기" 를 만들 수 있다. 평소 빌드에는 들어가지 않는다.
static bool wifiConnectKnown(uint32_t waitMs) {
#ifdef FORCE_WIFI_SETUP
  (void)waitMs;
  Serial.println("[와이파이] FORCE_WIFI_SETUP — 알고 있는 인증정보를 모두 건너뜁니다(시험용 빌드)");
  return false;
#else
  if (wifiSsid[0] && wifiTry(wifiSsid, wifiPass, waitMs)) return true;
  if (strcmp(wifiSsid, WIFI_SSID) != 0 && wifiTry(WIFI_SSID, WIFI_PASSWORD, waitMs)) return true;
  return false;
#endif
}

static void cfgSaveCache() {
  if (!prefs.begin("talentcfg", false)) { Serial.println("[설정] NVS 열기 실패"); return; }
  prefs.putInt  ("step",    cfg.talentStep);
  prefs.putBool ("sleepOn", cfg.sleepEnabled);
  prefs.putUInt ("sleepMs", cfg.sleepTimeoutMs);
  prefs.putUInt ("coolMs",  cfg.tagCooldownMs);
  prefs.putUInt ("debMs",   cfg.touchDebounceMs);
  prefs.putUInt ("httpMs",  cfg.httpTimeoutMs);
  prefs.putUChar("rot",     cfg.rotation);
  prefs.putUChar("bl",      cfg.backlight);
  prefs.putBool ("snd",     cfg.sound);
  prefs.putUInt ("ttl",     cfg.ttlSec);
  prefs.putBool ("att",     cfg.attendanceMode);
  prefs.putUInt ("doneMs",  cfg.doneMs);
  prefs.putString("attCard", cfg.attendanceCard);
  prefs.putString("label",  cfg.label);
  prefs.putString("bgcol",  cfg.bgColor);
  // 켤 때 뜨는 화면은 서버에 붙기 전에 그려야 해서, 파일 이름을 여기 남긴다
  prefs.putString("splash", splashHas ? splashFile.file : "");
  prefs.putUShort("splashW", splashHas ? splashFile.w : 0);
  prefs.putUShort("splashH", splashHas ? splashFile.h : 0);
  menuSave("mEarn",  cfg.earnMenu);
  menuSave("mSpend", cfg.spendMenu);
  prefs.end();
}

// 응답의 메뉴 배열을 읽어 담는다. 서버가 이미 범위를 조여서 주지만,
// 기기에서도 이름 길이와 금액을 한 번 더 본다 — 화면이 깨지는 값이 들어오면
// 현장에서 손댈 방법이 없기 때문이다.
static void readMenu(JsonObject c, const char* key, Menu& out) {
  if (!c[key].is<JsonArray>()) return;             // 키가 없으면 건드리지 않는다
  Menu m;
  for (JsonObject it : c[key].as<JsonArray>()) {
    if (m.count >= MENU_MAX) break;
    const char* n = it["name"] | "";
    int32_t a = it["amount"] | 0;
    if (!n[0] || a < 1 || a > 999) continue;
    strlcpy(m.item[m.count].name, n, sizeof(m.item[0].name));
    m.item[m.count].amount = a;

    // 그림은 있으면 붙이고 없으면 넘어간다. 이름이 버퍼보다 길면 통째로 버린다 —
    // 잘린 이름으로 받으러 가면 404 가 날 뿐이라 아예 없는 셈 치는 편이 낫다.
    const char* im = it["img"] | "";
    const int16_t iw = it["imgW"] | 0, ih = it["imgH"] | 0;
    if (im[0] && iw > 0 && ih > 0 && strlen(im) < sizeof(m.item[0].img)) {
      strlcpy(m.item[m.count].img, im, sizeof(m.item[0].img));
      m.item[m.count].imgW = iw;
      m.item[m.count].imgH = ih;
    }
    m.count++;
  }
  out = m;
}

// ══════════════════════════════════════════════════════════════════
//  서버 연결 — 하나를 이어 쓴다
// ══════════════════════════════════════════════════════════════════
// 예전에는 요청마다 WiFiClientSecure 를 지역 변수로 새로 만들었다. 함수가 끝나면 소멸자가 연결을
// 끊어서, 키링을 댈 때마다 DNS + TCP + TLS 핸드셰이크(서버 인증서가 ECDSA P-256)를 처음부터 했다.
// PC 에서는 수십 ms 인 핸드셰이크가 ESP32 에서는 1초 안팎이라, 태그하고 이름이 뜨기까지가 늘어졌다.
//
// 서버(nginx)는 연결을 70초 넘게 살려 둔다(실측). 전역 하나를 이어 쓰면 두 번째 요청부터는
// 왕복만 남는다. 대기 중에는 apiKeepWarm 이 45초마다 가볍게 불러 연결이 식지 않게 한다.
static WiFiClientSecure apiTls;
static HTTPClient       apiHttp;
static uint32_t         apiUsedMs = 0;          // 마지막으로 쓴 때(0 = 끊어 둠)
static const uint32_t   API_IDLE_MS = 55000;    // 이보다 오래 쉬었으면 서버가 닫았을 수 있어 새로 붙는다

static void apiDrop() {
  apiHttp.end();
  apiTls.stop();
  apiUsedMs = 0;
}

static bool apiBegin(const char* url) {
  if (apiUsedMs && millis() - apiUsedMs > API_IDLE_MS) apiDrop();
  apiTls.setInsecure();                 // 자체 서버라 인증서 검증 생략(다른 보드들과 동일)
  apiHttp.setReuse(true);
  apiHttp.setTimeout(cfg.httpTimeoutMs);
  if (!apiHttp.begin(apiTls, url)) return false;
  apiHttp.addHeader("x-talent-key", TALENT_DEVICE_KEY);   // 서버의 TALENT_DEVICE_KEY 와 대조
  return true;
}

// 응답을 다 읽은 뒤 부른다. drop — 본문을 덜 읽었을 때(오류 응답 등). 남은 조각이 다음 응답의
// 머리로 읽히면 엉뚱한 실패가 나므로 그때는 끊는다. 길이를 모르는 응답(chunked)도 끝을 믿을 수 없어 끊는다.
static void apiEnd(bool drop = false) {
  const bool unknownLen = apiHttp.getSize() < 0;
  apiHttp.end();
  if (drop || unknownLen) apiTls.stop();
  apiUsedMs = millis();
}

// 이어 쓰던 연결이 서버 쪽에서 이미 닫혀 있으면 첫 요청이 **보내는 단계**에서 실패한다.
// 그때만 새로 붙어 한 번 더 보낸다. 보내기 전에 실패한 것이라 지급·사용(POST)도 서버에 닿지 않았다 —
// 두 번 처리될 일이 없다. 응답을 기다리다 끊긴 것(-5·-11)은 서버가 처리했을 수 있어 다시 보내지 않는다.
static bool apiRetryable(int code) {
  return code == HTTPC_ERROR_CONNECTION_REFUSED || code == HTTPC_ERROR_SEND_HEADER_FAILED ||
         code == HTTPC_ERROR_SEND_PAYLOAD_FAILED || code == HTTPC_ERROR_NOT_CONNECTED;
}

// body 가 nullptr 이면 GET. 실패로 끝나면 연결을 끊어 둔다(다음 요청은 새로 붙는다).
static int apiRequest(const char* url, const char* body) {
  for (int attempt = 0; attempt < 2; attempt++) {
    const bool reused = apiHttp.connected();
    if (!apiBegin(url)) { apiDrop(); return HTTPC_ERROR_CONNECTION_REFUSED; }
    int code;
    if (body) {
      apiHttp.addHeader("Content-Type", "application/json");
      code = apiHttp.POST((uint8_t*)body, strlen(body));
    } else {
      code = apiHttp.GET();
    }
    if (code > 0) return code;
    apiDrop();
    if (!reused || !apiRetryable(code)) return code;
    Serial.printf("[연결] 이어 쓰던 연결이 닫혀 있었습니다 — 새로 붙습니다 (%d)\n", code);
  }
  return HTTPC_ERROR_NOT_CONNECTED;
}
static int apiGet(const char* url)                   { return apiRequest(url, nullptr); }
static int apiPost(const char* url, const char* body) { return apiRequest(url, body); }

// 대기 중 연결 데우기 — 키링을 대는 순간 핸드셰이크를 하지 않게 미리 붙여 두고 식지 않게 한다.
// GET /api/talent/ping 은 서버가 아무것도 읽지 않고 답한다(저장소 왕복도 없다).
// 옛 서버에는 이 경로가 없어 401·404 가 오지만, 그래도 연결은 살아 있으므로 목적은 이룬다.
static const uint32_t API_WARM_MS = 45000;
static uint32_t apiWarmNextMs = 0;
static void apiKeepWarm(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (now < apiWarmNextMs) return;
  if (apiUsedMs && now - apiUsedMs < API_WARM_MS) { apiWarmNextMs = apiUsedMs + API_WARM_MS; return; }
  apiWarmNextMs = now + API_WARM_MS;     // 실패해도 매 루프 두드리지 않게
  char url[128];
  snprintf(url, sizeof(url), "%s/ping", TALENT_API_BASE);
  const uint32_t t0 = millis();
  const bool reused = apiHttp.connected();
  const int code = apiGet(url);
  if (code > 0) { apiHttp.getString(); apiEnd(); }
  Serial.printf("[연결] 데우기 %d · %lums (%s)\n", code, (unsigned long)(millis() - t0), reused ? "이어 씀" : "새 연결");
}

// ── 서버에서 설정 받기 ──
// GET /api/talent/config?device=<기기ID>   헤더: x-talent-key
// 실패하면 아무것도 바꾸지 않는다. 캐시(또는 기본값)가 그대로 유지된다.
static bool cfgFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[192];
  snprintf(url, sizeof(url), "%s/config?device=%s", TALENT_API_BASE, TALENT_DEVICE_ID);
  int code = apiGet(url);
  String payload = (code > 0) ? apiHttp.getString() : String();
  if (code > 0) apiEnd();

  if (code != 200) { Serial.printf("[설정] 서버 응답 %d — 캐시를 씁니다\n", code); return false; }

  JsonDocument doc;
  if (deserializeJson(doc, payload) || !doc["success"].as<bool>()) {
    Serial.println("[설정] 응답을 해석하지 못했습니다 — 캐시를 씁니다");
    return false;
  }
  JsonObject c = doc["config"];
  if (c.isNull()) return false;

  cfg.talentStep      = c["talentStep"]      | cfg.talentStep;
  cfg.sleepEnabled    = c["sleepEnabled"]    | cfg.sleepEnabled;
  cfg.sleepTimeoutMs  = c["sleepTimeoutMs"]  | cfg.sleepTimeoutMs;
  cfg.tagCooldownMs   = c["tagCooldownMs"]   | cfg.tagCooldownMs;
  cfg.touchDebounceMs = c["touchDebounceMs"] | cfg.touchDebounceMs;
  cfg.httpTimeoutMs   = c["httpTimeoutMs"]   | cfg.httpTimeoutMs;
  cfg.rotation        = c["rotation"]        | cfg.rotation;
  cfg.backlight       = c["backlight"]       | cfg.backlight;
  cfg.sound           = c["sound"]           | cfg.sound;
  cfg.ttlSec          = c["configTtlSec"]    | cfg.ttlSec;
  cfg.attendanceMode  = c["attendanceMode"]  | cfg.attendanceMode;
  cfg.doneMs          = c["doneMs"]          | cfg.doneMs;
  const char* ac = c["attendanceCard"] | "";
  strlcpy(cfg.attendanceCard, ac, sizeof(cfg.attendanceCard));

  // defaultMode·modeLock 은 더 읽지 않는다 — 방향을 카드가 정하므로
  // 기본 모드도 모드 잠금도 가리킬 것이 없다(관리자 화면의 값은 무시된다).

  const char* lb = c["deviceLabel"] | "";
  if (lb[0]) strlcpy(cfg.label, lb, sizeof(cfg.label));

  const char* bc = c["bgColor"] | "";
  if (bc[0]) strlcpy(cfg.bgColor, bc, sizeof(cfg.bgColor));

  // 메뉴는 키가 있을 때만 갈아 끼운다. 없으면 캐시에 있던 것을 그대로 둔다 —
  // 옛 서버에 붙었다고 메뉴가 사라지면 현장에서 기기가 못 쓰게 된다.
  readMenu(c, "earnMenu",  cfg.earnMenu);
  readMenu(c, "spendMenu", cfg.spendMenu);

  // 모바일 조회 링크 표. 키가 없으면(옛 서버) 건드리지 않는다 —
  // 표가 사라지면 이미 확인해 둔 카드까지 다시 읽게 된다.
  if (doc["passes"].is<JsonObject>()) {
    JsonObject pj = doc["passes"];
    passUriId = pj["uriPrefix"] | 0x04;
    const char* pb = pj["base"] | "";
    if (pb[0]) strlcpy(passBase, pb, sizeof(passBase));

    JsonObject pm = pj["map"];
    if (!pm.isNull()) {
      // static 으로 둔다 — cfgFetch 는 JsonDocument 와 TLS 클라이언트로 스택이
      // 이미 빡빡해서, 여기에 1.3KB 를 더 얹으면 넘칠 수 있다.
      static PassEntry next[PASS_MAX];
      uint8_t n = 0;
      uint16_t dropped = 0;                              // 상한을 넘어 담지 못한 실물 카드 수
      for (JsonPair kv : pm) {
        const char* u = kv.key().c_str();
        const char* t = kv.value().as<const char*>();
        if (!u || !t || !t[0]) continue;
        if (!isCardUid(u)) continue;                    // 한글 이름 키는 리더가 쓸 일이 없다
        if (strlen(u) >= sizeof(next[0].uid) || strlen(t) >= sizeof(next[0].token)) continue;
        // 상한에 닿아도 멈추지 않고 끝까지 센다 — 몇 장이 빠졌는지 알려야 고칠 수 있다.
        // (예전에는 여기서 조용히 멈춰, 넘친 아이들 카드에 주소가 안 써지는 것을 알 길이 없었다)
        if (n >= PASS_MAX) { dropped++; continue; }
        strlcpy(next[n].uid, u, sizeof(next[0].uid));
        strlcpy(next[n].token, t, sizeof(next[0].token));
        // 토큰이 그대로면 "이미 써 있다" 는 판정을 물려받는다. 설정을 받을 때마다
        // 처음부터 다시 읽으면 5분마다 카드를 훑게 된다.
        const int16_t old = passIndexOf(u);
        next[n].ok = (old >= 0 && !strcmp(passes[old].token, t)) ? passes[old].ok : false;
        n++;
      }
      memcpy(passes, next, sizeof(PassEntry) * n);
      passCount = n;
      Serial.printf("[링크] 표 %u개 (%s…) 받음\n", passCount, passBase);
      if (dropped) {
        Serial.printf("[링크] 경고: 상한 %u개를 넘어 %u장은 담지 못했습니다 — 그 카드에는 주소가 써지지 않습니다(PASS_MAX)\n",
                      (unsigned)PASS_MAX, (unsigned)dropped);
      }
    }
  }

  // 배경. 키가 없으면 건드리지 않는다.
  if (doc["bg"].is<JsonArray>()) {
    bgHas[0] = bgHas[1] = false;
    for (JsonObject b : doc["bg"].as<JsonArray>()) {
      const char* t = b["tab"] | "";
      const char* f = b["file"] | "";
      const int16_t w = b["w"] | 0, h = b["h"] | 0;
      const int idx = !strcmp(t, "earn") ? 0 : (!strcmp(t, "spend") ? 1 : -1);
      if (idx < 0 || !f[0] || w < 1 || h < 1) continue;
      if (strlen(f) >= sizeof(bgFile[0].file)) continue;
      bgFile[idx].base = idx;
      strlcpy(bgFile[idx].file, f, sizeof(bgFile[0].file));
      bgFile[idx].w = w;
      bgFile[idx].h = h;
      bgHas[idx] = true;
    }
    // 대기 화면이 하나뿐이라 배경도 한 장만 쓴다. 관리자 화면의 '지급 배경' 을
    // 그 한 장으로 삼는다 — 둘을 다 받아 두어도 갈아 끼울 자리가 없다.
    bgHas[1] = false;
  }

  // 켤 때 뜨는 화면
  if (doc["splash"].is<JsonObject>()) {
    JsonObject sp = doc["splash"];
    const char* f = sp["file"] | "";
    const int16_t w = sp["w"] | 0, hgt = sp["h"] | 0;
    if (f[0] && w > 0 && hgt > 0 && strlen(f) < sizeof(splashFile.file)) {
      splashFile.base = 0;
      strlcpy(splashFile.file, f, sizeof(splashFile.file));
      splashFile.w = w; splashFile.h = hgt;
      splashHas = true;
    } else {
      splashHas = false;
    }
  } else if (doc["splash"].isNull()) {
    splashHas = false;
  }

  // 헤더 띠 그림
  if (doc["header"].is<JsonObject>()) {
    headHasFile = false;
    JsonObject hd = doc["header"];
    const char* on  = hd["file"]    | "";
    const char* off = hd["offFile"] | "";
    const int16_t w = hd["w"] | 0, hgt = hd["h"] | 0;
    if (on[0] && off[0] && w > 0 && hgt > 0
        && strlen(on) < sizeof(headOnFile.file) && strlen(off) < sizeof(headOffFile.file)) {
      headOnFile.base = 0;
      strlcpy(headOnFile.file, on, sizeof(headOnFile.file));
      headOnFile.w = w; headOnFile.h = hgt;
      headOffFile.base = 1;
      strlcpy(headOffFile.file, off, sizeof(headOffFile.file));
      headOffFile.w = hd["offW"] | w;
      headOffFile.h = hd["offH"] | hgt;
      headHasFile = true;
    }
  } else if (doc["header"].isNull() && doc["config"].is<JsonObject>()) {
    headHasFile = false;                 // 서버가 지웠다
  }

  // 완료 그림. 처리를 마쳤을 때 띄운다(0=지급, 1=사용).
  if (doc["done"].is<JsonArray>()) {
    doneHas[0] = doneHas[1] = false;
    for (JsonObject d : doc["done"].as<JsonArray>()) {
      const char* t = d["side"] | "";
      const char* f = d["file"] | "";
      const int16_t w = d["w"] | 0, h = d["h"] | 0;
      const int idx = !strcmp(t, "earn") ? 0 : (!strcmp(t, "spend") ? 1 : -1);
      if (idx < 0 || !f[0] || w < 1 || h < 1) continue;
      if (strlen(f) >= sizeof(doneFile[0].file)) continue;
      doneFile[idx].base = idx;
      strlcpy(doneFile[idx].file, f, sizeof(doneFile[0].file));
      doneFile[idx].w = w;
      doneFile[idx].h = h;
      doneHas[idx] = true;
    }
  }

  // 그림 목록. 키가 없으면 건드리지 않는다(옛 서버에 붙어도 있던 것을 그대로 쓴다).
  if (doc["art"].is<JsonArray>()) {
    artCount = 0;
    for (JsonObject a : doc["art"].as<JsonArray>()) {
      if (artCount >= ART_MAX) break;
      const char* f = a["file"] | "";
      const int32_t base = a["base"] | 0;
      const int16_t w = a["w"] | 0, h = a["h"] | 0;
      if (!f[0] || base < 1 || w < 1 || h < 1) continue;
      if (strlen(f) >= sizeof(artFile[0].file)) continue;
      artFile[artCount].base = base;
      strlcpy(artFile[artCount].file, f, sizeof(artFile[0].file));
      artFile[artCount].w = w;
      artFile[artCount].h = h;
      artCount++;
    }
  }

  cfgClamp();
  cfgSaveCache();
  lastCfgFetch  = millis();
  cfgFromServer = true;

  Serial.printf("[설정] 서버 반영: step=%ld 슬립=%s(%lums) 밝기=%u 제목=%s\n",
                (long)cfg.talentStep, cfg.sleepEnabled ? "켬" : "끔",
                (unsigned long)cfg.sleepTimeoutMs,
                cfg.backlight, cfg.label);
  return true;
}

// ══════════════════════════════════════════════════════════════════
//  소리 — 방향으로 뜻을 구분한다 (지급은 올라가고, 사용은 내려간다)
// ══════════════════════════════════════════════════════════════════
// 부저는 GPIO 와 GND 사이에 그냥 물려 있다. 트랜지스터도 증폭도 없으므로
// 소리 크기를 키울 손잡이는 **주파수** 하나뿐이다.
//
// 얇은 피에조 판은 자기 공진점 언저리에서만 제대로 운다. 흔히 쓰는 12~14mm 판은
// 2.3~3.0kHz 가 그 자리이고, 거기서 멀어질수록 같은 전압을 줘도 소리가 급격히 작아진다.
// 예전 값(300~1600Hz)은 전부 그 아래여서 "울리지 않고 딸깍대는" 소리가 났다 —
// 특히 실패음 300Hz 는 시끄러운 행사장에서 사실상 들리지 않는다.
// 그래서 모든 소리를 2.0~3.2kHz 안으로 옮겼다. 뜻을 나누는 것은 세기가 아니라
// **방향과 개수**라, 대역을 옮겨도 "오르면 지급, 내리면 사용" 은 그대로다.
#define BUZ_LOW  1976   // 실패 — 대역 안에서 가장 낮은 쪽(어둡게 들린다)
#define BUZ_1    2093   // 도
#define BUZ_2    2637   // 미
#define BUZ_3    3136   // 솔

// 음 사이에 짧은 무음을 둔다. 붙여서 내면 두 음이 한 음처럼 뭉개져
// "삐-" 하나로 들린다 — 지급(2연음)과 탭 전환(1음)이 구별되지 않는다.
#define BUZ_GAP  18

static void beep(uint16_t freq, uint16_t ms) {
  if (!cfg.sound) return;              // 서버에서 무음으로 설정한 기기
  tone(PIN_BUZZER, freq, ms);          // ESP32 코어의 tone() 은 LEDC 50% 듀티 — 피에조 진폭이 가장 크다
  delay(ms);            // 재생이 끝날 때까지 붙잡는다(딥슬립 직전에도 잘리지 않게)
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);       // noTone 뒤 핀이 HIGH 로 남으면 판이 눌린 채 있어 다음 음이 둔해진다
  delay(BUZ_GAP);
}

// ══════════════════════════════════════════════════════════════════
//  상태 LED — 보드에 달린 RGB LED(WS2812)
// ══════════════════════════════════════════════════════════════════
// 화면을 보지 않아도 리더가 지금 어떤지 알게 한다 — 줄 선 뒤쪽이나 옆에서도 보인다.
//   빨강  오류 — 실패음이 나는 모든 자리(sndFail). LED_FAIL_MS 동안
//   초록  키링을 알아봤다 — 카드 대기·완료 화면(step 이 대기가 아닐 때), 내역에서 잔액 조회
//   노랑  연결 중 — 와이파이에 안 붙어 있다(부팅 때 붙는 중, 블루투스 설정 대기, 끊겨서 다시 붙는 중).
//         빠르게 깜빡인다(0.3초) — 파랑과 한눈에 갈리게. 이때는 태깅해도 서버에 못 묻는다
//   파랑  평소 대기 — 천천히 깜빡인다(1초 켜짐 · 1초 꺼짐). 살아 있다는 표시다
// 겹치면 위에서부터 이긴다: 붙잡은 색(빨강·내역 초록) → 키링 초록 → 연결 중 노랑 → 대기 파랑.
//
// 핀: 코어의 esp32s3 변형(pins_arduino.h)이 RGB_BUILTIN 을 GPIO48 로 둔다 — ESP32-S3-DevKitC-1 **v1.0**
// 의 자리다. **v1.1** 보드는 LED 가 GPIO38 로 옮겨졌으니 build_opt.h 에 -DSTATUS_LED_PIN=38 을 넣는다.
// 48·38 모두 이 스케치의 다른 핀, N16R8 의 PSRAM(35~37), USB(19·20)와 겹치지 않는다.
// RGB_BUILTIN 도 STATUS_LED_PIN 도 없으면 LED 코드는 아무 일도 하지 않는다.
#if !defined(STATUS_LED_PIN) && defined(RGB_BUILTIN)
#define STATUS_LED_PIN RGB_BUILTIN
#endif

#define LED_FAIL_MS   3000     // 오류 빨강을 두는 시간 — 오류 화면(OVERLAY_MS)과 같게
#define LED_WHO_MS    5000     // 내역에서 잔액을 조회했을 때 초록을 두는 시간 — WHO_OVERLAY_MS 와 같게
#define LED_BLINK_MS  1000     // 대기 파랑의 반 주기(켜짐 1초 · 꺼짐 1초)
#define LED_CONN_MS    300     // 연결 중 노랑의 반 주기 — 파랑보다 빠르게
#define LED_LEVEL     24       // 밝기(0~255). 보드 LED 는 바로 보면 눈부셔서 낮게 둔다

static uint32_t ledRgb(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
#define LED_OFF    0UL
#define LED_RED    ledRgb(LED_LEVEL, 0, 0)
#define LED_GREEN  ledRgb(0, LED_LEVEL, 0)
#define LED_BLUE   ledRgb(0, 0, LED_LEVEL)
// 노랑은 빨강·초록을 섞는다. WS2812 의 초록이 눈에 더 밝게 보여 초록을 조금 낮춰야 주황이 아닌 노랑이 된다
#define LED_YELLOW ledRgb(LED_LEVEL, LED_LEVEL * 3 / 4, 0)

static uint32_t ledShown     = 0xFFFFFFFFUL;   // 마지막으로 쓴 색 — 같으면 다시 쓰지 않는다
static uint32_t ledHoldRgb   = LED_OFF;        // 잠시 붙잡아 둘 색(오류 빨강 등)
static uint32_t ledHoldAt    = 0;
static uint32_t ledHoldMs    = 0;              // 0 이면 붙잡은 것이 없다

// rgbLedWrite 는 RMT 로 한 번 쓰는 데 1ms 남짓 걸린다 — loop 마다 쓰지 않고 색이 바뀔 때만 쓴다.
static void ledWrite(uint32_t rgb) {
#ifdef STATUS_LED_PIN
  if (rgb == ledShown) return;
  ledShown = rgb;
  rgbLedWrite(STATUS_LED_PIN, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
#else
  (void)rgb;
#endif
}

// 지금 상태에 맞는 색. loop 가 매번 부른다.
// 시각은 "지난 시간" 으로 비교한다 — millis() 는 49일마다 0 으로 돌아가서, "이 시각까지" 로 두면
// 상시 전원 기기에서 그 순간 빨강이 49일 동안 남는다.
static void ledUpdate(uint32_t now) {
  if (ledHoldMs && now - ledHoldAt < ledHoldMs) { ledWrite(ledHoldRgb); return; }
  ledHoldMs = 0;
  if (step != STEP_IDLE) { ledWrite(LED_GREEN); return; }        // 키링을 알아봤다(카드 대기·완료)
  if (WiFi.status() != WL_CONNECTED) {                           // 연결 중 — 빠르게 노랑 깜빡임
    ledWrite((now / LED_CONN_MS) % 2 == 0 ? LED_YELLOW : LED_OFF);
    return;
  }
  ledWrite((now / LED_BLINK_MS) % 2 == 0 ? LED_BLUE : LED_OFF);  // 대기 — 천천히 깜빡임
}

// 잠시 한 색으로 붙잡는다. 그 자리에서 곧바로 켠다 — loop 로 돌아갈 때까지 기다리면
// 실패음(0.4초)이 끝난 뒤에야 빨강이 뜬다.
static void ledHold(uint32_t rgb, uint32_t ms) {
  ledHoldRgb = rgb;
  ledHoldAt = millis();
  ledHoldMs = ms;
  ledWrite(rgb);
}

static void sndPowerOn()  { beep(BUZ_1, 80);  beep(BUZ_2, 80);  beep(BUZ_3, 120); } // 올라가며 켜짐
static void sndPowerOff() { beep(BUZ_3, 80);  beep(BUZ_2, 80);  beep(BUZ_1, 140); } // 내려가며 꺼짐
static void sndEarn()     { beep(BUZ_2, 70);  beep(BUZ_3, 130); }                   // 짧게 오르는 두 음
static void sndSpend()    { beep(BUZ_3, 70);  beep(BUZ_1, 130); }                   // 짧게 내리는 두 음
// 낮은 두 번 — 뭔가 잘못됐다. 실패를 알리는 모든 자리가 이것을 부르므로 **오류 빨강도 여기서** 켠다
// (자리마다 따로 켜면 한 군데씩 빠뜨린다). 무음으로 설정한 기기에서도 LED 는 켠다.
static void sndFail()     { ledHold(LED_RED, LED_FAIL_MS); beep(BUZ_LOW, 120); beep(BUZ_LOW, 220); }
static void sndMode()     { beep(BUZ_2, 45); }                                      // 탭 전환 짧은 한 음

// ══════════════════════════════════════════════════════════════════
//  서버 통신 (yvServer /api/talent)
// ══════════════════════════════════════════════════════════════════
// POST /earn 또는 /spend. path 는 "earn" | "spend".
// item 은 고른 메뉴 항목 이름. 서버의 reason 으로 보내 내역에 "아이스크림" 이 남게 한다 —
// 나중에 "이 아이가 뭘 샀는지" 를 잔액 변화만으로는 알 수 없기 때문이다.
// card 를 주면 금액·이름·분류는 서버가 그 카드에서 찾아 쓴다. 리더가 보낸 amount 는
// 화면에 미리 보여주려고 들고 있을 뿐, 실제 처리에는 쓰이지 않는다 — 현장에 놓인
// 기기라 요청을 흉내 내기 쉬워서, 오르내리는 양의 근거는 서버에만 둔다.
static TalentResult talentPost(const char* path, const char* uid, int32_t amount,
                               const char* item = "", const char* card = "") {
  TalentResult r;

  if (WiFi.status() != WL_CONNECTED) {
    snprintf(r.reason, sizeof(r.reason), "%s", "네트워크 없음");
    return r;
  }

  char url[128];
  snprintf(url, sizeof(url), "%s/%s", TALENT_API_BASE, path);

  char body[256];
  if (card && card[0]) {
    snprintf(body, sizeof(body),
             "{\"uid\":\"%s\",\"card\":\"%s\",\"device\":\"%s\"}",
             uid, card, TALENT_DEVICE_ID);
  } else if (item && item[0]) {
    snprintf(body, sizeof(body),
             "{\"uid\":\"%s\",\"amount\":%ld,\"device\":\"%s\",\"reason\":\"%s\"}",
             uid, (long)amount, TALENT_DEVICE_ID, item);
  } else {
    snprintf(body, sizeof(body),
             "{\"uid\":\"%s\",\"amount\":%ld,\"device\":\"%s\"}",
             uid, (long)amount, TALENT_DEVICE_ID);
  }

  int code = apiPost(url, body);
  String payload = (code > 0) ? apiHttp.getString() : String();
  if (code > 0) apiEnd();

  JsonDocument doc;
  bool parsed = !payload.isEmpty() && !deserializeJson(doc, payload);

  if (code == 200 && parsed && doc["success"].as<bool>()) {
    r.ok      = true;
    r.balance = doc["balance"] | 0;
    r.delta   = doc["delta"]   | 0;
    return r;
  }

  if (code == 409) {             // 잔액 부족 — 서버가 현재 잔액도 함께 준다
    r.lowBalance = true;
    r.balance = parsed ? (doc["balance"] | 0) : 0;
    snprintf(r.reason, sizeof(r.reason), "%s", "잔액이 모자랍니다");
    return r;
  }
  if (code == 401) { snprintf(r.reason, sizeof(r.reason), "%s", "기기 인증 실패"); return r; }
  if (code <= 0)   { snprintf(r.reason, sizeof(r.reason), "%s", "서버 연결 실패"); return r; }

  snprintf(r.reason, sizeof(r.reason), "서버 오류 %d", code);
  return r;
}

// ══════════════════════════════════════════════════════════════════
//  포인트 그림 내려받기
// ══════════════════════════════════════════════════════════════════
// 목록에 있는데 파일이 없는 것만 받는다. 이름에 해시가 들어 있어 "있다 = 최신" 이다.
// 받다가 실패해도 그냥 넘어간다 — 그 그림만 숫자로 나오고 나머지는 그대로 돈다.

// 한 장 받아 파일로 쓴다. 26KB 를 통째로 메모리에 올리지 않고 흘려 쓴다 —
// HTTPS 핸드셰이크가 쓸 힙을 남겨 두어야 한다.
// dir — 서버에서 받을 자리. 그림은 "/art/file/", 아이 사진은 "/photo/device/"(artDownload·photoSyncStep).
// 받아 두는 곳은 둘 다 ART_DIR 이다 — 이름 앞머리(ph-)로 갈리므로 섞이지 않는다.
static bool artDownloadFrom(const char* dir, const ArtFile& a) {
  char url[192];
  snprintf(url, sizeof(url), "%s%s%s", TALENT_API_BASE, dir, a.file);

  const int code = apiGet(url);
  if (code != 200) {
    Serial.printf("[그림] %s 응답 %d\n", a.file, code);
    if (code > 0) apiEnd(true);         // 본문을 읽지 않았다 — 이어 쓰지 않는다
    return false;
  }

  // 크기가 맞지 않으면 받지 않는다. 화면에 밀어 넣을 때 길이를 믿기 때문이다.
  const int want = (int)a.w * a.h * 2;
  if (apiHttp.getSize() != want) {
    Serial.printf("[그림] %s 크기 불일치 %d != %d\n", a.file, apiHttp.getSize(), want);
    apiEnd(true);
    return false;
  }

  char path[64];
  snprintf(path, sizeof(path), "%s/%s", ART_DIR, a.file);
  fs::File f = LittleFS.open(path, "w");
  if (!f) { apiEnd(true); return false; }

  const int wrote = apiHttp.writeToStream(&f);
  f.close();
  apiEnd(wrote != want);                // 덜 받았으면 남은 조각이 있다 — 끊는다

  if (wrote != want) {
    LittleFS.remove(path);          // 반쪽짜리를 남기면 다음에 "있다" 로 오해한다
    Serial.printf("[그림] %s 저장 실패 %d/%d\n", a.file, wrote, want);
    return false;
  }
  Serial.printf("[그림] %s 받음 (%dx%d, %dB)\n", a.file, a.w, a.h, wrote);
  return true;
}

// 목록에 없는 파일을 지운다. 그림을 바꾸면 옛 이름이 남는데, 두면 자리만 먹는다.
static void artPrune() {
  fs::File dir = LittleFS.open(ART_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* name = f.name();
    if (artWanted(name)) continue;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, name);
    f.close();
    LittleFS.remove(path);
    Serial.printf("[그림] %s 지움(목록에 없음)\n", name);
  }
}

// ── 켤 때 화면 아래 진행 줄 ──
// 켤 때는 와이파이·설정·카드·명단·그림을 차례로 받느라 몇 초가 걸린다. 켤 때 그림(splash)만 떠 있으면
// 멈춘 것인지 받는 중인지 알 수 없어, 화면 맨 아래에 지금 하는 일을 한 줄로 적는다.
// 켜는 중(setup)에만 그린다 — 다시 붙었을 때 afterOnline 이 불려도 평소 화면에는 그리지 않는다.
static bool bootShowing = false;
static uint16_t bootDownloads = 0;           // 켜면서 새로 받은 그림 수
static const int BOOT_BAR_H = 26;
static void bootStatus(const char* fmt, ...) {
  if (!bootShowing) return;
  char msg[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  const int y = tft.height() - BOOT_BAR_H;
  tft.fillRect(0, y, tft.width(), BOOT_BAR_H, 0x2104);   // 짙은 띠 — 켤 때 그림 위에서도 읽힌다
  useFont(14);                                          // 켜는 동안 14px 을 들고 있는다(줄마다 다시 읽지 않게)
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, 0x2104);
  tft.drawString(msg, tft.width() / 2, y + BOOT_BAR_H / 2);
}

static bool artDownload(const ArtFile& a) {
  if (bootShowing) bootStatus("그림 받는 중 (%u장째)", (unsigned)++bootDownloads);
  return artDownloadFrom("/art/file/", a);
}

// 파일 이름이 지금 목록(배경 + 완료 그림 + 카드 그림 + 아이 사진)에 있는지.
// 여기서 빠뜨리면 artPrune 이 방금 받은 그림을 지워 버린다.
static bool artWanted(const char* name) {
  // 아이 사진 — 이름표를 제대로 받았을 때만 가른다(rosterFresh 주석 참고)
  if (!strncmp(name, "ph-", 3)) return !rosterFresh || photoInRoster(name);
  for (uint8_t i = 0; i < 2; i++)
    if (bgHas[i] && !strcmp(name, bgFile[i].file)) return true;
  for (uint8_t i = 0; i < 2; i++)
    if (doneHas[i] && !strcmp(name, doneFile[i].file)) return true;
  if (headHasFile && (!strcmp(name, headOnFile.file) || !strcmp(name, headOffFile.file)))
    return true;
  if (splashHas && !strcmp(name, splashFile.file)) return true;
  for (uint8_t i = 0; i < cardCount; i++)
    if (cards[i].img[0] && !strcmp(name, cards[i].img)) return true;
  return false;
}

static void artSync() {
  if (WiFi.status() != WL_CONNECTED) return;
  LittleFS.mkdir(ART_DIR);

  // 포인트 그림(금액에 따라 뜨던 것)은 더 이상 그리지 않는다 — 카드마다 그림을
  // 붙이는 쪽으로 바뀌었다. 목록은 옛 서버 호환으로 받아 두되 파일은 받지 않는다.
  uint8_t want = 0, got = 0;
  for (uint8_t i = 0; i < 2; i++) {
    if (!bgHas[i]) continue;
    want++;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, bgFile[i].file);
    if (LittleFS.exists(path) || artDownload(bgFile[i])) got++;
    else bgHas[i] = false;              // 못 받았으면 없는 셈 치고 검정으로 간다
  }
  // 켤 때 뜨는 화면
  if (splashHas) {
    want++;
    char sp[64];
    snprintf(sp, sizeof(sp), "%s/%s", ART_DIR, splashFile.file);
    if (LittleFS.exists(sp) || artDownload(splashFile)) got++;
    else splashHas = false;
  }

  // 헤더 띠 그림. 두 장 중 하나라도 못 받으면 구워 넣은 헤더로 되돌린다.
  if (headHasFile) {
    want += 2;
    char p1[64], p2[64];
    snprintf(p1, sizeof(p1), "%s/%s", ART_DIR, headOnFile.file);
    snprintf(p2, sizeof(p2), "%s/%s", ART_DIR, headOffFile.file);
    const bool h1 = LittleFS.exists(p1) || artDownload(headOnFile);
    const bool h2 = LittleFS.exists(p2) || artDownload(headOffFile);
    if (h1) got++;
    if (h2) got++;
    if (!h1 || !h2) headHasFile = false;
  }

  // 완료 그림
  for (uint8_t i = 0; i < 2; i++) {
    if (!doneHas[i]) continue;
    want++;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, doneFile[i].file);
    if (LittleFS.exists(path) || artDownload(doneFile[i])) got++;
    else doneHas[i] = false;            // 못 받았으면 글자로 대신한다
  }

  // 카드 그림. 못 받은 카드는 이름을 지워 없는 셈 친다 — 그 카드만 글자로 나온다.
  for (uint8_t i = 0; i < cardCount; i++) {
    if (!cards[i].img[0]) continue;
    want++;
    ArtFile a;
    a.base = 0;
    strlcpy(a.file, cards[i].img, sizeof(a.file));
    a.w = cards[i].imgW; a.h = cards[i].imgH;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, a.file);
    if (LittleFS.exists(path) || artDownload(a)) got++;
    else cards[i].img[0] = '\0';
  }

  artPrune();
  Serial.printf("[그림] %u/%u 준비됨\n", got, want);
}

// 아이 사진 한 장 받기 — loop 가 부른다. 받을 것이 있어도 **조용할 때만** 한 장씩 받는다:
//   · 대기 화면(키링을 기다리는 중)이고
//   · 5초 동안 아무 입력이 없고
//   · 와이파이가 붙어 있을 때
// 한 장 받는 동안(0.5초 남짓) 루프가 멈춘다. 줄 선 아이가 키링을 대는 사이에 받으면 그만큼
// 태깅이 늦게 반응하므로 조건을 좁게 잡았다. 실패하면 3초 쉬고 다음 아이로 넘어간다 —
// 그 아이는 다음 이름표 갱신(설정 주기) 때 다시 시도한다.
static void photoSyncStep(uint32_t now) {
  if (photoCursor >= rosterCount || now < photoNextMs) return;
  if (step != STEP_IDLE || now - lastActivity < 5000) return;
  if (WiFi.status() != WL_CONNECTED) return;

  // 이미 받아 둔 것은 건너뛰며 받을 한 장을 찾는다. 한 번에 16자리까지만 본다 —
  // 루프를 오래 붙잡지 않게(남은 자리는 다음 루프에서 이어서 본다).
  for (uint8_t n = 0; n < 16 && photoCursor < rosterCount; n++) {
    const RosterEntry& r = roster[photoCursor++];
    if (!r.img[0]) continue;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, r.img);
    if (LittleFS.exists(path)) continue;

    ArtFile a;
    a.base = 0;
    strlcpy(a.file, r.img, sizeof(a.file));
    a.w = PHOTO_PX; a.h = PHOTO_PX;
    const bool ok = artDownloadFrom("/photo/device/", a);
    Serial.printf("[사진] %s %s\n", r.name, ok ? "받음" : "못 받음");
    photoNextMs = millis() + (ok ? 300 : 3000);
    return;
  }
}

// ══════════════════════════════════════════════════════════════════
//  이름표 · 내역 받기
// ══════════════════════════════════════════════════════════════════
// 두 요청 모두 실패해도 그냥 넘어간다. 이름표가 없으면 UID 를 보여주고,
// 내역이 없으면 내역 탭에만 안내가 뜬다 — 지급·사용는 계속 된다.

// 응답을 String 으로 통째로 받지 않고 스트림에서 바로 파싱한다.
// 이름표는 200명이면 8KB 가 넘어 두 벌을 들고 있을 이유가 없다.
static bool getJson(const char* pathAndQuery, JsonDocument& doc,
                    DeserializationOption::Filter filter) {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[192];
  snprintf(url, sizeof(url), "%s%s", TALENT_API_BASE, pathAndQuery);

  int code = apiGet(url);
  if (code != 200) {
    Serial.printf("[%s] 서버 응답 %d\n", pathAndQuery, code);
    if (code > 0) apiEnd(true);         // 본문을 읽지 않았다 — 이어 쓰지 않는다
    return false;
  }
  DeserializationError err = deserializeJson(doc, apiHttp.getStream(), filter);
  apiEnd((bool)err);                    // 해석이 중간에 멈췄으면 남은 조각이 있다 — 끊는다
  if (err) { Serial.printf("[%s] 해석 실패 %s\n", pathAndQuery, err.c_str()); return false; }
  return doc["success"].as<bool>();
}

// GET /api/talent/roster — UID → 이름
static bool rosterFetch() {
  JsonDocument filter;
  filter["success"] = true;
  filter["data"][0]["uid"]  = true;
  filter["data"][0]["name"] = true;
  filter["data"][0]["img"]  = true;     // 사진 파일 이름(없으면 칸이 없다 — 옛 서버도 마찬가지)

  JsonDocument doc;
  char q[24];
  snprintf(q, sizeof(q), "/roster?px=%d", PHOTO_PX);    // 사진 파일 이름의 크기를 고른다(서버 talentPhoto)
  if (!getJson(q, doc, DeserializationOption::Filter(filter))) return false;

  rosterCount = 0;
  uint16_t withPhoto = 0;
  for (JsonObject r : doc["data"].as<JsonArray>()) {
    if (rosterCount >= ROSTER_MAX) break;
    const char* u = r["uid"]  | "";
    const char* n = r["name"] | "";
    const char* p = r["img"]  | "";
    if (!u[0] || !n[0]) continue;
    strlcpy(roster[rosterCount].uid,  u, sizeof(roster[0].uid));
    strlcpy(roster[rosterCount].name, n, sizeof(roster[0].name));
    // 이름이 길어 잘리면 없는 파일을 찾게 되므로, 칸에 다 들어가지 않는 이름은 사진 없음으로 둔다
    if (p[0] && strlen(p) < sizeof(roster[0].img)) {
      strlcpy(roster[rosterCount].img, p, sizeof(roster[0].img));
      withPhoto++;
    } else {
      roster[rosterCount].img[0] = '\0';
    }
    rosterCount++;
  }
  rosterFresh = true;
  photoCursor = 0;                      // 사진은 처음부터 다시 훑는다(이미 있는 것은 금방 건너뛴다)
  Serial.printf("[이름표] %u명 받음 · 사진 %u명\n", rosterCount, withPhoto);
  return true;
}

// GET /api/talent/cards — 등록된 지급/사용 카드 전부
// 부팅할 때 한 번 받아 두고, 카드를 대면 이 목록에서 찾는다. 목록에 없으면 미등록이다.
// 못 받으면 카드 단계가 통째로 막히므로, 그때는 메뉴(예전 방식)로 물러선다.
static bool cardsFetch() {
  JsonDocument filter;
  filter["success"] = true;
  filter["data"][0]["uid"]    = true;
  filter["data"][0]["name"]   = true;
  filter["data"][0]["amount"] = true;
  filter["data"][0]["side"]   = true;
  filter["data"][0]["img"]    = true;
  filter["data"][0]["imgW"]   = true;
  filter["data"][0]["imgH"]   = true;

  JsonDocument doc;
  if (!getJson("/cards", doc, DeserializationOption::Filter(filter))) return false;

  cardCount = 0;
  for (JsonObject c : doc["data"].as<JsonArray>()) {
    if (cardCount >= CARD_MAX) break;
    const char* u = c["uid"] | "";
    if (!u[0]) continue;
    CardEntry& e = cards[cardCount];
    strlcpy(e.uid,  u,               sizeof(e.uid));
    strlcpy(e.name, c["name"] | "",  sizeof(e.name));
    e.amount = c["amount"] | 0;
    e.spend  = !strcmp(c["side"] | "earn", "spend");
    const char* f = c["img"] | "";
    if (f[0] && strlen(f) < sizeof(e.img)) {
      strlcpy(e.img, f, sizeof(e.img));
      e.imgW = c["imgW"] | 0;
      e.imgH = c["imgH"] | 0;
    } else {
      e.img[0] = '\0'; e.imgW = e.imgH = 0;
    }
    cardCount++;
  }
  Serial.printf("[카드] %u장 받음\n", cardCount);
  return true;
}

// UID 로 카드를 찾는다. 없으면 -1.
static int8_t cardIndexOf(const char* uid) {
  for (uint8_t i = 0; i < cardCount; i++)
    if (!strcmp(cards[i].uid, uid)) return (int8_t)i;
  return -1;
}

// ── 모바일 조회 링크 쓰기 ─────────────────────────────────────────
//
// /talents 의 키는 실물 카드가 없는 아이는 **한글 이름**이다(지금 50명 중 대부분).
// 리더가 읽는 것은 16진 UID 라 둘은 절대 만나지 않으므로, 표에서 카드 UID 만 골라
// 담는다. 아이가 실물 키링을 받으면(미등록카드 → 기존 사용자 매칭) 그때 UID 로
// 바뀌고 서버가 링크를 새로 내주므로, 다음 설정 갱신에 저절로 표에 들어온다.
static bool isCardUid(const char* s) {
  const size_t n = strlen(s);
  if (n != 8 && n != 14 && n != 20) return false;       // ISO14443A 는 4·7·10 바이트
  for (size_t i = 0; i < n; i++)
    if (!isdigit((unsigned char)s[i]) && !(s[i] >= 'A' && s[i] <= 'F')) return false;
  return true;
}

// int16_t 로 돌려준다 — 표가 128개를 넘으면 int8_t 로는 번호가 음수로 넘어가
// "표에 없는 카드" 로 읽힌다(PASS_MAX 가 200 이다).
static int16_t passIndexOf(const char* uid) {
  for (uint8_t i = 0; i < passCount; i++)
    if (!strcmp(passes[i].uid, uid)) return (int16_t)i;
  return -1;
}

// 카드에 이미 이 주소가 들어 있는지. NDEF URI 레코드는 4페이지부터 12바이트가
// 머리말이고 7페이지부터 주소가 이어진다(ntag2xx_WriteNDEFURI 가 그렇게 쓴다).
// 28자짜리 주소면 7장을 읽어 40ms 남짓 — 한 번 확인하면 다시 읽지 않는다.
static bool passAlreadyOn(const char* url, uint8_t len) {
  uint8_t buf[4];
  for (uint8_t i = 0; i < len; i += 4) {
    if (!nfc.ntag2xx_ReadPage(7 + i / 4, buf)) return false;
    for (uint8_t j = 0; j < 4 && i + j < len; j++)
      if (buf[j] != (uint8_t)url[i + j]) return false;
  }
  return true;
}

// **카드가 선택된 상태에서만 부를 것.** 태그를 알아본 직후(CC 읽기 옆)가 그 자리다.
// capBytes 는 그 카드의 사용자 영역 크기 — CC 세 번째 바이트 × 8 (NTAG213 은 144).
static void passEnsureWritten(const char* uid, uint16_t capBytes) {
  if (!passBase[0]) return;
  const int16_t i = passIndexOf(uid);
  if (i < 0 || passes[i].ok) return;                    // 표에 없거나 이미 확인했다

  char url[64];
  const int n = snprintf(url, sizeof(url), "%s%s", passBase, passes[i].token);
  if (n <= 0 || (size_t)n >= sizeof(url)) return;
  // ntag2xx_WriteNDEFURI 는 머리말 12바이트 + 끝표시 1바이트를 더 쓴다
  if ((uint16_t)n + 13 > capBytes) {
    Serial.printf("[링크] 주소가 카드 용량(%u바이트)보다 깁니다 — 건너뜁니다\n", capBytes);
    passes[i].ok = true;                                // 다시 시도해도 소용없다
    return;
  }

  if (passAlreadyOn(url, (uint8_t)n)) { passes[i].ok = true; return; }

  Serial.printf("[링크] %s → %s 쓰는 중…\n", uid, url);
  // Adafruit 의 dataLen 이 uint8_t 라 255 를 넘길 수 없다. NTAG215/216 을 쓰게 되면
  // 앞쪽 255바이트만 쓰는 셈인데, 주소가 40자 남짓이라 문제가 되지 않는다.
  const uint8_t cap8 = capBytes > 255 ? 255 : (uint8_t)capBytes;
  if (nfc.ntag2xx_WriteNDEFURI(passUriId, url, cap8)) {
    passes[i].ok = true;
    Serial.println("[링크] 썼습니다 — 이제 휴대전화로 대면 잔액이 보입니다");
  } else {
    // 카드를 일찍 뗐을 때가 대부분이다. 다음 태깅에 다시 해 본다.
    // 실패한 교환은 대상 선택 상태를 망가뜨려 그 뒤 읽기가 전부 실패한다.
    Serial.println("[링크] 쓰지 못했습니다 — 다음 태깅에 다시 시도합니다");
    nfc.SAMConfig();
  }
}

// POST /api/talent/seen — 처음 보는 카드를 서버에 알린다.
// 예전에는 모르는 UID 에 곧바로 잔액을 만들었다. 그러면 휴대폰이 스쳐도 "이름 없는
// 사람" 이 생겼다. 이제는 알리기만 하고, 관리자가 화면에서 무엇인지 정해 준다.
static void reportSeen(const char* uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[160];
  snprintf(url, sizeof(url), "%s/seen", TALENT_API_BASE);

  char body[128];
  snprintf(body, sizeof(body), "{\"uid\":\"%s\",\"device\":\"%s\"}", uid, TALENT_DEVICE_ID);
  const int code = apiPost(url, body);
  if (code > 0) { apiHttp.getString(); apiEnd(); }   // 본문까지 읽어야 연결을 이어 쓸 수 있다
  Serial.printf("[미등록] %s 알림 (%d)\n", uid, code);
}

// GET /api/talent/feed — 최근 내역 (이름·경과초는 서버가 붙여 준다)
//
// page 0 이 가장 최근이다. 화면에 들어가는 줄 수만큼만 달라고 해서 받은 것과 보이는
// 것을 같게 둔다 — 다르면 페이지를 넘길 때 몇 건이 소리 없이 건너뛰어진다.
// hasMore 는 "더 예전 것이 남았는지" 다. 리더가 스스로 알 방법이 없어 서버가 알려 준다.
static uint8_t feedRows();      // 화면 치수에 딸린 값이라 정의는 아래 화면 부분에 있다
// 기본 인자가 있는 함수는 .ino 가 프로토타입을 자동으로 만들어 주지 않는다.
// 정의는 화면 부분에 있고, 그보다 먼저 부르는 곳이 있어 여기서 미리 알린다.
static bool drawArtFile(const ArtFile& a, int x, int y, bool transp = false);

static bool feedFetch(uint8_t page) {
  JsonDocument filter;
  filter["success"] = true;
  filter["hasMore"] = true;
  filter["data"][0]["uid"]    = true;
  filter["data"][0]["name"]   = true;
  filter["data"][0]["delta"]  = true;
  filter["data"][0]["agoSec"] = true;

  char q[48];
  snprintf(q, sizeof(q), "/feed?limit=%u&page=%u", (unsigned)feedRows(), (unsigned)page);

  JsonDocument doc;
  if (!getJson(q, doc, DeserializationOption::Filter(filter))) {
    feedOk = false;
    return false;
  }

  feedCount = 0;
  for (JsonObject l : doc["data"].as<JsonArray>()) {
    if (feedCount >= FEED_MAX) break;
    const char* n = l["name"] | "";
    strlcpy(feed[feedCount].who, n[0] ? n : (l["uid"] | "?"), sizeof(feed[0].who));
    feed[feedCount].delta  = l["delta"]  | 0;
    feed[feedCount].agoSec = l["agoSec"] | 0;
    feedCount++;
  }
  // 빈 페이지를 받았다면(그 사이 내역이 지워졌다든지) 거기 머무르지 않고 최근 쪽으로
  // 되돌린다 — 빈 화면에서 '이전' 을 눌러야만 빠져나오는 상태를 만들지 않는다.
  feedPage    = (feedCount == 0 && page > 0) ? 0 : page;
  feedMore    = doc["hasMore"] | false;
  feedFetchMs = millis();
  feedOk = true;
  Serial.printf("[내역] %u쪽 %u건 받음 (다음 %s)\n",
                (unsigned)(feedPage + 1), feedCount, feedMore ? "있음" : "없음");
  return true;
}

// GET /api/talent/feed?uid=... — 한 키링의 잔액과 최근 내역
// 내역 탭에서 키링을 댔을 때 쓴다. 잔액과 내역을 한 번에 받는 이유는, 두 번 왕복하면
// 키링을 대고 화면이 뜰 때까지가 눈에 띄게 늘어지기 때문이다.
// full — 내역까지 받는다(내역 탭). 평소 화면은 잔액만 있으면 되므로 false 로 부른다 —
// 서버가 내역·전체 명단을 읽지 않고 그 키링 한 줄만 읽어 답한다(limit=0, 옛 서버는 내역을 붙여 준다).
static bool whoFetch(const char* uid, bool full = true) {
  JsonDocument filter;
  filter["success"] = true;
  filter["who"]["name"]    = true;
  filter["who"]["balance"] = true;
  filter["who"]["known"]   = true;
  filter["data"][0]["delta"]  = true;
  filter["data"][0]["agoSec"] = true;

  char q[64];
  snprintf(q, sizeof(q), "/feed?limit=%u&uid=%s", full ? (unsigned)WHO_MAX : 0u, uid);

  strlcpy(whoUid, uid, sizeof(whoUid));
  whoCount = 0; whoBalance = 0; whoKnown = false; whoName[0] = '\0';

  JsonDocument doc;
  if (!getJson(q, doc, DeserializationOption::Filter(filter))) return false;

  JsonObject w = doc["who"];
  if (!w.isNull()) {
    strlcpy(whoName, w["name"] | "", sizeof(whoName));
    whoBalance = w["balance"] | 0;
    whoKnown   = w["known"]   | false;
  }
  // 이름표에 있으면 그것도 받는다 — 서버가 이름을 비워 보내도 화면은 이름으로 나온다
  if (!whoName[0]) strlcpy(whoName, nameOf(uid), sizeof(whoName));

  for (JsonObject l : doc["data"].as<JsonArray>()) {
    if (whoCount >= WHO_MAX) break;
    whoFeed[whoCount].who[0] = '\0';           // 한 사람 것이라 이름은 줄마다 적지 않는다
    whoFeed[whoCount].delta  = l["delta"]  | 0;
    whoFeed[whoCount].agoSec = l["agoSec"] | 0;
    whoCount++;
  }
  whoFetchMs = millis();
  Serial.printf("[조회] %s(%s) 잔액 %ld · 내역 %u건\n",
                uid, whoName, (long)whoBalance, whoCount);
  return true;
}

// ══════════════════════════════════════════════════════════════════
//  화면
// ══════════════════════════════════════════════════════════════════
static void backlight(bool on) {
  // PWM 으로 서서히 켜면 눈이 편하다. 끌 때는 바로 끈다. 최대 밝기는 서버 설정을 따른다.
  if (on) {
    for (int d = 0; d < cfg.backlight; d += 15) { analogWrite(PIN_TFT_BL, d); delay(6); }
    analogWrite(PIN_TFT_BL, cfg.backlight);
  } else {
    analogWrite(PIN_TFT_BL, 0);
  }
}

// ── 치수 ──────────────────────────────────────────────────────────
// 테두리 · 헤더 · 내용 세 층으로 나눈다. rotation 에 따라 폭이 바뀌므로
// 가로 좌표는 tft.width() 에서 매번 계산한다.
//
// 예전에는 헤더 아래에 78px 짜리 탭 줄(지급·사용 그림 버튼)이 한 층 더 있었다.
// 방향을 카드가 정하게 되면서 고를 것이 없어져 걷어냈고, 그 78px 은 내용이 가져갔다 —
// 카드 목록이 세 줄 더 들어가고, 배경 그림도 그만큼 커졌다.
#define BORDER   4
#define HEAD_H  42
static int contentTop() { return BORDER + HEAD_H; }
static int contentH()   { return tft.height() - contentTop() - BORDER; }
static int contentMid() { return contentTop() + contentH() / 2; }

// ── 색 ────────────────────────────────────────────────────────────
// tft.color565 는 init() 뒤에야 쓸 수 있어 setup() 에서 채운다.
static uint16_t C_EARN, C_SPEND, C_NEUTRAL, C_HEADTXT, C_TABBG, C_ROWALT, C_WIFI, C_STRIP;
// 흰 바탕에서 쓰는 벌. 밝은 초록·연회색은 흰 종이 위에서 흐려 보여 따로 둔다.
static uint16_t C_EARN_D, C_SPEND_D, C_INK, C_LINE;
// 내역 탭 페이지 띠에서 '지금은 눌러도 소용없다' 를 말하는 회색

static void initColors() {
  C_EARN    = tft.color565(  0, 200,  90);
  C_SPEND   = tft.color565(230,  40,  40);
  C_NEUTRAL = tft.color565( 90,  96, 108);
  C_HEADTXT = tft.color565( 24,  48, 107);   // 로고의 남색과 같은 계열
  C_TABBG   = tft.color565( 26,  28,  34);
  C_ROWALT  = tft.color565( 13,  15,  19);
  C_WIFI    = tft.color565(138, 144, 153);
  // 탭 줄 배경. 버튼 그림이 이 색에 합성돼 있어 같은 색이라야 이어진다(FunFunLogo.h).
  C_STRIP   = tft.color565(STRIP_R, STRIP_G, STRIP_B);

  C_EARN_D  = tft.color565(  0, 150,  66);   // 흰 바탕용 초록 — 밝은 쪽은 눈에 안 잡힌다
  C_SPEND_D = tft.color565(200,  28,  28);
  C_INK     = tft.color565( 20,  22,  28);   // 본문 — 순검정보다 눈이 덜 아프다
  C_LINE    = tft.color565(214, 218, 224);   // 가름선
}

// ── 테두리 ────────────────────────────────────────────────────────
// 부호(+/-)를 쓰지 않는 대신 이 테두리가 방향을 알린다. 획 하나보다
// 멀리서 잘 보이고, 화면 어디를 보고 있든 눈에 들어온다.
//
// 다만 방향은 카드를 댄 뒤에야 정해진다. 그전까지(대기·카드 기다림·내역)는
// 회색이다 — 아직 아무 방향도 아닌데 초록이나 빨강을 띄우면 그 색이 거짓말이 된다.
static uint16_t frameColor() {
  if (step == STEP_DONE) return doneDelta > 0 ? C_EARN : C_SPEND;
  return C_NEUTRAL;
}

static void drawFrame() {
  const uint16_t c = frameColor();
  const int w = tft.width(), h = tft.height();
  tft.fillRect(0, 0, w, BORDER, c);
  tft.fillRect(0, h - BORDER, w, BORDER, c);
  tft.fillRect(0, 0, BORDER, h, c);
  tft.fillRect(w - BORDER, 0, BORDER, h, c);
}

// ── 헤더 ──────────────────────────────────────────────────────────
// 흰 띠. 왼쪽부터 로고 · (제목) · 내역 버튼 · 연결 상태.
//
// 내역은 예전에 세 번째 탭이었다. 탭이 그림 버튼으로 커지면서 셋을 나란히 두면
// 하나가 반 폭이 되어 그림이 뭉개진다. 내역은 태깅과 상관없는 "보기" 라서
// 헤더로 옮겨도 뜻이 흐려지지 않는다.
//
// 로고는 두 벌 중 하나를 고른다: 제목이 기본값이면 워드마크(글자가 이미 들어
// 있다), 제목을 바꾼 기기면 코인만 두고 그 옆에 제목을 그린다.

static void drawHeader() {
  const int x0 = BORDER, w = tft.width() - BORDER * 2, y = BORDER;

  // 관리자가 올린 띠가 있으면 그 한 장이 로고·제목·연결 표시를 다 말한다.
  // 연결 상태에 따라 두 장을 갈아 끼운다 — 끊긴 것은 태깅하다 실패하기 전에
  // 여기서 먼저 보여야 한다.
  if (headHasFile) {
    const ArtFile& a = (WiFi.status() == WL_CONNECTED) ? headOnFile : headOffFile;
    if (drawArtFile(a, x0, y)) return;
    // 파일을 읽다 실패하면 아래 기본 헤더로 내려간다
  }

  tft.fillRect(x0, y, w, HEAD_H, TFT_WHITE);

  const bool named = strcmp(cfg.label, DEF_LABEL) != 0;
  const int lw = named ? FUNFUN_MARK_W : FUNFUN_LOGO_W;
  const int lh = named ? FUNFUN_MARK_H : FUNFUN_LOGO_H;
  tft.pushImage(x0 + 8, y + (HEAD_H - lh) / 2, lw, lh,
                named ? FUNFUN_MARK : FUNFUN_LOGO);

  if (named) {
    useFont(20);
    tft.setTextColor(C_HEADTXT, TFT_WHITE);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(cfg.label, x0 + 8 + lw + 8, y + HEAD_H / 2);
    useFont(0);
  }

  // 내역 버튼은 따로 두지 않는다 — 헤더 띠 전체가 내역 단추다(histHit).
  // 작은 아이콘 하나보다 띠 전체가 훨씬 누르기 쉽고, 관리자가 올린 띠 그림에
  // 자리를 뺏기지도 않는다. 보고 있는 중이라는 표시는 테두리 색(회색)이 한다.

  // 연결이 끊긴 것은 태깅하다 실패하기 전에 여기서 먼저 보여야 한다.
  //
  // 잘 되고 있을 때는 아이콘으로 조용히 알린다. 끊겼을 때만 빨간 글자로 말하는데,
  // 그때는 사람이 뭔가 해야 하는 상태라 그림보다 글이 낫기 때문이다.
  if (WiFi.status() == WL_CONNECTED) {
    tft.pushImage(x0 + w - 8 - ICON_ONLINE_W, y + (HEAD_H - ICON_ONLINE_H) / 2,
                  ICON_ONLINE_W, ICON_ONLINE_H, ICON_ONLINE);
  } else {
    useFont(14);
    tft.setTextColor(C_SPEND, TFT_WHITE);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("끊김", x0 + w - 8, y + HEAD_H / 2);
    useFont(0);
  }
}

// 헤더 띠를 눌렀는지 — 띠 전체가 내역 단추다.
static bool histHit(uint16_t tx, uint16_t ty) {
  return (int)tx >= BORDER && (int)tx < tft.width() - BORDER
      && (int)ty >= BORDER && (int)ty < BORDER + HEAD_H;
}

// 내용 영역을 비운다. 배경 그림이 있으면 그것으로 채우고, 없으면 탭의 바탕색으로 채운다.
//
// 바탕색은 어느 탭이든 설정색(bgColor, 기본 #EBAC42)이다. 헤더 띠·탭 줄의 빈자리도
// 같은 색으로 칠해 위에서 아래까지 한 장으로 이어지게 한다.
// 목록의 줄무늬는 그 바탕에서 한 단계 눌러 만든다(bgShade) — 바탕이 무슨 색이든
// 뒤집히지 않는다.
//
// 배경 사진을 쓸 때는 글자를 투명하게 그려야 한다(setTextColor 인자 하나). 배경색을 함께
// 주면 글자마다 네모가 찍혀 그림을 가리기 때문이다. 잔상 걱정은 없다 —
// 내용은 늘 이 함수로 지운 뒤 통째로 다시 그린다.
static bool     contentHasBg = false;
static uint16_t C_BG = TFT_WHITE;   // 지금 내용 영역의 바탕색 (clearContent 가 정한다)

// "#RRGGBB" 를 RGB565 로. 읽을 수 없으면 기본색으로 돌린다.
static uint16_t hexToColor(const char* hex) {
  if (!hex || strlen(hex) != 7 || hex[0] != '#') return tft.color565(0xEB, 0xAC, 0x42);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = nib(hex[i + 1]);
    if (v[i] < 0) return tft.color565(0xEB, 0xAC, 0x42);
  }
  return tft.color565(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
}

// 그 색 위에 어두운 글자가 나은지. 사람 눈은 초록에 가장 민감하고 파랑에 둔해서
// 단순 평균이 아니라 가중치를 준다(BT.601). 어떤 색을 넣어도 글자가 읽히게 하려는 것이다.
static bool isLightColor(const char* hex) {
  if (!hex || strlen(hex) != 7 || hex[0] != '#') return true;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  const int r = nib(hex[1]) * 16 + nib(hex[2]);
  const int g = nib(hex[3]) * 16 + nib(hex[4]);
  const int b = nib(hex[5]) * 16 + nib(hex[6]);
  return (r * 299 + g * 587 + b * 114) / 1000 >= 140;
}

// 내용 영역 바탕색 — 어느 탭이든 설정색(기본 #EBAC42)이다.
// 내역만 검정으로 두던 것을 없앴다: 화면이 두 세계로 갈라져 보였고, 줄무늬는
// 바탕색에서 한 단계 눌러 만들면 밝은 바탕에서도 자연스럽다(bgShade).
static uint16_t screenBg() { return hexToColor(cfg.bgColor); }

// 바탕색에서 한 단계 진한(또는 밝은) 색. 줄무늬와 띠에 쓴다.
// 밝은 바탕이면 눌러서 어둡게, 어두운 바탕이면 띄워서 밝게 — 바탕이 무슨 색이든
// 같은 만큼만 차이를 주면 눈에 자연스럽다.
static uint16_t bgShade(uint8_t amt) {
  const char* h = cfg.bgColor;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  if (!h || strlen(h) != 7 || h[0] != '#') return tft.color565(0xD4, 0x9B, 0x3B);
  int r = nib(h[1]) * 16 + nib(h[2]);
  int g = nib(h[3]) * 16 + nib(h[4]);
  int b = nib(h[5]) * 16 + nib(h[6]);
  if (isLightColor(h)) {
    r = r * (100 - amt) / 100; g = g * (100 - amt) / 100; b = b * (100 - amt) / 100;
  } else {
    r += (255 - r) * amt / 100; g += (255 - g) * amt / 100; b += (255 - b) * amt / 100;
  }
  return tft.color565(r, g, b);
}

// 그림을 몇 줄씩 묶어 읽고 보낸다. 예전에는 한 줄마다 파일 읽기 + SPI 전송(창 잡기·잠금)을 따로 해서
// 배경(270줄)과 큰 사진(208줄)을 그리는 데만 눈에 띄게 걸렸다. 16줄이면 버퍼가 240x16x2 = 7.7KB 다.
#define BLIT_ROWS 16
static uint16_t blitBuf[240 * BLIT_ROWS];
static uint16_t blitSide[240 * BLIT_ROWS];

// hole — 곧 불투명한 그림(큰 사진)으로 덮일 네모. 그 안의 배경은 그리지 않는다(어차피 가려진다).
// inset — 네모 위아래 몇 줄은 그래도 그린다. 사진 모서리가 둥글어(비침색) 그 틈으로 배경이 보이기 때문이다.
// hw 가 0 이면 구멍 없이 전부 그린다(clearContent).
static void clearContentHole(int hx, int hy, int hw, int hh, int inset) {
  const int x = BORDER, y = contentTop();
  const int w = tft.width() - BORDER * 2, h = contentH();
  const int y0 = hw > 0 ? hy + inset : 0, y1 = hw > 0 ? hy + hh - inset : 0;   // 건너뛸 줄 [y0, y1)
  const int lw = hw > 0 ? hx - x : 0, rw = hw > 0 ? x + w - (hx + hw) : 0;       // 구멍 줄의 왼쪽·오른쪽 여백

  contentHasBg = false;
  C_BG = screenBg();

  // 내역 탭은 배경 그림을 쓰지 않는다 — 목록을 정확히 읽어야 하는 자리다.
  // (바탕색은 다른 탭과 같다. 그림만 안 깐다)
  // 화면을 돌린 기기도 쓰지 않는다(배경은 세로 232x192 로만 만들어 둔다).
  if (tab != TAB_HISTORY && bgHas[0] && bgFile[0].w == w && bgFile[0].h == h) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, bgFile[0].file);
    fs::File f = LittleFS.open(path, "r");
    if (f) {
      bool ok = true;
      for (int j = 0; j < h && ok; j += BLIT_ROWS) {
        const int n = min(BLIT_ROWS, h - j);
        if (f.read((uint8_t*)blitBuf, (size_t)w * n * 2) != (size_t)w * n * 2) { ok = false; break; }
        // 묶음 안에서 '구멍 줄' 과 '온 줄' 로 이어진 토막마다 한 번씩 보낸다
        for (int r = 0; r < n;) {
          const bool in = (y + j + r) >= y0 && (y + j + r) < y1;
          int k = r;
          while (k < n && (((y + j + k) >= y0 && (y + j + k) < y1) == in)) k++;
          if (!in) {
            tft.pushImage(x, y + j + r, w, k - r, blitBuf + r * w);
          } else {
            if (lw > 0) {
              for (int q = r; q < k; q++) memcpy(blitSide + (q - r) * lw, blitBuf + q * w, (size_t)lw * 2);
              tft.pushImage(x, y + j + r, lw, k - r, blitSide);
            }
            if (rw > 0) {
              for (int q = r; q < k; q++) memcpy(blitSide + (q - r) * rw, blitBuf + q * w + (w - rw), (size_t)rw * 2);
              tft.pushImage(x + w - rw, y + j + r, rw, k - r, blitSide);
            }
          }
          r = k;
        }
      }
      f.close();
      if (ok) { contentHasBg = true; return; }
      // 읽다 실패하면 아래에서 바탕색으로 덮는다
    }
  }
  if (hw <= 0) { tft.fillRect(x, y, w, h, C_BG); return; }
  tft.fillRect(x, y, w, y0 - y, C_BG);                       // 구멍 위
  tft.fillRect(x, y1, w, y + h - y1, C_BG);                  // 구멍 아래
  if (lw > 0) tft.fillRect(x, y0, lw, y1 - y0, C_BG);        // 구멍 옆
  if (rw > 0) tft.fillRect(x + w - rw, y0, rw, y1 - y0, C_BG);
}

static void clearContent() { clearContentHole(0, 0, 0, 0, 0); }

// 내용 영역의 한 조각만 바탕으로 되돌린다 — 화면 전체를 다시 그리지 않고 그 자리만 고칠 때.
// 배경 그림이 깔려 있으면(clearContent 가 contentHasBg 로 알려 둔다) 그 조각을 파일에서 다시 읽는다.
static void restoreContent(int x, int y, int w, int h) {
  const int cx = BORDER, cy = contentTop();
  const int cw = tft.width() - BORDER * 2, ch = contentH();
  if (x < cx) { w -= cx - x; x = cx; }
  if (y < cy) { h -= cy - y; y = cy; }
  if (x + w > cx + cw) w = cx + cw - x;
  if (y + h > cy + ch) h = cy + ch - y;
  if (w <= 0 || h <= 0) return;

  if (contentHasBg) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, bgFile[0].file);
    fs::File f = LittleFS.open(path, "r");
    if (f) {
      static uint16_t line[240];
      bool ok = true;
      for (int j = 0; j < h && ok; j++) {
        ok = f.seek(((size_t)(y - cy + j) * cw + (x - cx)) * 2) && f.read((uint8_t*)line, w * 2) == (size_t)(w * 2);
        if (ok) tft.pushImage(x, y + j, w, 1, line);
      }
      f.close();
      if (ok) return;
    }
  }
  tft.fillRect(x, y, w, h, C_BG);
}

// 내용 영역의 글자색. 배경 사진이 있으면 투명하게, 없으면 바탕색을 함께 준다.
// (배경색을 주면 글자 뒤가 칠해져 그림이 가려진다)
static void contentText(uint16_t fg) {
  if (contentHasBg) tft.setTextColor(fg);
  else              tft.setTextColor(fg, C_BG);
}

// ── 글자색 세 벌 ──────────────────────────────────────────────────
// 같은 자리라도 바탕이 밝으냐 어두우냐(또는 어두운 배경 사진이냐)에 따라
// 읽히는 색이 반대다. 그릴 때마다 고르지 않게 역할로 부른다.
//   inkMain  제목·이름처럼 가장 먼저 읽혀야 하는 것
//   inkSub   안내문
//   inkMuted 곁들이는 값(경과 시간 등)
//
// 세 단계를 **바탕색에서 끌어낸다.** 예전에는 회색 두 벌(C_INK2·C_INK3)을 고정으로
// 썼는데, 바탕색은 관리자가 아무 색이나 넣을 수 있어서 기본 주황(#EBAC42) 위의
// 회색처럼 글자가 바탕에 묻는 자리가 생겼다. 이제 가장 잘 읽히는 색(먹색 또는 흰색)에서
// 바탕 쪽으로 정해진 만큼만 섞어 흐린 단계를 만든다 — 바탕이 무슨 색이든 대비가 남는다.
static bool lightBg() { return !contentHasBg && isLightColor(cfg.bgColor); }

// 바탕 쪽으로 pct% 만큼 섞은 글자색. 0 이면 가장 진한 글자, 100 이면 바탕과 같아진다.
static uint16_t inkMix(uint8_t pct) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  const char* h = cfg.bgColor;
  int br = 0, bg = 0, bb = 0;                 // 섞어 갈 바탕색
  if (!contentHasBg && h && strlen(h) == 7 && h[0] == '#') {
    br = nib(h[1]) * 16 + nib(h[2]);
    bg = nib(h[3]) * 16 + nib(h[4]);
    bb = nib(h[5]) * 16 + nib(h[6]);
  }
  // 배경 사진 위에서는 바탕을 검정으로 친다 — 흰 글자가 어두운 쪽으로만 흐려진다.
  const bool light = lightBg();
  int r = light ?  20 : 255;                  // C_INK / 흰색
  int g = light ?  22 : 255;
  int b = light ?  28 : 255;
  r += (br - r) * pct / 100;
  g += (bg - g) * pct / 100;
  b += (bb - b) * pct / 100;
  return tft.color565(r, g, b);
}

static uint16_t inkMain()  { return inkMix(0); }
static uint16_t inkSub()   { return inkMix(28); }
static uint16_t inkMuted() { return inkMix(48); }
// 방향색(초록/빨강)도 흰 바탕에서는 한 단계 진한 쪽을 쓴다.
static uint16_t inkEarn()  { return lightBg() ? C_EARN_D  : C_EARN; }
static uint16_t inkSpend() { return lightBg() ? C_SPEND_D : C_SPEND; }
static uint16_t inkDir(bool up) { return up ? inkEarn() : inkSpend(); }


// 비침색(color key). RGB565 에는 알파가 없어서, 서버가 투명했던 자리를 이 색으로
// 채워 두고 여기서 그 색만 건너뛴다(TFT_eSPI 의 pushImage 투명 인자).
// 서버(talentArt.js TRANSPARENT_KEY)와 같은 값이라야 한다.
#define ART_TRANSPARENT 0xF81F   // 자홍 — 그림에 거의 안 나오는 색

// transp 를 주면 비침색 자리를 건너뛴다. 내용 영역 위에 얹히는 그림(완료·카드)만
// 그렇게 굽는다 — 헤더·탭·배경·시작화면은 자리를 꽉 채우는 그림이라 필요 없다.
static bool drawArtFile(const ArtFile& a, int x, int y, bool transp) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%s", ART_DIR, a.file);
  fs::File f = LittleFS.open(path, "r");
  if (!f) return false;

  // 화면 폭(240)만큼 잡는다. 예전에는 128 이었는데, 그때는 이 함수가 포인트·카드
  // 그림(폭 108)만 그렸기 때문이다. 헤더 띠(232)와 완료 그림(154)이 이 길을 타면서
  // 폭이 128 을 넘어 조용히 false 로 떨어졌다 — 올려도 안 나오던 원인이다.
  if (a.w > 240) { f.close(); return false; }
  // 몇 줄씩 묶어 읽고 보낸다(BLIT_ROWS). 비침색이 든 묶음만 비침 경로로 보낸다 — 비침 경로는 픽셀마다
  // 색을 보고 이어진 토막마다 창을 다시 잡아 느리다. 큰 사진은 둥근 모서리가 있는 위아래 몇 묶음만 해당된다.
  for (int j = 0; j < a.h; j += BLIT_ROWS) {
    const int n = min(BLIT_ROWS, (int)a.h - j);
    const size_t cnt = (size_t)a.w * n;
    if (f.read((uint8_t*)blitBuf, cnt * 2) != cnt * 2) { f.close(); return false; }
    bool keyed = false;
    if (transp) for (size_t i = 0; i < cnt; i++) if (blitBuf[i] == ART_TRANSPARENT) { keyed = true; break; }
    if (keyed) tft.pushImage(x, y + j, a.w, n, blitBuf, ART_TRANSPARENT);
    else       tft.pushImage(x, y + j, a.w, n, blitBuf);
  }
  f.close();
  return true;
}

// ── 지급 / 사용 탭 ────────────────────────────────────────────────
// 메뉴가 있으면 왼쪽은 "무엇을 얼마나", 오른쪽은 고르는 목록으로 나눈다.
// 메뉴가 비어 있으면 예전처럼 talentStep 하나를 가운데에 크게 둔다.
//
// 한글은 14·20px 두 종류뿐이라(FontKR14/20) 큰 숫자는 내장 폰트 6번(48px)을 쓴다.
static void drawTagScreen() {
  clearContent();

  // ── 대기 화면 ──
  // 그림 한 장으로 말하는 자리다. 배경(bg)을 올려 두면 그것만 보여준다.
  //
  // 예전에는 여기에 메뉴(항목별 포인트)와 포인트 그림을 그렸다. 새 흐름에서는
  // "무엇을 얼마나" 를 지급/사용 카드가 말하므로, 메뉴를 눌러도 처리에 아무 영향이
  // 없다 — 눌리는데 아무 일도 안 나는 목록이 화면을 덮고 있었다. 그래서 뺐다.
  // (설정의 earnMenu·spendMenu 는 서버에 남아 있지만 리더는 더 이상 쓰지 않는다)
  //
  // 배경이 없을 때만 한 줄을 남긴다. 아무것도 없는 흰 화면은 고장으로 보이고,
  // 그때는 무엇을 해야 하는지 알려 줄 방법이 글자밖에 없다.
  if (!contentHasBg) {
    useFont(20);
    tft.setTextDatum(MC_DATUM);
    contentText(inkMain());
    tft.drawString("키링을 대주세요", tft.width() / 2, contentMid());
    useFont(0);
  }
}

// ── 내역 탭 ───────────────────────────────────────────────────────
// 부호 대신 색으로 방향을 말한다 — 목록에서도 규칙이 같아야 한다.
//
// 위·아래에 페이지 띠를 하나씩 둔다. 위는 최근 쪽(이전 페이지), 아래는 예전 쪽
// (다음 페이지)이다. 목록을 읽는 눈의 방향과 같게 두어, 아래로 다 읽고 나면
// 그 자리에서 바로 "다음" 을 누르게 된다.
#define HROW    24   // 내역 한 줄
#define HNAV_H  26   // 페이지 띠 높이

static int histNavTopY()  { return contentTop(); }
static int histNavBotY()  { return contentTop() + contentH() - HNAV_H; }

// 한 화면에 들어가는 줄 수. 서버에 달라고 할 개수이기도 하다(feedFetch).
static uint8_t feedRows() {
  int room = (contentH() - HNAV_H * 2 - 4) / HROW;
  if (room < 1) room = 1;
  if (room > FEED_MAX) room = FEED_MAX;
  return (uint8_t)room;
}

// 페이지 띠 하나. 눌러도 소용없을 때는(첫 쪽에서 '이전') 회색으로 둔다 —
// 없애 버리면 띠가 사라졌다 나타났다 하면서 아래 목록 위치가 흔들린다.
static void drawNavBar(int y, bool up, bool on, const char* text) {
  const int x = BORDER, w = tft.width() - BORDER * 2;
  const uint16_t bar = bgShade(18);          // 바탕에서 한 단계 눌러 띠로 쓴다
  tft.fillRect(x, y, w, HNAV_H, bar);

  // 띠 위 글자도 바탕에서 끌어낸다 — 꺼진 쪽은 더 섞어 흐리게 둘 뿐이다.
  const uint16_t fg = on ? inkMain() : inkMix(55);
  const int cx = tft.width() / 2, cy = y + HNAV_H / 2;

  // 삼각형은 직접 그린다 — 한글 폰트에 ▲▼ 가 없다(상용 2350자만 구워 넣었다)
  const int tw = 5, th = 5, tx = x + 12;
  if (up) tft.fillTriangle(tx, cy + th, tx + tw * 2, cy + th, tx + tw, cy - th, fg);
  else    tft.fillTriangle(tx, cy - th, tx + tw * 2, cy - th, tx + tw, cy + th, fg);

  useFont(14);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bar);
  tft.drawString(text, cx, cy);
  useFont(0);
}

static void drawHistory() {
  clearContent();

  // 페이지 띠는 목록보다 먼저 그린다. 못 불러왔을 때도 띠는 남아 있어야
  // 그 자리를 눌러 다시 받아 볼 수 있다 — 띠가 없으면 탭을 나갔다 들어오는 수밖에 없다.
  //
  // 위 띠 — 이전 페이지(더 최근). 첫 쪽에서는 지금이 가장 최근이라고 말해 준다.
  drawNavBar(histNavTopY(), true, feedPage > 0,
             feedPage > 0 ? "이전 페이지" : "가장 최근");

  // 아래 띠 — 다음 페이지(더 예전). 몇 쪽을 보고 있는지도 여기에 적는다.
  char down[24];
  if (!feedOk)       snprintf(down, sizeof(down), "다시 불러오기");
  else if (feedMore) snprintf(down, sizeof(down), "다음 페이지 (%u쪽)", (unsigned)(feedPage + 1));
  else               snprintf(down, sizeof(down), "마지막 (%u쪽)",      (unsigned)(feedPage + 1));
  drawNavBar(histNavBotY(), false, !feedOk || feedMore, down);

  useFont(14);
  tft.setTextDatum(MC_DATUM);

  if (!feedOk) {
    contentText(inkSpend());
    tft.drawString("내역을 불러오지 못했습니다", tft.width() / 2, contentMid() - 14);
    contentText(inkMuted());
    tft.drawString("연결을 확인하세요", tft.width() / 2, contentMid() + 14);
    useFont(0);
    return;
  }

  if (!feedCount) {
    contentText(inkMuted());
    tft.drawString("아직 내역이 없습니다", tft.width() / 2, contentMid());
    useFont(0);
    return;
  }

  const int pad = 10;
  const int listTop = histNavTopY() + HNAV_H + 2;
  const uint32_t elapsed = (millis() - feedFetchMs) / 1000;
  uint8_t rows = feedCount;
  if (rows > feedRows()) rows = feedRows();

  // 한 줄 걸러 바탕을 한 단계 눌러 깐다. 예전에는 검정 위에 어두운 줄이었는데,
  // 바탕색을 관리자가 고르게 되면서 바탕에서 만들어 쓰는 쪽으로 바꿨다.
  const uint16_t stripe = bgShade(8);
  for (uint8_t i = 0; i < rows; i++) {
    const int y = listTop + i * HROW;
    const uint16_t bg = (i & 1) ? stripe : C_BG;
    if ((i & 1) && !contentHasBg) tft.fillRect(BORDER, y, tft.width() - BORDER * 2, HROW, bg);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(inkMain(), bg);
    tft.drawString(feed[i].who, BORDER + pad, y + HROW / 2);

    // 서버가 준 경과초에 기기가 깨어 있던 시간을 더한다
    const uint32_t ago = feed[i].agoSec + elapsed;
    char when[16];
    if      (ago < 60)   snprintf(when, sizeof(when), "방금");
    else if (ago < 3600) snprintf(when, sizeof(when), "%lu분 전", (unsigned long)(ago / 60));
    else                 snprintf(when, sizeof(when), "%lu시간 전", (unsigned long)(ago / 3600));
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(inkMuted(), bg);
    tft.drawString(when, tft.width() / 2 + 16, y + HROW / 2);

    char what[24];
    snprintf(what, sizeof(what), "%ld %s",
             (long)labs(feed[i].delta), feed[i].delta > 0 ? "지급" : "사용");
    tft.setTextColor(inkDir(feed[i].delta > 0), bg);
    tft.drawString(what, tft.width() - BORDER - pad, y + HROW / 2);
  }
  useFont(0);
}

// 페이지 띠를 눌렀는지. 0=위(최근 쪽) 1=아래(예전 쪽) -1=아님
static int8_t histNavHit(uint16_t tx, uint16_t ty) {
  if ((int)tx < BORDER || (int)tx >= tft.width() - BORDER) return -1;
  if ((int)ty >= histNavTopY() && (int)ty < histNavTopY() + HNAV_H) return 0;
  if ((int)ty >= histNavBotY() && (int)ty < histNavBotY() + HNAV_H) return 1;
  return -1;
}

// ── 탭 내용 위에 잠깐 덮는 화면들 ─────────────────────────────────
// 결과·오류는 몇 초 뒤 원래 탭으로 돌아간다(loop 의 overlayUntil).
static uint32_t overlayUntil = 0;

static void drawWorking() {
  clearContent();
  useFont(14);
  tft.setTextDatum(MC_DATUM);
  contentText(inkSub());
  tft.drawString("확인 중", tft.width() / 2, contentMid());
  useFont(0);
}

static void drawErrorScreen(const char* reason) {
  clearContent();
  const int cy = contentMid();
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkSpend());
  tft.drawString("처리하지 못했습니다", tft.width() / 2, cy - 18);
  useFont(14);
  contentText(inkMuted());
  tft.drawString(reason, tft.width() / 2, cy + 14);
  useFont(0);
}

// 잔액 부족 — 이름을 같이 보여줘야 누구 것인지 바로 안다.
static void drawLowBalance(const char* who, int32_t balance) {
  clearContent();
  const int cy = contentMid();
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(who, tft.width() / 2, cy - 46);
  useFont(14);
  contentText(inkSpend());
  tft.drawString("포인트가 모자랍니다", tft.width() / 2, cy - 16);
  useFont(0);

  char b[12];
  snprintf(b, sizeof(b), "%ld", (long)balance);
  contentText(inkMuted());
  tft.drawString(b, tft.width() / 2, cy + 26, 4);
}

// 태깅 결과 — "김용민 / 1 포인트 사용 / 남은 포인트 11".
// 부호는 쓰지 않는다. 방향은 테두리와 글자색이 말한다.
static void drawResultScreen(const char* who, const char* item, int32_t balance, int32_t delta) {
  clearContent();
  const int cy = contentMid();
  const uint16_t col = inkDir(delta > 0);

  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(who, tft.width() / 2, cy - 66);

  // "아이스크림 15 포인트 사용" — 무엇 때문에 오갔는지가 잔액보다 먼저 궁금하다.
  // 메뉴를 안 쓰는 기기는 항목 이름 없이 예전처럼 나온다.
  char line[64];
  if (item && item[0]) {
    snprintf(line, sizeof(line), "%s %ld 포인트 %s",
             item, (long)labs(delta), delta > 0 ? "지급" : "사용");
  } else {
    snprintf(line, sizeof(line), "%ld 포인트 %s",
             (long)labs(delta), delta > 0 ? "지급" : "사용");
  }
  useFont(14);
  contentText(col);
  tft.drawString(line, tft.width() / 2, cy - 40);
  useFont(0);

  // 남은 잔액은 숫자뿐이라 내장 폰트 6번(48px)으로 크게 그린다
  char b[12];
  snprintf(b, sizeof(b), "%ld", (long)balance);
  contentText(inkMain());
  tft.drawString(b, tft.width() / 2, cy + 6, 6);

  useFont(14);
  contentText(inkMuted());
  tft.drawString("남은 포인트", tft.width() / 2, cy + 48);
  useFont(0);
}

// ── 내역 탭에서 키링을 댔을 때 ────────────────────────────────────
// 이름 · 잔액 · 최근 몇 건. 지급도 사용도 하지 않는 "확인만" 하는 화면이라
// 방향색을 쓰지 않고, 잔액을 가장 크게 둔다 — 대는 사람이 궁금한 것이 그것이다.
static void drawWhoScreen() {
  clearContent();
  const int top = contentTop();

  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(whoName[0] ? whoName : whoUid, tft.width() / 2, top + 22);
  useFont(0);

  // 서버가 모르는 키링. 잔액 0 을 크게 띄우면 "0포인트인 사람" 으로 읽히므로 말로 알린다.
  if (!whoKnown) {
    useFont(14);
    contentText(inkSub());
    tft.drawString("아직 등록되지 않은 키링입니다", tft.width() / 2, top + 62);
    contentText(inkMuted());
    tft.drawString("지급 탭에서 대면 등록됩니다", tft.width() / 2, top + 88);
    useFont(0);
    return;
  }

  char b[12];
  snprintf(b, sizeof(b), "%ld", (long)whoBalance);
  contentText(inkMain());
  tft.drawString(b, tft.width() / 2, top + 56, 6);

  useFont(14);
  contentText(inkMuted());
  tft.drawString("남은 포인트", tft.width() / 2, top + 92);
  useFont(0);

  if (!whoCount) {
    useFont(14);
    contentText(inkMuted());
    tft.drawString("아직 내역이 없습니다", tft.width() / 2, top + 124);
    useFont(0);
    return;
  }

  // 최근 몇 건. 전체 내역과 같은 규칙(색이 방향, 부호는 안 쓴다)으로 그린다.
  tft.fillRect(BORDER + 20, top + 108, tft.width() - (BORDER + 20) * 2, 1, bgShade(20));

  const int ROW = 19, pad = 22;
  const uint32_t elapsed = (whoCount ? (millis() - whoFetchMs) / 1000 : 0);
  useFont(14);
  for (uint8_t i = 0; i < whoCount; i++) {
    const int y = top + 114 + i * ROW;

    char what[24];
    snprintf(what, sizeof(what), "%ld %s",
             (long)labs(whoFeed[i].delta), whoFeed[i].delta > 0 ? "지급" : "사용");
    tft.setTextDatum(ML_DATUM);
    contentText(inkDir(whoFeed[i].delta > 0));
    tft.drawString(what, BORDER + pad, y + ROW / 2);

    const uint32_t ago = whoFeed[i].agoSec + elapsed;
    char when[16];
    if      (ago < 60)   snprintf(when, sizeof(when), "방금");
    else if (ago < 3600) snprintf(when, sizeof(when), "%lu분 전", (unsigned long)(ago / 60));
    else                 snprintf(when, sizeof(when), "%lu시간 전", (unsigned long)(ago / 3600));
    tft.setTextDatum(MR_DATUM);
    contentText(inkMuted());
    tft.drawString(when, tft.width() - BORDER - pad, y + ROW / 2);
  }
  useFont(0);
}

// ══════════════════════════════════════════════════════════════════
//  진행 화면 — 키링 → 카드 → 완료
// ══════════════════════════════════════════════════════════════════
// 글자를 되도록 줄이고 그림으로 말한다. 대상이 어린이라 읽는 것보다 보는 것이 빠르고,
// 그림은 서버에서 갈아 끼울 수 있어 현장에서 말이 바뀌어도 펌웨어를 다시 굽지 않는다.

// 아래쪽 취소 단추. 화면 폭을 다 쓰는 띠라 손가락으로 누르기 쉽다.
#define BTN_H 46

static int btnY() { return tft.height() - BORDER - BTN_H; }

static void drawBigButton(const char* label, uint16_t col) {
  const int x = BORDER, w = tft.width() - BORDER * 2, y = btnY();
  tft.fillRoundRect(x, y, w, BTN_H, 10, col);
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, col);
  tft.drawString(label, tft.width() / 2, y + BTN_H / 2);
  useFont(0);
}

static bool btnHit(uint16_t tx, uint16_t ty) {
  return (int)tx >= BORDER && (int)tx < tft.width() - BORDER
      && (int)ty >= btnY() && (int)ty < btnY() + BTN_H;
}

// 단추가 차지하는 만큼을 뺀 내용 높이
static int bodyH() { return contentH() - BTN_H - 6; }

// ── 1단계: 누구에게 · 무엇을 (카드를 기다린다) ────────────────────
// 이름·잔액을 위에 두고 그 아래로 이 기기에서 쓸 수 있는 카드를 늘어놓는다.
// 확인 단추가 없어지면서 이 한 장이 "누구인지 보는 자리" 와 "카드를 대는 자리" 를
// 겸한다 — 잘못 댄 키링은 아래 '취소' 로 물린다.
//
// 등록된 카드를 목록으로 함께 보여준다. 어떤 카드가 있는지 모르면 아무 카드나
// 대 보게 되고, 그때마다 "이 기기에서 쓸 수 없는 카드" 가 떠서 답답해진다.
// 이름·잔액 줄과 카드 목록을 따로 그릴 수 있게 나눴다. 이름표에 있는 키링은 잔액을 받기 전에
// 이 화면부터 띄우고(잔액 자리는 '…'), 잔액이 오면 drawWaitRefresh 가 잔액과 카드 목록만 고친다 —
// 사진·이름을 다시 그리면 한 번 번쩍인다.
static const int WAIT_PAD   = 22;
static const int WAIT_BAL_W = 80;          // 잔액 자리 폭(내장 4번 폰트로 다섯 자리)
static int waitNameX = 0;                   // 마지막으로 그린 이름의 x — 잔액만 고칠 때 이름을 다시 얹는다
static bool waitHasPhoto = false;

static void drawWaitName(int top) {
  useFont(20);
  tft.setTextDatum(ML_DATUM);
  contentText(inkMain());
  tft.drawString(curName[0] ? curName : curUid, waitNameX, top + (waitHasPhoto ? 18 : 16));
  useFont(0);
}

static void drawWaitBalance(int top) {
  char b[12];
  if (curBalanceKnown) snprintf(b, sizeof(b), "%ld", (long)curBalance);
  else                 strlcpy(b, "...", sizeof(b));     // 받는 중 — 0 으로 보이면 잔액이 없는 줄 안다
  tft.setTextDatum(MR_DATUM);
  contentText(curBalanceKnown ? inkMain() : inkMuted());
  tft.drawString(b, tft.width() - BORDER - WAIT_PAD, top + 16, 4);
}

static void drawWaitList(int top) {
  const int pad = WAIT_PAD;
  // 등록된 카드를 모두 보여준다. 지급인지 사용인지는 금액의 색이 말한다
  // (초록 = 지급, 빨강 = 사용) — 이제 그것이 화면에서 방향을 아는 유일한 자리다.
  //
  // 잔액으로 감당이 안 되는 사용 카드는 흐리게 둔다. 대 보고 나서 "포인트가
  // 모자랍니다" 를 보는 것보다, 대기 전에 눈으로 아는 편이 낫다.
  // 잔액을 아직 받지 못했으면 흐리지 않는다 — 0 으로 쳐서 사용 카드가 모두 흐려지면 잘못 읽힌다.
  const int ROW = 24;
  int y = top + 66;
  uint8_t shown = 0;
  useFont(14);
  for (uint8_t i = 0; i < cardCount; i++) {
    if (y + ROW > contentTop() + bodyH()) break;
    const bool tooMuch = curBalanceKnown && cards[i].spend && curBalance - cards[i].amount < 0;
    tft.setTextDatum(ML_DATUM);
    contentText(tooMuch ? inkMuted() : inkSub());
    tft.drawString(cards[i].name, BORDER + pad, y + ROW / 2);

    char amt[12];
    snprintf(amt, sizeof(amt), "%ld", (long)cards[i].amount);
    tft.setTextDatum(MR_DATUM);
    contentText(tooMuch ? inkMuted() : inkDir(!cards[i].spend));
    tft.drawString(amt, tft.width() - BORDER - pad, y + ROW / 2);
    y += ROW;
    shown++;
  }
  if (!shown) {
    tft.setTextDatum(MC_DATUM);
    contentText(inkMuted());
    tft.drawString("등록된 카드가 없습니다", tft.width() / 2, top + 100);
  }
  useFont(0);
}

// ── 큰 사진 모양 ──
// 사진을 받아 둔 아이는 사진을 화면 폭보다 조금 작게(208) 크게 띄우고, 이름은 사진 윗부분에,
// '카드를 대주세요'·잔액은 아랫부분에 짙은 띠로 겹친다. 내용 높이(218)가 사진으로 차서 카드 목록은 싣지 않는다 —
// 어떤 카드를 댈지는 손에 든 카드가 말하고, 잔액이 모자라면 카드를 댈 때 알려 준다.
// 띠는 불투명이라 잔액이 오면 아래 띠만 다시 칠하면 된다(사진을 다시 그리지 않는다).
// 화면을 가로로 돌린 기기처럼 사진이 들어갈 높이가 안 되면 글자만 있는 모양으로 간다.
static bool waitBig = false;
static const int      BIG_BAND_H = 30;
static const uint16_t BIG_BAND   = 0x2104;   // 짙은 회색 — 어떤 사진 위에서도 흰 글자가 읽힌다

static int bigX() { return (tft.width() - PHOTO_PX) / 2; }
static int bigY() { return contentTop() + (bodyH() - PHOTO_PX) / 2; }
static bool bigFits() { return PHOTO_PX <= tft.width() - BORDER * 2 && PHOTO_PX <= bodyH(); }

// 띠 글자는 한글 폰트(FontKR)다. useFont 로 크기를 바꿀 때마다 글자표를 다시 읽어서, 예전처럼
// 윗띠(20) → 풀기 → 아랫띠(14) → 풀기 → 취소 단추(20) → 풀기 로 세 번 불러오면 띠가 사진보다 오래 걸렸다(64ms).
// 20px(이름·취소 단추) → 14px(안내·P) → 내장 폰트(잔액 숫자) 차례로 한 번씩만 바꾼다.
// 잔액이 오면 숫자 자리만 내장 폰트로 고친다(drawBigBalance) — 한글 폰트를 다시 부르지 않는다.
static const int BIG_BAL_W = 60;            // 잔액 숫자 자리 폭(내장 4번 폰트 네 자리)
static int bigBalRight = 0;                 // 잔액 숫자의 오른쪽 끝(P 앞)

static void drawBigBalance() {
  const int y = bigY() + PHOTO_PX - 8 - BIG_BAND_H, cy = y + BIG_BAND_H / 2;
  useFont(0);
  tft.fillRect(bigBalRight - BIG_BAL_W, y + 2, BIG_BAL_W, BIG_BAND_H - 4, BIG_BAND);
  char b[12];
  if (curBalanceKnown) snprintf(b, sizeof(b), "%ld", (long)curBalance);
  else                 strlcpy(b, "...", sizeof(b));     // 받는 중 — 0 으로 보이면 잔액이 없는 줄 안다
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(curBalanceKnown ? TFT_WHITE : 0x8410, BIG_BAND);
  tft.drawString(b, bigBalRight, cy + 1, 4);
}

static void drawBigBands() {
  const int x = bigX() + 8, w = PHOTO_PX - 16;
  const int ty = bigY() + 8;
  const int by = bigY() + PHOTO_PX - 8 - BIG_BAND_H, cy = by + BIG_BAND_H / 2;
  tft.fillRoundRect(x, ty, w, BIG_BAND_H, 8, BIG_BAND);
  tft.fillRoundRect(x, by, w, BIG_BAND_H, 8, BIG_BAND);

  // 20px — 이름, 그리고 같은 폰트로 취소 단추(drawBigButton 은 끝에 폰트를 푼다)
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, BIG_BAND);
  tft.drawString(curName[0] ? curName : curUid, bigX() + PHOTO_PX / 2, ty + BIG_BAND_H / 2);
  drawBigButton("취소", C_NEUTRAL);

  // 14px — 안내와 P
  useFont(14);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(0xC618, BIG_BAND);            // 옅은 회색 — 안내는 잔액보다 한 단계 물린다
  tft.drawString("카드를 대주세요", x + 10, cy);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_WHITE, BIG_BAND);
  tft.drawString("P", x + w - 10, cy + 2);
  bigBalRight = x + w - 12 - tft.textWidth("P");

  drawBigBalance();                              // 안에서 폰트를 푼다
}

static void drawWaitCard() {
  const int top = contentTop();
  const int pad = WAIT_PAD;

  waitBig = false;
  const char* img = photoOf(curUid);
  if (img && bigFits()) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, img);
    if (LittleFS.exists(path)) {
      // 사진이 덮을 가운데는 배경을 그리지 않는다 — 둥근 모서리(8%) 틈만큼 위아래 줄은 그린다
      const uint32_t t0 = millis();
      clearContentHole(bigX(), bigY(), PHOTO_PX, PHOTO_PX, PHOTO_PX * 8 / 100 + 2);
      const uint32_t t1 = millis();
      ArtFile a;
      a.base = 0;
      strlcpy(a.file, img, sizeof(a.file));
      a.w = PHOTO_PX; a.h = PHOTO_PX;
      if (drawArtFile(a, bigX(), bigY(), true)) {  // 덜 받은 파일이면 false
        const uint32_t t2 = millis();
        waitBig = true;
        drawBigBands();
        Serial.printf("[시간] 큰 사진 화면 — 바탕 %lums · 사진 %lums · 띠 %lums\n",
                      (unsigned long)(t1 - t0), (unsigned long)(t2 - t1), (unsigned long)(millis() - t2));
        return;
      }
    }
  }
  clearContent();                                  // 사진이 없거나 그리다 멈췄으면 글자 모양으로

  // ── 글자만 있는 모양(사진이 없거나 아직 못 받았을 때) ──
  // 이름은 왼쪽, 잔액은 오른쪽으로 한 줄에 묶었다. 카드 목록에 자리를 내주려고
  // 잔액을 48px 에서 26px(내장 4번 폰트)로 줄였다 — 여기서 크게 볼 것은 카드다.
  waitHasPhoto = false;
  waitNameX = BORDER + pad;
  drawWaitName(top);
  drawWaitBalance(top);

  useFont(14);
  contentText(inkSub());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("카드를 대주세요", tft.width() / 2, top + 44);
  useFont(0);

  tft.fillRect(BORDER + 20, top + 58, tft.width() - (BORDER + 20) * 2, 1,
               lightBg() ? C_LINE : C_TABBG);

  drawWaitList(top);
  drawBigButton("취소", C_NEUTRAL);
}

// 잔액을 받은 뒤 — 잔액 자리와 카드 목록만 바탕으로 되돌리고 다시 그린다(사진·이름·안내는 그대로).
// 이름이 길어 잔액 자리까지 닿았을 수 있어 이름도 한 번 더 얹는다(같은 자리에 같은 글자라 티가 나지 않는다).
static void drawWaitRefresh() {
  if (waitBig) { drawBigBalance(); return; }       // 큰 사진 모양 — 잔액 숫자 자리만 고친다
  const int top = contentTop();
  restoreContent(tft.width() - BORDER - WAIT_PAD - WAIT_BAL_W, top + 2, WAIT_BAL_W, 28);
  drawWaitName(top);
  drawWaitBalance(top);
  const int listTop = top + 60;
  restoreContent(BORDER, listTop, tft.width() - BORDER * 2, contentTop() + bodyH() - listTop);
  drawWaitList(top);
}

// ── 3단계: 되었습니다 ─────────────────────────────────────────────
// 서버에서 올린 그림 한 장으로 말한다. 글자는 잔액과 증감 한 줄뿐이다.
static void drawDone() {
  drawFrame();            // 이 화면에서만 테두리가 방향색이 된다(frameColor)
  clearContent();
  const int top = contentTop();
  const bool up = doneDelta > 0;
  const int idx = up ? 0 : 1;

  // 그림이 있으면 그림에 자리를 몰아준다. 이 화면은 "되었습니다" 를 그림 한 장으로
  // 말하는 자리라, 글자는 얼마와 남은 포인트 두 줄이면 충분하다.
  //
  // 그 카드의 그림이 먼저다 — 출석 카드면 출석 그림, 간식 카드면 간식 그림이
  // 뜬다. 카드에 그림이 없을 때만 지급·사용 공통 그림으로 떨어진다.
  int y = top + 10;
  bool drew = false;
  ArtFile pic;
  bool hasPic = false;
  if (curCard >= 0 && cards[curCard].img[0]) {
    pic.base = 0;
    strlcpy(pic.file, cards[curCard].img, sizeof(pic.file));
    pic.w = cards[curCard].imgW;
    pic.h = cards[curCard].imgH;
    hasPic = true;
  } else if (doneHas[idx]) {
    pic = doneFile[idx];
    hasPic = true;
  }
  if (hasPic && pic.h <= contentH() - 70 && pic.w <= tft.width() - BORDER * 2) {
    drew = drawArtFile(pic, (tft.width() - pic.w) / 2, y, true);
    if (drew) y += pic.h + 6;
  }

  char amt[16];
  snprintf(amt, sizeof(amt), "%ld", (long)labs(doneDelta));
  tft.setTextDatum(MC_DATUM);
  contentText(inkDir(up));

  if (drew) {
    // 그림이 이미 크게 말하고 있어 숫자는 한 단계 작게(내장 폰트 4번, 26px)
    tft.drawString(amt, tft.width() / 2, y + 14, 4);
    y += 32;
  } else {
    // 그림이 없으면 예전처럼 이름을 띄우고 숫자를 크게 그린다
    useFont(20);
    contentText(inkMain());
    tft.drawString(curName[0] ? curName : curUid, tft.width() / 2, y + 14);
    useFont(0);
    y += 34;
    contentText(inkDir(up));
    tft.drawString(amt, tft.width() / 2, y + 22, 6);
    y += 48;
  }

  useFont(14);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMuted());
  char rest[40];
  if (drew) {
    snprintf(rest, sizeof(rest), "%s · %s %ld 남음",
             curName[0] ? curName : curUid, up ? "지급" : "사용", (long)doneBalance);
  } else {
    snprintf(rest, sizeof(rest), "%s · 남은 포인트 %ld", up ? "지급" : "사용", (long)doneBalance);
  }
  tft.drawString(rest, tft.width() / 2, y + 6);
  useFont(0);
}

static void drawNoNfc() {
  clearContent();
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkSpend());
  tft.drawString("NFC 모듈 없음", tft.width() / 2, contentMid());
  useFont(0);
}

// 내역 한 묶음을 받아 온다. 왕복이 1초쯤 걸려서 그동안 화면이 멈춘 것처럼 보이지
// 않게 안내를 먼저 띄운다. 그리는 것은 부른 쪽에서 한다(탭 전환이면 drawScreen,
// 페이지 넘김이면 drawHistory — 테두리·헤더는 그대로라 목록만 다시 그리면 된다).
static void historyLoad(uint8_t page) {
  clearContent();
  useFont(14);
  tft.setTextDatum(MC_DATUM);
  contentText(inkSub());
  tft.drawString("불러오는 중", tft.width() / 2, contentMid());
  useFont(0);
  feedFetch(page);
}

// 진행 중이던 것을 접고 대기로 돌아간다. 사람·카드를 함께 비운다 —
// 남겨 두면 다음 사람이 앞사람의 이름 위에 겹쳐 처리될 수 있다.
static void resetStep() {
  step = STEP_IDLE;
  stepAt = millis();
  curUid[0] = '\0'; curName[0] = '\0';
  curBalance = 0; curCard = -1;
  curBalanceKnown = true;
  whoCount = 0;
  lastUid[0] = '\0';                  // 같은 키링을 곧바로 다시 댈 수 있게 쿨다운을 푼다
}

// 현재 탭의 내용. 오버레이가 떠 있는 동안에는 부르지 않는다.
static void drawTabContent() {
  if (!nfcReady && tab != TAB_HISTORY) { drawNoNfc(); return; }
  if (tab == TAB_HISTORY) { drawHistory(); return; }
  switch (step) {
    case STEP_CARD:    drawWaitCard(); break;
    case STEP_DONE:    drawDone();     break;
    default:           drawTagScreen(); break;   // STEP_IDLE — "키링을 대주세요"
  }
}

// 화면 전체. 탭이 바뀌면 테두리 색도 바뀌므로 통째로 다시 그린다.
static void drawScreen() {
  drawFrame();
  drawHeader();
  drawTabContent();
}

// ══════════════════════════════════════════════════════════════════
//  딥슬립 — 진입 경로는 이 함수 하나로 통일한다
// ══════════════════════════════════════════════════════════════════
static void goToDeepSleep() {
  // 상시 전원 기기가 대부분이라 기본은 꺼져 있다. 서버에서 켠 기기만 잠든다.
  if (!cfg.sleepEnabled) return;

  // 라디오를 먼저 끈다. 켠 채로 잠들면 전류가 크게 새어 배터리가 빨리 준다.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  sndPowerOff();          // beep() 안에서 재생 시간을 기다리므로 잘리지 않는다
  backlight(false);
  ledWrite(LED_OFF);      // WS2812 는 칩이 잠들어도 마지막 색을 들고 있다 — 끄고 잔다
  tft.writecommand(0x10); // ILI9341 sleep in

  // 정전식 패드를 걷어냈으므로 touchSleepWakeUpEnable 로는 깨울 수 없다.
  // XPT2046 은 화면을 누르면 T_IRQ 를 LOW 로 떨어뜨린다 — 그 하강을 기상 신호로 쓴다.
  // ext0 는 RTC 핀만 받는다 — PIN_TOUCH_IRQ 를 RTC 핀으로 고른 이유다(핀 정의의 주석 참고).
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_TOUCH_IRQ, 0);

  Serial.println("딥슬립 진입");
  Serial.flush();
  esp_deep_sleep_start();
}

// ══════════════════════════════════════════════════════════════════
//  블루투스 와이파이 설정 — 못 붙었을 때만 연다
// ══════════════════════════════════════════════════════════════════
// 와이파이에 못 붙으면 이 화면에서 멈춰 휴대폰을 기다린다. 그동안 다른 일을 해도
// 어차피 서버에 못 물어보니, 붙을 때까지 여기서 붙잡는 편이 화면도 코드도 단순하다.
//
// ── GATT 구성 ─────────────────────────────────────────────────────
//   서비스  7f3e1a00-4b21-4c8e-9a11-2d5f6c8e2001   (ChurchDisplayRxBLE 와 같은 뿌리)
//     WIFI  ...2002  WRITE        휴대폰 → 기기
//                    {"ssid":"우리교회","pass":"비밀번호"}   넣고 곧바로 붙어 본다
//                    {"cmd":"scan"}    주변 목록을 훑어 SCAN 에 담는다
//                    {"cmd":"forget"}  저장해 둔 것을 지운다(구워 넣은 것으로 돌아간다)
//     STATE ...2003  READ·NOTIFY  기기 → 휴대폰
//                    {"state":"wait|scan|try|ok|fail","ssid":"...","ip":"...","msg":"..."}
//     SCAN  ...2004  READ         [{"ssid":"...","rssi":-52,"lock":true}, ...] 최대 10개
//
// 비밀번호는 절대 읽어 주지 않는다 — WIFI 는 쓰기 전용이고, STATE 에도 이름만 싣는다.
// 광고도 설정 모드일 때만 한다. 나올 때 deinit(true) 로 메모리까지 돌려주므로
// 평소 동작에는 블루투스가 아예 없는 것과 같다.
#define UUID_PROV_SVC   "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e2001"
#define UUID_PROV_WIFI  "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e2002"
#define UUID_PROV_STATE "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e2003"
#define UUID_PROV_SCAN  "7f3e1a00-4b21-4c8e-9a11-2d5f6c8e2004"

// 아무도 오지 않으면 접는다. 새벽에 공유기가 죽어 재부팅된 기기가 설정 화면을
// 붙들고 있으면, 아침에 오는 사람은 그것이 고장인 줄 안다 — 끊긴 대기 화면이 낫다.
// (시험용 빌드에서는 접지 않는다 — 휴대폰을 들고 오는 데 5분이 넘게 걸린다)
#ifdef FORCE_WIFI_SETUP
#define PROV_TIMEOUT_MS 0xFFFFFFFFUL
#else
#define PROV_TIMEOUT_MS 300000UL     // 5분
#endif
#define PROV_BTN_H      44

static NimBLECharacteristic* chrProvState = nullptr;
static NimBLECharacteristic* chrProvScan  = nullptr;
static char provName[24] = "";                 // 광고 이름 — 화면에도 이것을 띄운다
static bool provAdvOn    = false;              // 광고가 실제로 섰나(못 섰으면 화면으로 알린다)
static char provMsg[48]  = "";                 // 화면 아래 상태줄
// BLE 콜백은 NimBLE 호스트 태스크에서 돈다. 거기서 와이파이나 플래시를 만지면
// 그 태스크가 몇 초씩 멈춰 연결이 끊긴다 — 값만 받아 두고 아래 루프에서 처리한다.
static volatile bool provHasCreds = false, provWantScan = false, provWantForget = false;
static char provSsid[33] = "", provPass[64] = "";

// ── 화면 ──
// 탭도 배경 그림도 쓰지 않고 스스로 그린다. 부팅 중(탭을 고르기 전)에도 떠야 하고,
// 이 화면에서 할 일은 하나뿐이라 다른 것이 끼어들 이유가 없다.
static int provTop()   { return BORDER + HEAD_H; }
static int provBtnY()  { return tft.height() - BORDER - PROV_BTN_H; }
static int provMsgY()  { return provBtnY() - 34; }

// 0 = 다시 시도, 1 = 건너뛰기, -1 = 단추 밖
static int8_t provBtnHit(uint16_t tx, uint16_t ty) {
  if ((int)ty < provBtnY() || (int)ty >= provBtnY() + PROV_BTN_H) return -1;
  const int mid = tft.width() / 2;
  if ((int)tx < BORDER || (int)tx >= tft.width() - BORDER) return -1;
  return (int)tx < mid ? 0 : 1;
}

static void provDrawButtons() {
  const int gap = 8, y = provBtnY();
  const int w = (tft.width() - BORDER * 2 - gap) / 2;
  const int x2 = tft.width() - BORDER - w;
  tft.fillRoundRect(BORDER, y, w, PROV_BTN_H, 10, C_EARN);
  tft.fillRoundRect(x2,     y, w, PROV_BTN_H, 10, C_NEUTRAL);
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, C_EARN);
  tft.drawString("다시 시도", BORDER + w / 2, y + PROV_BTN_H / 2);
  tft.setTextColor(TFT_WHITE, C_NEUTRAL);
  tft.drawString("건너뛰기", x2 + w / 2, y + PROV_BTN_H / 2);
  useFont(0);
}

// 상태줄만 다시 그린다. 붙는 동안 몇 번씩 바뀌는 자리라 화면을 통째로 다시
// 그리면 깜빡여서 "먹통" 으로 보인다.
static void provDrawMsg() {
  const uint16_t bg = hexToColor(cfg.bgColor);
  const uint16_t ink = inkMain();
  tft.fillRect(BORDER, provMsgY() - 12, tft.width() - BORDER * 2, 26, bg);
  useFont(14);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ink, bg);
  tft.drawString(provMsg, tft.width() / 2, provMsgY());
  useFont(0);
}

static void provSay(const char* msg) {
  strlcpy(provMsg, msg, sizeof(provMsg));
  Serial.printf("[설정] %s\n", msg);
  provDrawMsg();
}

static void drawProvScreen() {
  const uint16_t bg  = hexToColor(cfg.bgColor);
  const uint16_t ink = inkMain();       // 여기도 다른 화면과 같은 대비 규칙을 쓴다
  const uint16_t sub = inkSub();
  const int y = provTop();
  tft.fillRect(BORDER, y, tft.width() - BORDER * 2, tft.height() - y - BORDER, bg);

  useFont(20);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ink, bg);
  tft.drawString("와이파이 설정", tft.width() / 2, y + 22);

  useFont(14);
  tft.setTextColor(sub, bg);
  tft.drawString("휴대폰 블루투스에서", tft.width() / 2, y + 52);
  useFont(20);
  tft.setTextColor(ink, bg);
  tft.drawString(provName, tft.width() / 2, y + 78);
  useFont(14);
  tft.setTextColor(sub, bg);
  tft.drawString("에 연결해 주세요", tft.width() / 2, y + 102);
  useFont(0);

  provDrawMsg();
  provDrawButtons();
}

// ── GATT ──
static void provSetState(const char* state, const char* msg) {
  if (!chrProvState) return;
  JsonDocument d;
  d["state"]  = state;
  d["device"] = TALENT_DEVICE_ID;
  const char* ssid = provSsid[0] ? provSsid : (wifiSsid[0] ? wifiSsid : WIFI_SSID);
  d["ssid"] = ssid;                                 // 이름만. 비밀번호는 내보내지 않는다
  if (WiFi.status() == WL_CONNECTED) d["ip"] = WiFi.localIP().toString();
  if (msg && msg[0]) d["msg"] = msg;
  char out[200];
  const size_t n = serializeJson(d, out, sizeof(out));
  chrProvState->setValue((uint8_t*)out, n);
  chrProvState->notify();
}

class ProvWifiCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    NimBLEAttValue v = c->getValue();
    JsonDocument d;
    if (deserializeJson(d, v.c_str())) {
      provSetState("fail", "보낸 값을 읽지 못했습니다");
      return;
    }
    const char* cmd = d["cmd"] | "";
    if (!strcmp(cmd, "scan"))   { provWantScan   = true; return; }
    if (!strcmp(cmd, "forget")) { provWantForget = true; return; }

    const char* ssid = d["ssid"] | "";
    if (!ssid[0]) { provSetState("fail", "ssid 가 비어 있습니다"); return; }
    strlcpy(provSsid, ssid,            sizeof(provSsid));
    strlcpy(provPass, d["pass"] | "",  sizeof(provPass));
    provHasCreds = true;                            // 실제 접속은 루프에서
  }
};
static ProvWifiCB provWifiCB;

static void provStart() {
  snprintf(provName, sizeof(provName), "funfun-%s", TALENT_DEVICE_ID);

  NimBLEDevice::init(provName);
  NimBLEDevice::setMTU(247);                        // 짧은 JSON 이지만 한 번에 오가게

  NimBLEServer*  srv = NimBLEDevice::createServer();
  srv->advertiseOnDisconnect(true);
  NimBLEService* svc = srv->createService(UUID_PROV_SVC);

  NimBLECharacteristic* w = svc->createCharacteristic(UUID_PROV_WIFI, NIMBLE_PROPERTY::WRITE);
  w->setCallbacks(&provWifiCB);
  chrProvState = svc->createCharacteristic(UUID_PROV_STATE,
                                           NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  chrProvScan  = svc->createCharacteristic(UUID_PROV_SCAN, NIMBLE_PROPERTY::READ);
  chrProvScan->setValue("[]");
  svc->start();

  NimBLEAdvertising* a = NimBLEDevice::getAdvertising();

  // 순서가 중요하다: enableScanResponse() 는 "데이터를 이미 넣었다" 표시(m_advDataSet)를
  // 지운다. 데이터를 넣은 뒤에 켜면 start() 가 페이로드를 처음부터 다시 만들어
  // 여기서 맞춰 둔 것이 날아간다. 그래서 켜는 것을 먼저 한다.
  a->enableScanResponse(true);

  // 광고 31바이트에 이름(최대 21B)과 128비트 UUID(18B)를 함께 넣으면 넘친다.
  // 이름만 광고하고 UUID 는 스캔 응답으로 보낸다 — 휴대폰 목록에는 이름이 보여야 한다.
  NimBLEAdvertisementData adv, rsp;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  const bool okName = adv.setName(provName);
  const bool okUuid = rsp.setCompleteServices(NimBLEUUID(UUID_PROV_SVC));
  const bool okAdv  = a->setAdvertisementData(adv);
  const bool okRsp  = a->setScanResponseData(rsp);

  // 호스트가 아직 동기화되지 않았으면 광고가 서지 않는다("Host not synced!").
  // init() 이 돌아온 직후 몇 십 ms 가 그 상태라, 설 때까지 짧게 다시 걸어 본다.
  provAdvOn = false;
  for (uint8_t i = 0; i < 20 && !provAdvOn; i++) {
    provAdvOn = a->start();
    if (!provAdvOn) delay(100);
  }

  Serial.printf("[설정] 블루투스 광고 %s — %s (서비스 %s)\n",
                provAdvOn ? "시작" : "실패", provName, UUID_PROV_SVC);
  if (!okName || !okUuid || !okAdv || !okRsp)
    Serial.printf("[설정] 광고 데이터 이름=%d UUID=%d adv=%d rsp=%d\n",
                  okName, okUuid, okAdv, okRsp);
}

static void provStop() {
  NimBLEDevice::deinit(true);        // 메모리까지 돌려준다 — 평소에는 들고 있지 않는다
  chrProvState = chrProvScan = nullptr;
  Serial.printf("[설정] 블루투스 종료, 여유 힙=%lu B\n", (unsigned long)ESP.getFreeHeap());
}

// 주변 목록. 휴대폰에서 이름을 손으로 치는 것보다 골라 주는 편이 오타가 없다.
static void provScan() {
  provSay("주변 와이파이를 찾는 중");
  provSetState("scan", "");
  const int n = WiFi.scanNetworks();               // 신호 센 순서로 돌아온다

  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (int i = 0; i < n && i < 10; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["lock"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  char out[512];
  const size_t len = serializeJson(d, out, sizeof(out));
  if (chrProvScan) chrProvScan->setValue((uint8_t*)out, len);
  WiFi.scanDelete();

  char msg[48];
  snprintf(msg, sizeof(msg), "%d 개를 찾았습니다", n < 0 ? 0 : n);
  provSay(msg);
  provSetState("wait", msg);
}

// 붙을 때까지(또는 '건너뛰기' · 5분) 여기 머문다. 연결됐으면 true.
static bool provisionMode() {
  drawFrame();
  drawHeader();                       // 헤더의 '끊김' 이 왜 이 화면인지 말해 준다
  provStart();
  provMsg[0] = '\0';
  drawProvScreen();
  // 광고가 서지 않으면 휴대폰에서 아예 보이지 않는다. 빈 화면으로 기다리게 두면
  // "고장" 이 되므로, 기기를 다시 켜라는 말을 여기서 한다.
  provSay(provAdvOn ? "기다리는 중" : "블루투스를 열지 못했습니다");
  provSetState("wait", "");
  sndFail();                          // 손이 필요한 상태라는 신호

  bool ok = false;
  const uint32_t t0 = millis();
  uint32_t lastTouch = 0;

  while (millis() - t0 < PROV_TIMEOUT_MS) {
    // 이 화면에서는 loop 가 돌지 않는다 — 여기서 LED 를 갱신한다(처음 3초는 실패 빨강, 그 뒤 연결 중 노랑)
    ledUpdate(millis());

    // ── 휴대폰이 보낸 것 ──
    if (provWantForget) {
      provWantForget = false;
      wifiForgetCreds();
      provSay("저장해 둔 설정을 지웠습니다");
      provSetState("wait", "저장해 둔 설정을 지웠습니다");
    }
    if (provWantScan) {
      provWantScan = false;
      provScan();
    }
    if (provHasCreds) {
      provHasCreds = false;
      char msg[48];
      snprintf(msg, sizeof(msg), "%s 에 붙는 중", provSsid);
      provSay(msg);
      provSetState("try", "");
      if (wifiTry(provSsid, provPass, 12000)) {
        wifiSaveCreds(provSsid, provPass);          // 다음 부팅부터는 이것으로 붙는다
        provSay("연결됐습니다");
        provSetState("ok", "");
        sndEarn();
        delay(1200);                                // 휴대폰이 결과를 읽을 틈
        ok = true;
        break;
      }
      provSay("붙지 못했습니다");
      provSetState("fail", "이름이나 비밀번호를 확인해 주세요");
      sndFail();
    }

    // ── 화면 단추 ──
    uint16_t tx, ty;
    if (tft.getTouch(&tx, &ty) && millis() - lastTouch > cfg.touchDebounceMs) {
      lastTouch = millis();
      const int8_t b = provBtnHit(tx, ty);
      if (b == 0) {                                 // 다시 시도 — 알고 있는 것으로 한 번 더
        sndMode();
        provSay("다시 붙어 보는 중");
        provSetState("try", "");
        if (wifiConnectKnown(8000)) { ok = true; provSetState("ok", ""); sndEarn(); break; }
        provSay("아직 붙지 못했습니다");
        provSetState("fail", "");
      } else if (b == 1) {                          // 건너뛰기 — 끊긴 채로 쓴다
        sndMode();
        break;
      }
    }
    delay(20);
  }

  provStop();
  if (ok) Serial.printf("[와이파이] 연결됨 %s\n", WiFi.localIP().toString().c_str());
  else    Serial.println("[와이파이] 설정 모드를 나갑니다 — 끊긴 채로 계속합니다");
  return ok;
}

// 붙고 나서 서버에서 받아 오는 것들. 부팅과 설정 모드 뒤가 같은 길을 쓴다.
static void afterOnline() {
  Serial.printf("WiFi 연결됨 %s\n", WiFi.localIP().toString().c_str());
  // 설정을 받아온다. 실패해도 캐시(또는 기본값)로 계속 간다.
  bootStatus("설정 받는 중");
  if (cfgFetch()) {
    bootStatus("카드 목록 받는 중");
    cardsFetch();                      // 카드 목록도 함께 받아 둔다
    analogWrite(PIN_TFT_BL, cfg.backlight);  // 밝기만 바로 반영(다시 페이드하면 깜빡인다)
  }
  // 이름표는 태깅 즉시 이름을 띄우기 위해 미리 받아 둔다.
  // 내역은 그 탭을 열 때 받는다 — 부팅을 그만큼 늦출 이유가 없다.
  bootStatus("명단 받는 중");
  if (rosterFetch()) bootStatus("명단 %u명 받음", (unsigned)rosterCount);
  else               bootStatus("명단을 받지 못했습니다");
  // 그림은 없는 것만 받는다. 이미 있으면 통신하지 않아 부팅이 늦어지지 않는다.
  // (새로 받을 그림이 있으면 artDownload 가 '그림 받는 중' 으로 줄을 바꾼다)
  artSync();
  lastCfgFetch = millis();
}

// ══════════════════════════════════════════════════════════════════
//  태그 처리
// ══════════════════════════════════════════════════════════════════
static void uidToStr(const uint8_t* uid, uint8_t len, char* out, size_t cap) {
  size_t n = 0;
  for (uint8_t i = 0; i < len && n + 2 < cap; i++) n += snprintf(out + n, cap - n, "%02X", uid[i]);
  out[n] = '\0';
}

// 결과·오류를 몇 초 띄우고 원래 탭으로 돌아간다.
#define OVERLAY_MS 3000
// 헤더 띠 두 번 터치로 보는 시간 — 첫 누름 뒤 이 안에 한 번 더 누르면 두 번(학생 사진 모두 받기)
#define HEAD_DOUBLE_MS 450
// 내역 탭의 잔액 조회는 읽을 것이 여러 줄이라 조금 더 오래 둔다.
#define WHO_OVERLAY_MS 5000

// 키링이 아닌 것을 댔을 때의 안내. 쿨다운은 handleTag 와 같은 변수를 쓴다 —
// 카드를 리더 위에 올려 둔 채로 두면 안내가 끝없이 다시 뜨기 때문이다.
// 응답은 하는데 NDEF 가 아닌 카드. 지급/사용 카드로 쓰려고 댄 것일 수 있으니
// 서버에 알려 관리자 화면의 '미등록카드' 에 뜨게 하고, 무엇을 하라고 알려 준다.
// (예전에는 이런 카드를 "키링이 아닙니다" 로 내치기만 해서, 지급/사용 카드를
//  리더에 대도 아무 데도 나타나지 않아 등록할 길이 없었다)
static void unknownCard(const char* s) {
  uint32_t now = millis();
  if (!strcmp(s, lastUid) && now - lastTagMs < cfg.tagCooldownMs) return;
  strncpy(lastUid, s, sizeof(lastUid) - 1);
  lastTagMs = now;

  reportSeen(s);

  sndFail();
  clearContent();
  useFont(14);
  tft.setTextDatum(MC_DATUM);
  contentText(inkSub());
  tft.drawString("처음 보는 카드입니다", tft.width() / 2, contentMid() - 14);
  contentText(inkMuted());
  tft.drawString("관리자 화면에서 등록하세요", tft.width() / 2, contentMid() + 14);
  useFont(0);
  overlayUntil = millis() + 2200;
  lastActivity = millis();
}

static void rejectTag(const char* s) {
  uint32_t now = millis();
  if (!strcmp(s, lastUid) && now - lastTagMs < cfg.tagCooldownMs) return;
  strncpy(lastUid, s, sizeof(lastUid) - 1);
  lastTagMs = now;

  sndFail();
  clearContent();
  useFont(14);
  tft.setTextDatum(MC_DATUM);
  contentText(inkSub());
  tft.drawString("펀펀포인트 키링이", tft.width() / 2, contentMid() - 14);
  tft.drawString("아닙니다",         tft.width() / 2, contentMid() + 14);
  useFont(0);
  overlayUntil = millis() + 2200;
  lastActivity = millis();
}

// 카드가 정해졌을 때 실제로 처리한다.
static void commitCard() {
  if (curCard < 0) return;
  const CardEntry& c = cards[curCard];

  drawWorking();
  TalentResult r = talentPost(c.spend ? "spend" : "earn", curUid, c.amount, c.name, c.uid);

  if (r.ok) {
    (r.delta > 0 ? sndEarn : sndSpend)();
    doneDelta = r.delta;
    doneBalance = r.balance;
    step = STEP_DONE;
    stepAt = millis();
    drawDone();
    Serial.printf("%s %s(%s) %s → %ld\n", r.delta > 0 ? "지급" : "사용",
                  curUid, curName, c.name, (long)r.balance);
    feedFetchMs = 0;                   // 다음에 내역을 열면 방금 것이 맨 위에 오게
  } else if (r.lowBalance) {
    sndFail();
    drawLowBalance(curName[0] ? curName : curUid, r.balance);
    overlayUntil = millis() + OVERLAY_MS;
    Serial.printf("포인트 부족 %s(%s)\n", curUid, curName);
  } else {
    sndFail();
    drawErrorScreen(r.reason);
    overlayUntil = millis() + OVERLAY_MS;
    Serial.printf("실패 %s — %s\n", curUid, r.reason);
  }
  lastActivity = millis();
}

// 출석모드 — 카드 단계를 건너뛴다. 설정에 적힌 카드를 쓰고, 비어 있으면
// 이 기기에서 쓸 수 있는 첫 카드를 쓴다.
static int8_t attendanceCardIndex() {
  if (cfg.attendanceCard[0]) {
    const int8_t i = cardIndexOf(cfg.attendanceCard);
    if (i >= 0) return i;
  }
  return cardCount ? 0 : -1;     // 정해 둔 것이 없으면 첫 카드
}

// 취소 단추를 눌렀을 때. 진행 중이던 것을 접고 대기로 돌아간다 —
// 키링을 잘못 댔거나 다른 사람이 먼저 대 버렸을 때 쓰는 자리다.
// (완료 화면에는 단추가 없다 — 시간이 지나면 저절로 돌아간다)
// ── 헤더 띠 한 번 터치 ── 내역으로 가거나(끊겼으면 와이파이 설정) 대기로 돌아온다.
// 두 번 터치(학생 사진 모두 받기)와 가르느라 loop 가 HEAD_DOUBLE_MS 만큼 기다린 뒤 부른다.
static void headerTap(uint32_t now) {
  // 끊겨 있으면 내역 대신 와이파이 설정으로 간다. 여기가 '끊김' 이라고 적혀
  // 있는 자리이고, 끊긴 채로는 내역도 받아 오지 못한다 — 눌러서 할 수 있는
  // 일이 그것뿐이라 그리로 보낸다.
  if (WiFi.status() != WL_CONNECTED && tab != TAB_HISTORY) {
    if (provisionMode()) afterOnline();
    resetStep();
    drawScreen();
    lastActivity = millis();
    return;
  }
  // 다시 누르면 대기 화면으로 돌아온다 — 띠 전체가 단추라 "나가는 문" 도
  // 같은 자리여야 헤맬 일이 없다.
  overlayUntil = 0;
  resetStep();
  sndMode();
  if (tab == TAB_HISTORY) {
    tab = TAB_MAIN;
  } else {
    tab = TAB_HISTORY;
    // 열 때는 늘 가장 최근 묶음부터 — 지난번에 넘겨 둔 쪽에서 시작하면
    // 방금 찍힌 것이 안 보여 "안 들어갔다" 로 읽힌다.
    if (feedFetchMs == 0 || now - feedFetchMs > 30000 || feedPage != 0) {
      drawFrame(); drawHeader();
      historyLoad(0);
    }
  }
  drawScreen();
  lastActivity = millis();
}

// ── 학생 사진 모두 받기 (헤더 띠 두 번 터치) ──────────────────────
// 평소에는 대기 중 조용할 때 한 장씩 받는다(photoSyncStep). 행사 전에 사진을 한꺼번에 올렸거나,
// 리더를 새로 들여 사진이 하나도 없을 때 기다리지 않고 지금 다 받게 한다.
// 명단을 새로 받고 → 사진이 있는 아이마다 파일을 확인해 없거나 크기가 틀린 것만 받는다(이름이 곧 해시라
// 있으면 최신). 받는 동안 화면을 누르면 멈춘다. 끝나면 새로 받음·이미 있음·실패를 몇 초 띄운다.
static void drawPhotoSync(const char* title, uint16_t done, uint16_t total, const char* name) {
  clearContent();
  const int cy = contentMid();
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(title, tft.width() / 2, cy - 54);
  useFont(14);
  contentText(inkSub());
  tft.drawString(name && name[0] ? name : " ", tft.width() / 2, cy - 24);

  // 진행 막대
  const int bx = BORDER + 28, bw = tft.width() - bx * 2, by = cy - 4, bh = 12;
  const uint16_t line = lightBg() ? C_LINE : C_TABBG;
  tft.drawRect(bx, by, bw, bh, line);
  if (total) tft.fillRect(bx + 1, by + 1, (int)((bw - 2) * (uint32_t)done / total), bh - 2, inkEarn());

  contentText(inkMuted());
  tft.drawString("화면을 누르면 멈춥니다", tft.width() / 2, contentTop() + contentH() - 18);
  useFont(0);
  char n[16];
  snprintf(n, sizeof(n), "%u / %u", (unsigned)done, (unsigned)total);
  contentText(inkMain());
  tft.drawString(n, tft.width() / 2, cy + 32, 4);
}

static void photoSyncAll() {
  sndMode();
  overlayUntil = 0;
  resetStep();
  tab = TAB_MAIN;
  drawScreen();

  if (WiFi.status() != WL_CONNECTED) {
    sndFail();
    drawErrorScreen("와이파이에 연결되지 않았습니다");
    overlayUntil = millis() + OVERLAY_MS;
    return;
  }
  drawPhotoSync("명단 받는 중", 0, 0, "");
  if (!rosterFetch()) {
    sndFail();
    drawErrorScreen("명단을 받지 못했습니다");
    overlayUntil = millis() + OVERLAY_MS;
    return;
  }

  uint16_t total = 0;
  for (uint16_t i = 0; i < rosterCount; i++) if (roster[i].img[0]) total++;

  // 두 번째 터치의 손가락이 아직 화면에 있으면 곧바로 '멈춤' 으로 읽힌다 — 뗄 때까지(최대 1초) 기다린다
  uint16_t tx, ty;
  for (uint32_t t0 = millis(); tft.getTouch(&tx, &ty) && millis() - t0 < 1000;) delay(20);

  const size_t want = (size_t)PHOTO_PX * PHOTO_PX * 2;
  uint16_t done = 0, got = 0, have = 0, failed = 0;
  bool stopped = false;
  uint32_t lastDraw = 0;
  for (uint16_t i = 0; i < rosterCount; i++) {
    const RosterEntry& r = roster[i];
    if (!r.img[0]) continue;
    done++;

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, r.img);
    size_t size = 0;
    if (LittleFS.exists(path)) {
      fs::File f = LittleFS.open(path, "r");
      if (f) { size = f.size(); f.close(); }
    }
    if (size == want) {
      have++;
      if (millis() - lastDraw > 300) { drawPhotoSync("사진 확인 중", done, total, r.name); lastDraw = millis(); }
    } else {
      if (size) LittleFS.remove(path);       // 크기가 틀린 반쪽 파일 — 지우고 새로 받는다
      drawPhotoSync("사진 받는 중", done, total, r.name);
      lastDraw = millis();
      ArtFile a;
      a.base = 0;
      strlcpy(a.file, r.img, sizeof(a.file));
      a.w = PHOTO_PX; a.h = PHOTO_PX;
      if (artDownloadFrom("/photo/device/", a)) got++;
      else failed++;
    }
    if (tft.getTouch(&tx, &ty)) { stopped = true; break; }
    ledUpdate(millis());
  }
  if (!stopped) photoCursor = rosterCount;   // 다 훑었다 — 조용할 때 받기는 할 일이 없다
  artPrune();                                // 명단에서 빠진 아이의 옛 사진을 치운다(명단을 막 받았다)
  Serial.printf("[사진] 모두 받기 — 새로 %u · 있음 %u · 실패 %u%s\n",
                got, have, failed, stopped ? " (멈춤)" : "");

  // 결과
  clearContent();
  const int cy = contentMid();
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(stopped ? "사진 받기를 멈췄습니다" : "학생 사진을 받았습니다", tft.width() / 2, cy - 40);
  useFont(14);
  char l1[48], l2[48];
  snprintf(l1, sizeof(l1), "새로 받음 %u장 · 이미 있음 %u장", (unsigned)got, (unsigned)have);
  contentText(inkSub());
  tft.drawString(total ? l1 : "사진을 올린 학생이 없습니다", tft.width() / 2, cy - 6);
  if (failed) {
    snprintf(l2, sizeof(l2), "받지 못함 %u장 — 다시 두 번 눌러 보세요", (unsigned)failed);
    contentText(inkSpend());
    tft.drawString(l2, tft.width() / 2, cy + 20);
  }
  useFont(0);
  if (failed) sndFail(); else sndEarn();
  overlayUntil = millis() + 4000;
  lastActivity = millis();
}

static void onCancel() {
  sndMode();
  resetStep();
  drawScreen();
  lastActivity = millis();
}

// ── 태그가 들어왔을 때 ────────────────────────────────────────────
// 단계마다 받아야 할 것이 다르다. 대기·사람 단계에서는 키링을, 카드 단계에서는
// 카드를 기다린다. 엉뚱한 것을 대면 처리하지 않고 무엇을 대야 하는지 알려 준다.
static void handleTag(const uint8_t* uid, uint8_t len) {
  char s[24];
  uidToStr(uid, len, s, sizeof(s));

  // 같은 태그가 붙어 있는 동안 계속 처리되지 않게 잠시 막는다
  uint32_t now = millis();
  if (!strcmp(s, lastUid) && now - lastTagMs < cfg.tagCooldownMs) return;
  strncpy(lastUid, s, sizeof(lastUid) - 1);
  lastTagMs = now;

  // 내역 탭에서는 잔액을 건드리지 않고 보여주기만 한다
  if (tab == TAB_HISTORY) {
    drawWorking();
    if (whoFetch(s)) {
      // 내역 화면은 대기 단계 그대로라(step) 초록이 저절로 켜지지 않는다 — 조회 화면을 띄우는 동안만 켠다
      ledHold(LED_GREEN, LED_WHO_MS);
      sndMode();
      drawWhoScreen();
      overlayUntil = millis() + WHO_OVERLAY_MS;
    } else {
      sndFail();
      drawErrorScreen("잔액을 불러오지 못했습니다");
      overlayUntil = millis() + OVERLAY_MS;
    }
    lastActivity = millis();
    return;
  }

  const int8_t ci = cardIndexOf(s);

  // ── 카드를 기다리는 중 ──
  if (step == STEP_CARD) {
    // 같은 키링을 한 번 더 댄 것 — 이미 그 아이 화면이다. 예전에는 '등록되지 않은 카드입니다' 를 띄우고
    // 그 키링을 미등록 카드로 서버에 알렸다(서버가 걸러 기록은 남지 않지만, 아이 앞에서 오류가 뜨고 0.7초 멈췄다).
    if (ci < 0 && !strcmp(s, curUid)) return;
    // 다른 아이의 키링 — 앞사람이 취소하지 않고 간 경우다. 그 아이로 바꿔 아래 키링 흐름을 탄다
    if (ci < 0 && rosterHas(s)) {
      step = STEP_IDLE;
      curUid[0] = '\0'; curName[0] = '\0';
      curBalanceKnown = true;
    } else
    if (ci < 0) {                      // 카드가 아니거나 등록되지 않았다
      sndFail();
      drawErrorScreen("등록되지 않은 카드입니다");
      overlayUntil = millis() + OVERLAY_MS;
      reportSeen(s);
      lastActivity = millis();
      return;
    }
    // 사용에서 잔액을 넘으면 처리하지 않는다. 서버도 막지만(409), 여기서 먼저 걸러야
    // "확인 중" 을 거쳐 실패로 돌아오는 왕복이 없다 — 아이 앞에서 기다리는 시간이다.
    if (cards[ci].spend && curBalance - cards[ci].amount < 0) {
      sndFail();
      drawLowBalance(curName[0] ? curName : curUid, curBalance);
      overlayUntil = millis() + OVERLAY_MS;
      lastActivity = millis();
      return;                            // 카드 단계에 그대로 머문다 — 다른 카드를 대면 된다
    }
    curCard = ci;
    commitCard();                        // 확인 단계 없이 바로 처리하고 완료 화면으로
    return;
  }

  // ── 사람을 기다리는 중(대기·사람·확인·완료) ──
  // 카드를 먼저 대면 순서를 알려 준다 — 아무 반응이 없으면 고장으로 읽힌다.
  if (ci >= 0) {
    sndFail();
    drawErrorScreen("키링을 먼저 대주세요");
    overlayUntil = millis() + OVERLAY_MS;
    lastActivity = millis();
    return;
  }

  // 키링이다. 이름표(미리 받아 둔 명단)에 있으면 **서버를 기다리지 않고** 이름·사진부터 띄운다.
  // 예전에는 '확인 중' 을 띄운 채 서버 응답(잔액)이 와야 이름이 나왔다 — 태그하고 한참 뒤에 떴다.
  // 잔액은 곧이어 받아 그 자리만 고친다(drawWaitRefresh). 명단에 없는 키링은 누구인지 몰라
  // 예전처럼 서버에 먼저 묻는다. 출석모드는 곧바로 처리 화면으로 가므로 먼저 띄우지 않는다.
  const uint32_t tagT0 = millis();
  const bool early = !cfg.attendanceMode && rosterHas(s);
  if (early) {
    strlcpy(curUid, s, sizeof(curUid));
    strlcpy(curName, nameOf(s), sizeof(curName));
    curBalance = 0;
    curBalanceKnown = false;
    curCard = -1;
    step = STEP_CARD;
    stepAt = millis();
    overlayUntil = 0;
    drawWaitCard();
    Serial.printf("[시간] 키링 → 이름 %lums\n", (unsigned long)(millis() - tagT0));
    sndMode();                         // 확인음은 화면을 그린 뒤 — 소리가 끝날 때까지(45ms) 그리기를 붙잡지 않게
  } else {
    drawWorking();
  }

  // 누구인지·잔액을 서버에 묻는다 — 등록되지 않았으면 아무것도 만들지 않는다.
  const bool reused = apiHttp.connected();
  const bool got = whoFetch(s, false);
  Serial.printf("[시간] 키링 → 잔액 %lums (%s)\n", (unsigned long)(millis() - tagT0),
                reused ? "연결 이어 씀" : "새 연결");
  // 먼저 띄운 화면을 물릴 때 — resetStep 은 쿨다운까지 풀어, 키링을 올려 둔 채면 오류가 연달아 뜬다
  auto dropEarly = []() { step = STEP_IDLE; curUid[0] = '\0'; curName[0] = '\0'; curBalanceKnown = true; };
  if (!got) {
    if (early) dropEarly();
    sndFail();
    drawErrorScreen("잔액을 불러오지 못했습니다");
    overlayUntil = millis() + OVERLAY_MS;
    lastActivity = millis();
    return;
  }
  if (!whoKnown) {
    if (early) dropEarly();
    sndFail();
    clearContent();
    useFont(20);
    tft.setTextDatum(MC_DATUM);
    contentText(inkMain());
    tft.drawString("등록되지 않은 키링", tft.width() / 2, contentMid() - 16);
    useFont(14);
    contentText(inkMuted());
    tft.drawString("관리자에게 알려 주세요", tft.width() / 2, contentMid() + 14);
    useFont(0);
    overlayUntil = millis() + OVERLAY_MS;
    reportSeen(s);
    Serial.printf("[키링] %s — 미등록\n", s);
    lastActivity = millis();
    return;
  }

  if (early) {
    // 이미 이름을 띄워 두었다 — 잔액과 카드 목록만 고친다
    curBalance = whoBalance;
    curBalanceKnown = true;
    if (whoName[0]) strlcpy(curName, whoName, sizeof(curName));
    drawWaitRefresh();
    Serial.printf("[키링] %s(%s) 잔액 %ld\n", curUid, curName, (long)curBalance);
    lastActivity = millis();
    return;
  }

  sndMode();
  strlcpy(curUid, s, sizeof(curUid));
  strlcpy(curName, whoName, sizeof(curName));
  curBalance = whoBalance;
  curBalanceKnown = true;
  curCard = -1;
  step = STEP_CARD;
  stepAt = millis();
  overlayUntil = 0;
  Serial.printf("[키링] %s(%s) 잔액 %ld\n", curUid, curName, (long)curBalance);

  // 출석모드는 카드까지 건너뛴다 — 줄이 길 때 키링 한 번으로 끝내려는 것이다
  if (cfg.attendanceMode) {
    const int8_t i = attendanceCardIndex();
    if (i < 0) {
      sndFail();
      drawErrorScreen("등록된 카드가 없습니다");
      overlayUntil = millis() + OVERLAY_MS;
      lastActivity = millis();
      return;
    }
    curCard = i;
    commitCard();                      // 안에서 lastActivity 도 갱신한다
    return;
  }

  drawWaitCard();
  lastActivity = millis();
}

// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // USB 가 PC 에 꽂혀 있는데 아무도 로그를 읽지 않으면(모니터를 닫았다든지) ESP32 코어는 쓸 때마다
  // 최대 2초(100ms × 20번)를 기다린다. 태그 한 번에 로그가 여러 줄이라, 그 상태에서는 키링을 대고
  // 화면이 뜨기까지 2초가 넘게 걸렸다(실측 2129ms, 모니터를 열면 128ms). 로그는 버려도 되니 기다리지 않는다.
  // 충전기에만 꽂혀 있으면(호스트 없음) 원래 기다리지 않아 현장에서는 드러나지 않던 문제다.
  Serial.setTxTimeoutMs(0);
#endif
  delay(200);
  Serial.println("\n=== TalentNfcReader 시작 ===");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_TFT_BL, OUTPUT);
  analogWrite(PIN_TFT_BL, 0);
  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);   // 딥슬립 기상용(ext0). 평소에는 폴링만 쓴다.
  // 상태 LED 를 먼저 끈다 — WS2812 는 리셋돼도 전원이 붙어 있는 한 이전 색을 들고 있어,
  // 켜지는 동안 옛 빨강이 남아 오류로 읽힌다. 이후 색은 loop 의 ledUpdate 가 정한다.
  ledWrite(LED_OFF);

  // 서버에서 받은 포인트 그림을 담아 두는 곳. 못 열어도 계속 간다 —
  // 그때는 펌웨어에 구워 넣은 그림으로 돈다.
  if (!LittleFS.begin(true)) Serial.println("LittleFS 를 열지 못했습니다 — 받은 그림은 쓰지 못합니다.");

  // 캐시된 설정을 먼저 읽는다. 서버 왕복을 기다리지 않고 바로 화면을 그리기 위함이다.
  cfgLoadCache();

  tft.init();
  // 그림 데이터의 바이트 순서.
  //
  // pushImage 는 _swapBytes 가 false 면 uint16 배열의 메모리 바이트를 그대로 SPI 로 민다.
  // ESP32 는 리틀엔디안이라 0xF800(빨강)이 메모리에 00 F8 로 놓이는데, ILI9341 은 상위
  // 바이트를 먼저 받으므로 0x00F8 로 읽혀 색이 뒤집히고 화면이 지글거린다.
  //
  // 그래서 true 로 켜고, 그림은 어디서 오든 "정상 RGB565 값" 하나로 통일한다 —
  // 컴파일해 넣은 헤더(tools/make-artwork.py)도, 서버에서 받는 .565 파일
  // (yvServer/talent/talentArt.js 가 writeUInt16LE 로 쓴다)도 같은 규칙이다.
  // 한쪽만 바꾸면 다른 쪽이 깨지므로 셋을 함께 고칠 것.
  tft.setSwapBytes(true);
  tft.setRotation(cfg.rotation);   // 거치 방향 — 서버 설정(기본 0 = 240x320 세로)
  tft.setTouch((uint16_t*)TOUCH_CAL);
  initColors();
  tft.fillScreen(TFT_BLACK);

  // 깨어난 이유를 남겨 두면 현장 디버깅이 쉽다
  esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
  Serial.printf("기상 원인: %d %s\n", (int)why,
                why == ESP_SLEEP_WAKEUP_EXT0 ? "(화면 터치)" : "(전원/리셋)");

  sndPowerOn();
  backlight(true);

  // 잔액은 서버가 갖고 있으므로 깨어날 때마다 접속한다.
  // 접속에 몇 초가 걸릴 수 있어 화면에 알린다(멈춘 것처럼 보이지 않게).
  //
  // ── 켤 때 뜨는 화면 ──
  // 관리자가 올린 그림 한 장이 화면 전체를 덮는다. 테두리·헤더·탭 버튼은 그리지
  // 않는다 — 아직 아무것도 고를 수 없는 상태라 단추가 있으면 눌러도 되는 줄 안다.
  //
  // 이 그림은 서버에 붙기 **전에** 그려야 해서 파일 이름을 NVS 에 들고 있다가 쓴다.
  // 그래서 처음 켤 때(또는 그림을 바꾼 직후 한 번)는 아직 없고, 그때는 바탕색에
  // 글자 한 줄로 대신한다.
  bool drewSplash = false;
  if (splashHas && splashFile.w == tft.width() && splashFile.h == tft.height()) {
    drewSplash = drawArtFile(splashFile, 0, 0);
  }
  if (!drewSplash) {
    const uint16_t bg = hexToColor(cfg.bgColor);
    tft.fillScreen(bg);
    useFont(20);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(isLightColor(cfg.bgColor) ? C_INK : TFT_WHITE, bg);
    tft.drawString("연결 중", tft.width() / 2, tft.height() / 2);
    useFont(0);
  }

  bootShowing = true;
  bootStatus("와이파이 연결 중");

  wifiLoadSaved();
  ledUpdate(millis());                 // 연결 중 노랑을 곧바로 켠다(이어서 wifiTry 가 깜빡인다)
  if (!wifiConnectKnown(8000)) {
    // 알고 있는 것으로는 못 붙었다. 블루투스를 열고 휴대폰에서 넣어 줄 때까지 기다린다.
    Serial.println("WiFi 연결 실패 — 블루투스 설정 모드로 들어갑니다.");
    bootShowing = false;               // 설정 화면이 켤 때 화면을 덮는다 — 그 위에 진행 줄을 그리지 않는다
    provisionMode();
  }
  if (WiFi.status() == WL_CONNECTED) {
    afterOnline();
  } else {
    // 연결 못 해도 계속 진행한다. 태깅할 때 실패 사유를 화면에 보여준다.
    // 헤더의 '끊김' 을 누르면 설정 화면을 다시 열 수 있다.
    Serial.println("WiFi 연결 실패 — 태깅 시 서버 요청이 실패합니다.");
  }
  Serial.printf("[설정] 출처=%s step=%ld 슬립=%s\n",
                cfgFromServer ? "서버" : "캐시/기본값", (long)cfg.talentStep,
                cfg.sleepEnabled ? "켬" : "끔");

  if (bootDownloads) bootStatus("그림 %u장 받음 · 리더 준비 중", (unsigned)bootDownloads);
  else               bootStatus("리더 준비 중");
  Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL);
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  nfcReady = (ver != 0);
  if (nfcReady) {
    nfc.SAMConfig();
    Serial.printf("PN532 준비됨 (v%d.%d)\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
  } else {
    Serial.printf("PN532 를 찾지 못했습니다 — 배선(SDA=%d, SCL=%d)과 I2C 모드 스위치를 확인하세요.\n",
                  PIN_NFC_SDA, PIN_NFC_SCL);
    sndFail();
  }

  wifiWasOnline = WiFi.status() == WL_CONNECTED;
  bootShowing = false;
  useFont(0);                          // 진행 줄이 들고 있던 14px 을 내려놓는다 — 내장 폰트로 그리는 숫자가 있다
  drawScreen();
  lastActivity = millis();
}

void loop() {
  const uint32_t now = millis();

  // ── 상태 LED ── 오류 빨강 · 키링 초록 · 대기 파랑 깜빡임(색이 바뀔 때만 쓴다)
  ledUpdate(now);

  // ── 화면 터치: 탭 전환 ──
  // 정전식 패드 대신 디스플레이의 XPT2046 을 읽는다. getTouch() 는 눌린 동안
  // 계속 true 라, 디바운스로 한 번만 받는다.
  uint16_t tx, ty;
  const bool touching = tft.getTouch(&tx, &ty);

  // ── 헤더 띠: 한 번 = 내역, 두 번 = 학생 사진 모두 받기 ──
  // 두 번을 세려면 '누른 채' 가 아니라 **누르는 순간**(떼었다가 다시 누름)을 세야 한다 — 디바운스로는
  // 누른 채 있는 손가락도 몇 백 ms 마다 다시 눌림으로 들어온다. 첫 누름 뒤 HEAD_DOUBLE_MS 안에 한 번 더 누르면
  // 두 번, 그 시간이 지나도록 없으면 한 번으로 처리한다(그래서 내역은 그만큼 늦게 열린다).
  // 120ms 보다 짧게 이어진 누름은 한 번 누르는 사이 터치가 잠깐 끊긴 것으로 보고 세지 않는다.
  static bool touchWasDown = false;
  static uint32_t headTapMs = 0;
  const bool pressEdge = touching && !touchWasDown;
  touchWasDown = touching;
  if (pressEdge && histHit(tx, ty)) {
    lastTouchMs = now;
    lastActivity = now;
    if (headTapMs && now - headTapMs >= 120 && now - headTapMs <= HEAD_DOUBLE_MS) {
      headTapMs = 0;
      photoSyncAll();
      return;
    }
    if (!headTapMs || now - headTapMs >= 120) headTapMs = now;
  }
  if (headTapMs && now - headTapMs > HEAD_DOUBLE_MS) {
    headTapMs = 0;
    headerTap(now);
    return;
  }

  if (touching && !histHit(tx, ty) && now - lastTouchMs > cfg.touchDebounceMs) {
    lastTouchMs = now;

    // ── 아래쪽 취소 단추 ──
    // 진행 중일 때만 있다. 화면 폭을 다 쓰는 띠라 다른 것과 겹치지 않는다.
    if (tab != TAB_HISTORY && !overlayUntil && step == STEP_CARD && btnHit(tx, ty)) {
      onCancel();
      return;
    }
    // 헤더 띠는 위에서 따로 받는다(한 번·두 번 터치)

    // ── 내역 탭의 페이지 띠 ──
    // 위는 최근 쪽, 아래는 예전 쪽. 끝에 닿았으면 아무 일도 하지 않는다 —
    // 소리를 내면 "눌렀는데 안 된다" 가 아니라 "고장" 으로 들린다.
    if (tab == TAB_HISTORY && !overlayUntil) {
      const int8_t nav = histNavHit(tx, ty);
      if (nav >= 0) {
        if (!feedOk && nav == 1)      { sndMode(); historyLoad(feedPage); drawHistory(); }
        else if (nav == 0 && feedPage > 0) { sndMode(); historyLoad(feedPage - 1); drawHistory(); }
        else if (nav == 1 && feedMore)     { sndMode(); historyLoad(feedPage + 1); drawHistory(); }
        lastActivity = now;
        return;
      }
    }

    lastActivity = now;
  }

  // ── 결과·오류 화면을 물린다 ──
  if (overlayUntil && now > overlayUntil) {
    overlayUntil = 0;
    // 오류로 끊긴 자리는 대기로 되돌린다 — 앞사람 이름이 남은 채 다음 사람이
    // 카드를 대면 엉뚱한 사람에게 처리된다.
    if (step == STEP_DONE) resetStep();
    drawTabContent();
  }

  // ── 완료 화면을 물린다 ──
  if (step == STEP_DONE && now - stepAt > cfg.doneMs && !overlayUntil) {
    resetStep();
    drawScreen();
  }

  // ── 진행 중인 채로 잊혀진 것을 접는다 ──
  // 아이가 키링만 대고 가 버리면 그 이름이 화면에 남는다. 다음 사람이 카드를 대면
  // 앞사람에게 처리되므로, 한동안 아무 일도 없으면 스스로 대기로 돌아간다.
  if (step == STEP_CARD && !overlayUntil && now - stepAt > 60000) {
    Serial.println("[단계] 60초 무입력 — 대기로 되돌립니다");
    resetStep();
    drawScreen();
  }

  // ── 태깅 ──
  if (nfcReady) {
    // 버퍼가 255 바이트인 이유:
    // Adafruit_PN532::readDetectedPassiveTargetID 는 응답 프레임의 12번째 바이트를
    // UID 길이로 그대로 믿고 그만큼 이 버퍼에 복사한다 — 경계 검사가 없다.
    // I2C 프레임이 한 번 어긋나면 그 값이 200 이 될 수도 있어서, 7 바이트만 잡아 두면
    // 스택을 넘어 쓴다. 여기서 넉넉히 잡아 두면 그 피해가 이 버퍼 안에서 끝난다.
    uint8_t uid[255] = {0};
    uint8_t uidLen = 0;
    // 논블로킹에 가깝게 짧은 타임아웃으로 훑는다(터치 반응이 굼떠지지 않게)
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 80)) {
      // ISO14443A 의 UID 는 4·7·10 바이트뿐이다. 그 밖의 길이는 응답이 깨진 것이므로
      // 버린다 — 깨진 프레임을 그대로 태우면 없는 키링이 새로 만들어진다.
      if (uidLen == 4 || uidLen == 7 || uidLen == 10) {
        char dbg[24];
        uidToStr(uid, uidLen, dbg, sizeof(dbg));

        // ── 7바이트가 아니면 조용히 넘긴다 ──
        //
        // 우리 키링과 카드는 모두 7바이트(NTAG213)다. 4바이트로 답하는 것은
        // 휴대폰·교통카드·사원증이고, 특히 휴대폰은 지날 때마다 **다른** UID 를
        // 내놓는다. 그것마다 "아닙니다" 를 띄우면 사람이 지나가기만 해도 리더가
        // 깜빡이고 소리가 난다 — 화면도 소리도 로그도 없이 넘긴다.
        //
        // 이미 등록해 둔 카드는 길이와 상관없이 받는다. 4바이트 카드를 지급/사용
        // 카드로 쓰기로 했다면 그것까지 막을 이유는 없다.
        if (uidLen != 7 && cardIndexOf(dbg) < 0) {
          // 아무것도 하지 않는다. 로그도 남기지 않는다 — 루프마다 지나는 자리라
          // 한 줄만 찍어도 초당 열 몇 줄이 쌓인다.
        } else

        // ── 올려둔 카드는 한 번만 처리한다 ──
        //
        // 카드를 올려 두면 이 자리를 80ms 마다 지난다. 예전에는 쿨다운을 아래쪽
        // handleTag/rejectTag **안에서만** 봤다. 그래서 화면은 한 번만 바뀌는데
        // 그 위의 로그·CC 읽기·SAMConfig 는 초당 열 몇 번씩 계속 돌았다 —
        // 같은 줄이 로그에 수십 줄씩 쌓이던 것이 이것이다.
        //
        // 여기서 먼저 끊으면 NFC 교환 자체를 아끼고 로그도 한 번만 남는다.
        // (간격은 '같은 키링 재인식 간격' = tagCooldownMs. 터치 쪽 설정이 아니다)
        if (!strcmp(dbg, lastUid) && millis() - lastTagMs < cfg.tagCooldownMs) {
          // 아직 같은 카드다. 아무것도 하지 않는다.
        } else {
        // ── 키링만 받는다 ──
        //
        // 리더는 전파를 계속 내보내므로 5cm 안에 들어온 것은 무엇이든 응답한다.
        // 휴대폰은 지갑·교통카드 때문에 댈 때마다 **다른** 4바이트 UID 를 내놓고,
        // 사원증·교통카드는 자기 UID 로 답한다. 그대로 받으면 없는 사람이 새로
        // 만들어지고, 같은 키링을 대도 다른 이름으로 들어간 것처럼 보인다.
        //
        // 키링(NTAG213 계열)은 3번 페이지에 NDEF 용량 컨테이너가 있고 첫 바이트가
        // 0xE1 이다. 쓰는 스티커가 NXP 정품이 아니라 Feiju 제 호환 칩이어도 이 값은
        // 같다(실측 E1 10 12 00 = 144바이트, NTAG213 규격). MIFARE Classic 이나
        // 휴대폰은 이 읽기에 아예 응답하지 않으므로 여기서 갈린다.
        //
        // **읽은 직후에 물어야 한다.** 카드가 아직 선택된 상태라야 InDataExchange 가
        // 통한다. handleTag 안쪽처럼 뒤로 미루면 정품 키링도 실패로 나온다.
        uint8_t cc[4] = {0};
        bool ndef = nfc.ntag2xx_ReadPage(3, cc) && cc[0] == 0xE1;

        // 첫 읽기는 드물게 빗나간다 — 카드가 자리를 잡기 전에 잡히면 응답이 오지
        // 않고 CC 가 0 으로 남는다. 그때 바로 내치면 "아닙니다" 가 떴다가 다시
        // 대면 되는, 두 번 대야 하는 증상이 된다. 한 번만 다시 물어본다.
        if (!ndef && !cc[0] && !cc[1] && !cc[2] && !cc[3]) {
          nfc.SAMConfig();                         // 실패한 교환이 남긴 상태를 되돌린다
          uint8_t u2[255] = {0};
          uint8_t l2 = 0;
          if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, u2, &l2, 120)
              && l2 == uidLen && !memcmp(u2, uid, uidLen)) {   // 같은 카드일 때만
            memset(cc, 0, sizeof(cc));
            ndef = nfc.ntag2xx_ReadPage(3, cc) && cc[0] == 0xE1;
            if (ndef) Serial.println("[태그] 첫 읽기 실패 — 다시 물어 읽었습니다");
          }
        }

        // 지급/사용 카드는 NDEF 가 아니다(4바이트 UID 의 MIFARE 계열). 등록된
        // 카드라면 CC 와 상관없이 들여보낸다 — 이 문이 키링만 통과시키는 바람에
        // 카드 단계에서 카드를 대도 handleTag 까지 가지 못했다.
        const bool knownCard = cardIndexOf(dbg) >= 0;

        if (ndef || knownCard) {
          Serial.printf("[태그] %s (%u바이트) CC=%02X %02X %02X %02X%s\n",
                        dbg, uidLen, cc[0], cc[1], cc[2], cc[3],
                        ndef ? "" : " · 등록된 카드");
          // 모바일 조회 링크를 아직 안 썼으면 **여기서** 쓴다.
          //
          // 카드가 선택돼 있는 것이 확실한 유일한 자리다. handleTag 안쪽은 서버에
          // 묻느라 수백 ms 가 걸리고, 그동안 아이는 키링을 치우고 카드를 집는다.
          // 지급이든 사용이든 출석이든 내역이든 모든 길이 여기를 지나므로,
          // 모드와 상관없이 한 번만 써 두면 된다.
          //
          // 용량은 CC 세 번째 바이트 × 8 이다(NTAG213 은 0x12 → 144바이트).
          // 링크는 NDEF 키링에만 쓴다 — 지급/사용 카드에는 쓸 자리도, 쓸 이유도 없다.
          if (ndef) passEnsureWritten(dbg, (uint16_t)cc[2] * 8);
          handleTag(uid, uidLen);
        } else if (cc[0] || cc[1] || cc[2] || cc[3]) {
          // 응답은 왔는데 NDEF 가 아니다 = 다른 종류의 카드다(MIFARE Classic 등).
          // 지급/사용 카드로 쓰려고 댄 것일 수 있으니 등록할 수 있게 알린다.
          Serial.printf("[태그] %s — 처음 보는 카드 (CC %02X %02X %02X %02X)\n",
                        dbg, cc[0], cc[1], cc[2], cc[3]);
          nfc.SAMConfig();
          unknownCard(dbg);
        } else {
          // 아무 응답도 없다 — 휴대폰·교통카드처럼 우리와 상관없는 것.
          Serial.printf("[태그] %s — 응답 없음, 넘깁니다\n", dbg);
          // 실패한 교환은 대상 선택 상태를 망가뜨려 **다음 읽기까지 전부 실패**한다.
          nfc.SAMConfig();
          rejectTag(dbg);
        }
        }  // 쿨다운 else
      }
      // 길이가 4·7·10 이 아니면 응답 프레임이 깨진 것이다. 조용히 넘긴다 —
      // 깨진 프레임은 연달아 들어와 로그만 채운다.
    }
  }

  // ── 내역 탭의 "n분 전" 갱신 ──
  // 열어 둔 채 두면 시간이 멈춘 것처럼 보인다. 30초에 한 번 다시 그린다.
  static uint32_t lastAgeRedraw = 0;
  if (tab == TAB_HISTORY && !overlayUntil && now - lastAgeRedraw > 30000) {
    lastAgeRedraw = now;
    drawHistory();
  }

  // ── 끊겼으면 조용히 다시 붙어 본다 ──
  // 공유기가 잠깐 재부팅된 정도는 사람이 손대기 전에 스스로 돌아와야 한다.
  // 걸어만 두고 기다리지 않는다 — 여기서 멈추면 그동안 터치도 태깅도 굳는다.
  // (비밀번호가 아예 바뀐 경우는 이걸로 안 된다. 그때는 헤더의 '끊김' 을 눌러
  //  블루투스 설정 화면으로 간다)
  static uint32_t lastWifiRetry = 0;
  const bool online = WiFi.status() == WL_CONNECTED;
  if (!online && now - lastWifiRetry > 30000) {
    lastWifiRetry = now;
    WiFi.begin(wifiSsid[0] ? wifiSsid : WIFI_SSID,
               wifiSsid[0] ? wifiPass : WIFI_PASSWORD);
  }
  // 붙었다·끊겼다가 바뀌는 순간에만 손을 댄다.
  if (online != wifiWasOnline) {
    wifiWasOnline = online;
    Serial.printf("[와이파이] %s\n", online ? "다시 붙었습니다" : "끊겼습니다");
    drawHeader();
    if (online) {
      afterOnline();                     // 그동안 바뀐 설정·이름표·카드·그림을 받는다
      if (!overlayUntil) drawScreen();
    }
  }

  // ── 설정 주기 갱신 ──
  // 행사 중에 관리자가 값을 바꾸면 기기를 만지지 않고도 반영되게 한다.
  // 한 번에 1초 남짓 멈추므로 주기는 넉넉히(기본 5분) 잡는다.
  // rotation 은 화면을 다시 그려야 해서 여기서는 반영하지 않는다 — 다음 부팅에 적용된다.
  if (WiFi.status() == WL_CONNECTED && now - lastCfgFetch > cfg.ttlSec * 1000UL) {
    bool got = cfgFetch();
    lastCfgFetch = millis();             // 실패해도 매 루프 재시도하지 않게
    // 제목·배경·카드가 바뀌었을 수 있으므로 받은 김에 함께 다시 그린다
    if (got) {
      rosterFetch();                     // 이름이 새로 붙었을 수 있다
      cardsFetch();                      // 카드가 늘거나 금액이 바뀌었을 수 있다
      artSync();                         // 그림이 바뀌었으면 이름이 달라져 다시 받는다
      if (!overlayUntil) drawScreen();
    }
  }

  // ── 아이 사진 미리 받기 (조용할 때 한 장씩) ──
  photoSyncStep(now);

  // ── 서버 연결 데우기 ── 대기 화면일 때만(키링·카드 처리 중이거나 결과가 떠 있을 때는 건드리지 않는다)
  if (tab == TAB_MAIN && step == STEP_IDLE && !overlayUntil) apiKeepWarm(now);

  // ── 무입력이면 잠든다 (서버에서 켠 기기만) ──
  if (cfg.sleepEnabled && now - lastActivity > cfg.sleepTimeoutMs) goToDeepSleep();

  delay(10);
}
