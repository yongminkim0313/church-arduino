// TalentNfcReader — ESP32-S3 NFC 키링 리더 (펀펀포인트)
//
// NTAG-213 키링을 PN532(I2C)로 읽어 2.8" ILI9341 TFT 에 잔액을 보여준다.
// 입력은 디스플레이에 붙은 XPT2046 터치 패널 하나뿐이다 — 화면 위쪽의
// 지급·사용·내역 세 탭을 손으로 눌러 고른다.
//
// 지급·사용 탭은 화면 오른쪽에 '메뉴'(항목별 포인트)를 띄운다 — 지급은 출석 5,
// 새친구 20 처럼, 사용는 컵라면 5, 젤리 10 처럼. 눌러서 고른 항목의 포인트로 처리한다.
// 메뉴는 서버 설정(earnMenu/spendMenu)에서 오고, 비워 두면 talentStep 하나로 동작한다.
//
// 화면 테두리가 지금 태깅하면 무슨 일이 일어나는지 말해 준다:
//   초록 = 지급 · 빨강 = 사용 · 회색 = 내역 탭(태깅해도 처리하지 않는다)
// 부호(+/-)는 쓰지 않는다. 멀리서 보면 획 하나는 안 보이지만 테두리 색은 보인다.
//
// 포인트 잔액은 youthvision.co.kr 의 yvServer(/api/talent)가 관리한다.
// (같은 API 가 jesusdream.kr 에도 있다. 접속 주소는 ChurchSecrets.h 의 TALENT_API_BASE 하나로 바꾼다)
// 리더는 저장하지 않고 매번 서버에 묻는다 — 리더가 여러 대여도 잔액이 하나로 유지된다.
//
// ── 조작 ──────────────────────────────────────────────────────────
//   화면 위쪽 탭 터치 : 지급 / 사용 / 내역 전환
//   오른쪽 메뉴 터치  : 지급·사용할 항목을 고른다 (고른 것만 색이 채워진다)
//   키링 태깅         : 고른 항목대로 처리하고 이름과 함께 결과를 표시
//                       (예: "김용민 / 아이스크림 15 포인트 사용 / 남은 포인트 11")
//   내역 탭에서 태깅  : 잔액을 건드리지 않고 그 사람의 남은 포인트와 최근 4건을 보여준다
//   내역 탭 위·아래 띠: 위 = 이전 페이지(더 최근), 아래 = 다음 페이지(더 예전)
//   무입력            : sleepEnabled 가 켜진 기기만 딥슬립한다. 기본은 꺼짐 —
//                       상시 전원으로 세워 두는 기기가 대부분이고, 화면이 꺼지면
//                       고장으로 오해받는다.
//
// ── 빌드 (TFT_eSPI 설정을 이 스케치에만 적용) ──────────────────────
// TFT_eSPI 는 라이브러리 전역 설정(User_Setup_Select.h)을 쓰기 때문에, 그대로 두면
// 같은 PC 의 TTGO T-Display 스케치들과 설정이 충돌한다. 그래서 라이브러리를 건드리지
// 않고 빌드 플래그로 이 스케치에만 설정을 주입한다. build.sh 를 쓰면 된다.
//
//   ./TalentNfcReader/build.sh              # 컴파일
//   ./TalentNfcReader/build.sh --upload     # 컴파일 + 업로드
//
// 필요 라이브러리: TFT_eSPI, Adafruit PN532 (+ Adafruit BusIO)
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
enum ModeLock : uint8_t { LOCK_BOTH = 0, LOCK_EARN = 1, LOCK_SPEND = 2 };

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
  int32_t  talentStep      = DEF_TALENT_STEP;       // 메뉴가 빈 탭에서만 쓴다
  bool     defaultEarn     = true;                  // defaultMode
  uint8_t  modeLock        = LOCK_BOTH;
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
//   대기 ── 키링 ──▶ 사람(이름·잔액·최근내역) ── 확인 ──▶ 카드 대기 ── 카드 ──▶ 완료(그림)
//                                                                            │
//                                                            doneMs 뒤 대기 ◀┘
//
// 카드를 대면 곧바로 처리한다. 한 번 더 확인을 받지 않는 이유: 확인할 것(누구에게)은
// 이미 앞 단계에서 보고 눌렀고, 무엇을 얼마나는 카드에 적혀 있어 고를 여지가 없다.
// 줄을 세워 놓고 쓰는 기기라 누르는 횟수가 하나 줄면 체감이 크다.
//
// 출석모드면 '사람' 에서 확인을 누르는 순간 카드 단계도 건너뛴다(설정으로 정한 카드).
enum Step : uint8_t {
  STEP_IDLE,      // 키링을 기다린다
  STEP_PERSON,    // 누구인지 보여주고 확인을 기다린다
  STEP_CARD,      // 지급/사용 카드를 기다린다
  STEP_DONE,      // 완료 그림
};

// 지금 다루고 있는 사람과 카드. 단계가 대기로 돌아갈 때 함께 비운다.
static Step     step         = STEP_IDLE;
static uint32_t stepAt       = 0;    // 이 단계에 들어온 시각(자동 넘김·시간초과에 쓴다)
static char     curUid[24]   = "";
static char     curName[24]  = "";
static int32_t  curBalance   = 0;
static int8_t   curCard      = -1;   // cards[] 의 자리. -1 이면 아직 안 골랐다
static int32_t  doneDelta    = 0;    // 완료 화면에 띄울 값
static int32_t  doneBalance  = 0;

// ── 상태 ──────────────────────────────────────────────────────────
// 탭이 곧 모드다(enum Tab 은 TalentTypes.h — 자동 프로토타입보다 먼저 보여야 한다).
static Tab      tab          = TAB_EARN;
// 내역에 들어가기 전에 보던 탭. 헤더를 다시 누르면 여기로 돌아온다.
static Tab      prevTab      = TAB_EARN;
// 고른 항목은 탭마다 따로 기억한다 — 지급에서 '전도' 를 골라 뒀는데 사용 탭에
// 다녀오면 풀리는 것은 현장에서 짜증나는 동작이다.
static uint8_t  selEarn      = 0;
static uint8_t  selSpend     = 0;
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

// 탭 버튼 그림. 관리자가 올렸으면 구워 넣은 것 대신 이쪽을 쓴다(0=지급, 1=사용).
// 한 쪽은 고른 상태와 아닌 상태가 둘 다 있어야 쓴다 — 하나만 있으면 크기가 어긋나
// 두 칸의 배치가 깨진다. 서버가 그 규칙으로 걸러 보낸다.
static ArtFile tabOnFile[2], tabOffFile[2];
static bool    tabHasFile[2] = { false, false };

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

// ── 이름표 ────────────────────────────────────────────────────────
// 부팅할 때 GET /api/talent/roster 로 받아 둔다. 태그를 대는 순간 이름을
// 띄우려면 서버 응답을 기다릴 수 없어서다(왕복이 1초 가까이 걸린다).
#define ROSTER_MAX 200
struct RosterEntry { char uid[16]; char name[24]; };
static RosterEntry roster[ROSTER_MAX];
static uint16_t    rosterCount = 0;

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
  if (cfg.modeLock > LOCK_SPEND) cfg.modeLock = LOCK_BOTH;
  if (!cfg.label[0]) strlcpy(cfg.label, DEF_LABEL, sizeof(cfg.label));
  // "#RRGGBB" 가 아니면 기본색으로 돌린다. 잘못된 값이 오면 화면이 검게 칠해져
  // 고장으로 보이는데, 현장에서 그 원인을 짚기 어렵다.
  if (strlen(cfg.bgColor) != 7 || cfg.bgColor[0] != '#')
    strlcpy(cfg.bgColor, DEF_BG_COLOR, sizeof(cfg.bgColor));
}

// 지금 탭의 메뉴와 고른 자리. 내역 탭에서는 메뉴를 쓰지 않는다.
static Menu&    curMenu() { return (tab == TAB_SPEND) ? cfg.spendMenu : cfg.earnMenu; }
static uint8_t& curSel()  { return (tab == TAB_SPEND) ? selSpend : selEarn; }
static bool     hasMenu() { return tab != TAB_HISTORY && curMenu().count > 0; }

// 태깅했을 때 오르내릴 양. 메뉴가 비어 있으면 예전처럼 talentStep 하나를 쓴다.
static int32_t curAmount() {
  Menu& m = curMenu();
  if (!m.count) return cfg.talentStep;
  return m.item[curSel() < m.count ? curSel() : 0].amount;
}
// 내역에 남길 항목 이름. 메뉴를 안 쓰는 기기는 빈 문자열이다.
static const char* curItemName() {
  Menu& m = curMenu();
  if (!m.count) return "";
  return m.item[curSel() < m.count ? curSel() : 0].name;
}

// 메뉴가 줄어 고른 자리가 사라졌으면 첫 항목으로 되돌린다.
static void clampSel() {
  if (selEarn  >= cfg.earnMenu.count)  selEarn  = 0;
  if (selSpend >= cfg.spendMenu.count) selSpend = 0;
}

// 잠긴 기기에서 숨겨진 탭에 머물러 있지 않게 한다.
static bool tabAllowed(Tab t) {
  if (t == TAB_HISTORY) return true;
  if (cfg.modeLock == LOCK_EARN)  return t == TAB_EARN;
  if (cfg.modeLock == LOCK_SPEND) return t == TAB_SPEND;
  return true;
}

// 잠금이 걸려 있으면 그 탭으로, 아니면 기본 모드의 탭으로 맞춘다.
// 내역 탭에 머물러 있었다면 그대로 둔다 — 잠금과 상관없이 볼 수 있다.
static void cfgApplyMode() {
  if      (cfg.modeLock == LOCK_EARN)  tab = TAB_EARN;
  else if (cfg.modeLock == LOCK_SPEND) tab = TAB_SPEND;
  else if (tab != TAB_HISTORY)         tab = cfg.defaultEarn ? TAB_EARN : TAB_SPEND;
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
  cfg.defaultEarn     = prefs.getBool("dmode",  cfg.defaultEarn);
  cfg.modeLock        = prefs.getUChar("lock",  cfg.modeLock);
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
  clampSel();
}

static void cfgSaveCache() {
  if (!prefs.begin("talentcfg", false)) { Serial.println("[설정] NVS 열기 실패"); return; }
  prefs.putInt  ("step",    cfg.talentStep);
  prefs.putBool ("dmode",   cfg.defaultEarn);
  prefs.putUChar("lock",    cfg.modeLock);
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

// ── 서버에서 설정 받기 ──
// GET /api/talent/config?device=<기기ID>   헤더: x-talent-key
// 실패하면 아무것도 바꾸지 않는다. 캐시(또는 기본값)가 그대로 유지된다.
static bool cfgFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();                 // 자체 서버라 인증서 검증 생략(다른 보드들과 동일)
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[192];
  snprintf(url, sizeof(url), "%s/config?device=%s", TALENT_API_BASE, TALENT_DEVICE_ID);
  if (!http.begin(client, url)) return false;
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);

  int code = http.GET();
  String payload = (code > 0) ? http.getString() : String();
  http.end();

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

  const char* dm = c["defaultMode"] | "";
  if      (!strcmp(dm, "earn"))  cfg.defaultEarn = true;
  else if (!strcmp(dm, "spend")) cfg.defaultEarn = false;

  const char* ml = c["modeLock"] | "";
  if      (!strcmp(ml, "both"))  cfg.modeLock = LOCK_BOTH;
  else if (!strcmp(ml, "earn"))  cfg.modeLock = LOCK_EARN;
  else if (!strcmp(ml, "spend")) cfg.modeLock = LOCK_SPEND;

  const char* lb = c["deviceLabel"] | "";
  if (lb[0]) strlcpy(cfg.label, lb, sizeof(cfg.label));

  const char* bc = c["bgColor"] | "";
  if (bc[0]) strlcpy(cfg.bgColor, bc, sizeof(cfg.bgColor));

  // 메뉴는 키가 있을 때만 갈아 끼운다. 없으면 캐시에 있던 것을 그대로 둔다 —
  // 옛 서버에 붙었다고 메뉴가 사라지면 현장에서 기기가 못 쓰게 된다.
  readMenu(c, "earnMenu",  cfg.earnMenu);
  readMenu(c, "spendMenu", cfg.spendMenu);
  clampSel();

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

  // 탭 버튼 그림. 키가 없으면 건드리지 않는다(옛 서버에 붙어도 있던 것을 그대로 쓴다).
  if (doc["tabs"].is<JsonArray>()) {
    tabHasFile[0] = tabHasFile[1] = false;
    for (JsonObject t : doc["tabs"].as<JsonArray>()) {
      const char* side = t["side"] | "";
      const int idx = !strcmp(side, "earn") ? 0 : (!strcmp(side, "spend") ? 1 : -1);
      if (idx < 0) continue;
      const char* on  = t["file"]    | "";
      const char* off = t["offFile"] | "";
      const int16_t w = t["w"] | 0, hgt = t["h"] | 0;
      if (!on[0] || !off[0] || w < 1 || hgt < 1) continue;
      if (strlen(on) >= sizeof(tabOnFile[0].file) || strlen(off) >= sizeof(tabOffFile[0].file)) continue;
      tabOnFile[idx].base = idx;
      strlcpy(tabOnFile[idx].file, on, sizeof(tabOnFile[0].file));
      tabOnFile[idx].w = w; tabOnFile[idx].h = hgt;
      tabOffFile[idx].base = idx;
      strlcpy(tabOffFile[idx].file, off, sizeof(tabOffFile[0].file));
      tabOffFile[idx].w = t["offW"] | w;
      tabOffFile[idx].h = t["offH"] | hgt;
      tabHasFile[idx] = true;
    }
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

  Serial.printf("[설정] 서버 반영: 메뉴 지급%u/사용%u step=%ld 슬립=%s(%lums) 잠금=%u 밝기=%u 제목=%s\n",
                cfg.earnMenu.count, cfg.spendMenu.count,
                (long)cfg.talentStep, cfg.sleepEnabled ? "켬" : "끔",
                (unsigned long)cfg.sleepTimeoutMs,
                cfg.modeLock, cfg.backlight, cfg.label);
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

static void sndPowerOn()  { beep(BUZ_1, 80);  beep(BUZ_2, 80);  beep(BUZ_3, 120); } // 올라가며 켜짐
static void sndPowerOff() { beep(BUZ_3, 80);  beep(BUZ_2, 80);  beep(BUZ_1, 140); } // 내려가며 꺼짐
static void sndEarn()     { beep(BUZ_2, 70);  beep(BUZ_3, 130); }                   // 짧게 오르는 두 음
static void sndSpend()    { beep(BUZ_3, 70);  beep(BUZ_1, 130); }                   // 짧게 내리는 두 음
static void sndFail()     { beep(BUZ_LOW, 120); beep(BUZ_LOW, 220); }               // 낮은 두 번 — 뭔가 잘못됐다
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

  WiFiClientSecure client;
  client.setInsecure();          // 자체 서버라 인증서 검증은 생략(센서 보드들과 동일)
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[128];
  snprintf(url, sizeof(url), "%s/%s", TALENT_API_BASE, path);
  if (!http.begin(client, url)) {
    snprintf(r.reason, sizeof(r.reason), "%s", "연결 실패");
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);   // 서버의 TALENT_DEVICE_KEY 와 대조

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

  int code = http.POST(body);
  String payload = (code > 0) ? http.getString() : String();
  http.end();

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
static bool artDownload(const ArtFile& a) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[192];
  snprintf(url, sizeof(url), "%s/art/file/%s", TALENT_API_BASE, a.file);
  if (!http.begin(client, url)) return false;
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);

  const int code = http.GET();
  if (code != 200) {
    Serial.printf("[그림] %s 응답 %d\n", a.file, code);
    http.end();
    return false;
  }

  // 크기가 맞지 않으면 받지 않는다. 화면에 밀어 넣을 때 길이를 믿기 때문이다.
  const int want = (int)a.w * a.h * 2;
  if (http.getSize() != want) {
    Serial.printf("[그림] %s 크기 불일치 %d != %d\n", a.file, http.getSize(), want);
    http.end();
    return false;
  }

  char path[64];
  snprintf(path, sizeof(path), "%s/%s", ART_DIR, a.file);
  fs::File f = LittleFS.open(path, "w");
  if (!f) { http.end(); return false; }

  const int wrote = http.writeToStream(&f);
  f.close();
  http.end();

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

// 파일 이름이 지금 목록(배경 + 완료 그림 + 카드 그림)에 있는지.
// 여기서 빠뜨리면 artPrune 이 방금 받은 그림을 지워 버린다.
static bool artWanted(const char* name) {
  for (uint8_t i = 0; i < 2; i++)
    if (bgHas[i] && !strcmp(name, bgFile[i].file)) return true;
  for (uint8_t i = 0; i < 2; i++)
    if (doneHas[i] && !strcmp(name, doneFile[i].file)) return true;
  for (uint8_t i = 0; i < 2; i++)
    if (tabHasFile[i] && (!strcmp(name, tabOnFile[i].file) || !strcmp(name, tabOffFile[i].file)))
      return true;
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

  // 탭 버튼 그림. 두 장 중 하나라도 못 받으면 그 쪽은 구워 넣은 그림으로 되돌린다 —
  // 한 장만 받아 쓰면 고를 때마다 크기가 달라져 배치가 흔들린다.
  for (uint8_t i = 0; i < 2; i++) {
    if (!tabHasFile[i]) continue;
    want += 2;
    char p1[64], p2[64];
    snprintf(p1, sizeof(p1), "%s/%s", ART_DIR, tabOnFile[i].file);
    snprintf(p2, sizeof(p2), "%s/%s", ART_DIR, tabOffFile[i].file);
    const bool a1 = LittleFS.exists(p1) || artDownload(tabOnFile[i]);
    const bool a2 = LittleFS.exists(p2) || artDownload(tabOffFile[i]);
    if (a1) got++;
    if (a2) got++;
    if (!a1 || !a2) tabHasFile[i] = false;
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

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[192];
  snprintf(url, sizeof(url), "%s%s", TALENT_API_BASE, pathAndQuery);
  if (!http.begin(client, url)) return false;
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[%s] 서버 응답 %d\n", pathAndQuery, code);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), filter);
  http.end();
  if (err) { Serial.printf("[%s] 해석 실패 %s\n", pathAndQuery, err.c_str()); return false; }
  return doc["success"].as<bool>();
}

// GET /api/talent/roster — UID → 이름
static bool rosterFetch() {
  JsonDocument filter;
  filter["success"] = true;
  filter["data"][0]["uid"]  = true;
  filter["data"][0]["name"] = true;

  JsonDocument doc;
  if (!getJson("/roster", doc, DeserializationOption::Filter(filter))) return false;

  rosterCount = 0;
  for (JsonObject r : doc["data"].as<JsonArray>()) {
    if (rosterCount >= ROSTER_MAX) break;
    const char* u = r["uid"]  | "";
    const char* n = r["name"] | "";
    if (!u[0] || !n[0]) continue;
    strlcpy(roster[rosterCount].uid,  u, sizeof(roster[0].uid));
    strlcpy(roster[rosterCount].name, n, sizeof(roster[0].name));
    rosterCount++;
  }
  Serial.printf("[이름표] %u명 받음\n", rosterCount);
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

// 지금 이 기기가 받을 수 있는 카드인지. 지급 리더에 간식 카드를 대는 사고를 막는다.
static bool cardFitsTab(const CardEntry& c) {
  return c.spend ? (tab == TAB_SPEND) : (tab == TAB_EARN);
}

// POST /api/talent/seen — 처음 보는 카드를 서버에 알린다.
// 예전에는 모르는 UID 에 곧바로 잔액을 만들었다. 그러면 휴대폰이 스쳐도 "이름 없는
// 사람" 이 생겼다. 이제는 알리기만 하고, 관리자가 화면에서 무엇인지 정해 준다.
static void reportSeen(const char* uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(cfg.httpTimeoutMs);

  char url[160];
  snprintf(url, sizeof(url), "%s/seen", TALENT_API_BASE);
  if (!http.begin(client, url)) return;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-talent-key", TALENT_DEVICE_KEY);

  char body[128];
  snprintf(body, sizeof(body), "{\"uid\":\"%s\",\"device\":\"%s\"}", uid, TALENT_DEVICE_ID);
  const int code = http.POST(body);
  http.end();
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
static bool whoFetch(const char* uid) {
  JsonDocument filter;
  filter["success"] = true;
  filter["who"]["name"]    = true;
  filter["who"]["balance"] = true;
  filter["who"]["known"]   = true;
  filter["data"][0]["delta"]  = true;
  filter["data"][0]["agoSec"] = true;

  char q[64];
  snprintf(q, sizeof(q), "/feed?limit=%u&uid=%s", (unsigned)WHO_MAX, uid);

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
// 테두리 · 헤더 · 탭 · 내용 네 층으로 나눈다. rotation 에 따라 폭이 바뀌므로
// 가로 좌표는 tft.width() 에서 매번 계산한다.
#define BORDER   4
#define HEAD_H  42
// 탭 줄 높이. 그림 버튼(72px) + 위아래 여백.
// 예전에는 44px 알약만 잘라 썼는데, 손가락이 닿는 자리가 알약 모양뿐이라 가장자리를
// 누르면 안 먹었다. 이제 그림에 바깥 배경까지 넣어 잘라서(tools/make-artwork.py)
// 누를 수 있는 자리가 그림 사각형 전체가 된다 — tabHit 이 그림 사각형을 그대로 쓴다.
#define TAB_H   78
// 오른쪽 메뉴 칸. 14px 한글 5자(70px) + 안쪽 여백 12 + 세 자리 금액(24px) = 106 이라
// 108 로 잡았다. 서버가 이름을 5자, 금액을 999 로 조이는 근거가 이 계산이다.
// 남는 왼쪽 칸 120px 에는 "키링을 대주세요"(98px)와 48px 숫자 세 자리(81px)가 들어간다.
#define MENU_W  108
#define MENU_ROW 28
static int tabTop()     { return BORDER + HEAD_H; }
// 내역은 탭 줄 없이 전체를 쓴다 — 목록은 한 줄이라도 더 들어가는 편이 낫고,
// 거기서는 지급·사용을 고를 일이 없어 버튼이 자리만 차지한다.
static int contentTop() { return BORDER + HEAD_H + (tab == TAB_HISTORY ? 0 : TAB_H); }
static int contentH()   { return tft.height() - contentTop() - BORDER; }
static int contentMid() { return contentTop() + contentH() / 2; }

// ── 색 ────────────────────────────────────────────────────────────
// tft.color565 는 init() 뒤에야 쓸 수 있어 setup() 에서 채운다.
static uint16_t C_EARN, C_SPEND, C_NEUTRAL, C_HEADTXT, C_TABBG, C_ROWALT, C_WIFI, C_STRIP;
// 흰 바탕에서 쓰는 벌. 밝은 초록·연회색은 흰 종이 위에서 흐려 보여 따로 둔다.
static uint16_t C_EARN_D, C_SPEND_D, C_INK, C_INK2, C_INK3, C_LINE;
// 내역 탭 페이지 띠에서 '지금은 눌러도 소용없다' 를 말하는 회색
static uint16_t C_NAVOFF;

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
  C_INK2    = tft.color565( 74,  80,  90);   // 보조
  C_INK3    = tft.color565(132, 138, 148);   // 희미하게
  C_LINE    = tft.color565(214, 218, 224);   // 가름선
  C_NAVOFF  = tft.color565( 92,  96, 106);   // 눌러도 소용없는 페이지 띠
}

// 지금 태깅하면 무슨 일이 일어나는지를 한 가지 색으로 말한다.
static uint16_t tabColor(Tab t) {
  if (t == TAB_EARN)  return C_EARN;
  if (t == TAB_SPEND) return C_SPEND;
  return C_NEUTRAL;
}

// ── 테두리 ────────────────────────────────────────────────────────
// 부호(+/-)를 쓰지 않는 대신 이 테두리가 방향을 알린다. 획 하나보다
// 멀리서 잘 보이고, 화면 어디를 보고 있든 눈에 들어온다.
static void drawFrame() {
  const uint16_t c = tabColor(tab);
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

// ── 탭 ────────────────────────────────────────────────────────────
// 지급·사용 두 칸. 그림 버튼이라 글자를 따로 그리지 않는다(그림에 들어 있다).
// 고른 쪽은 색이 있는 그림, 고르지 않은 쪽은 회색 그림 — 두 벌을 따로 굽는다.
//
// 내역은 여기 없다 — 헤더로 옮겼다(drawHeader). 셋을 나란히 두면 한 칸이
// 78px 밖에 안 되어 그림이 뭉개진다.
//
// 잠긴 기기(지급 전용/사용 전용)는 한 칸만 가운데에 둔다.
// (struct TabBtn 는 TalentTypes.h)
// 한 칸을 채운다. 서버에서 받은 그림이 있으면 그것을, 없으면 구워 넣은 것을 쓴다.
// 탭 버튼 그림은 펌웨어에 구워 넣지 않는다(약 45KB 를 아꼈다). 서버가 늘 내려준다 —
// 관리자가 올린 것이 없으면 서버가 들고 있는 기본 그림을 대신 준다(talentArt 의
// default-art). 그래서 여기서는 파일만 본다.
//
// 그래도 받기 전(첫 부팅, 또는 통신 실패)에는 그림이 없다. 그때는 색 칠한 칸에
// 글자로 대신한다 — 빈 줄을 두면 고장으로 보인다.
#define TAB_FALLBACK_W 105
#define TAB_FALLBACK_H  72

static void tabBtnFill(TabBtn& b, Tab key, uint8_t idx) {
  b.key = key;
  b.img = nullptr; b.off = nullptr;
  b.file[0] = '\0'; b.fileOff[0] = '\0';
  if (tabHasFile[idx]) {
    strlcpy(b.file,    tabOnFile[idx].file,  sizeof(b.file));
    strlcpy(b.fileOff, tabOffFile[idx].file, sizeof(b.fileOff));
    b.w = tabOnFile[idx].w;
    b.h = tabOnFile[idx].h;
  } else {
    b.w = TAB_FALLBACK_W; b.h = TAB_FALLBACK_H;
  }
}

static uint8_t tabBtns(TabBtn* out) {
  uint8_t n = 0;
  if (tabAllowed(TAB_EARN))  tabBtnFill(out[n++], TAB_EARN,  0);
  if (tabAllowed(TAB_SPEND)) tabBtnFill(out[n++], TAB_SPEND, 1);
  return n;
}

// 버튼들을 가로 가운데에 나란히 놓는다. i 번째의 왼쪽 x.
// 틈은 0 이다 — 두 그림이 원본 한 장을 반으로 가른 것이라, 붙여 놓아야 바깥 테두리가
// 이어져 한 덩어리로 보인다. 띄우면 가운데가 갈라져 보인다.
static int tabBtnX(const TabBtn* b, uint8_t n, uint8_t i) {
  const int gap = 0;
  int total = gap * (n - 1);
  for (uint8_t k = 0; k < n; k++) total += b[k].w;
  int x = (tft.width() - total) / 2;
  for (uint8_t k = 0; k < i; k++) x += b[k].w + gap;
  return x;
}

static void drawTabs() {
  TabBtn b[2];
  const uint8_t n = tabBtns(b);
  // 탭 줄의 빈자리(버튼 둘레)는 내용 영역과 같은 바탕색으로 채운다 —
  // 흰 띠로 두면 버튼 위아래에 흰 줄이 남아 화면이 세 토막으로 끊겨 보인다.
  tft.fillRect(BORDER, tabTop(), tft.width() - BORDER * 2, TAB_H, tabBg(tab));

  // 고르지 않은 쪽은 회색 그림을 따로 쓴다. 계산으로 어둡게 만드는 것보다
  // 그림쟁이가 그린 것이 낫고, 줄 단위로 다시 칠하지 않아 그리기도 빠르다.
  for (uint8_t i = 0; i < n; i++) {
    const int x = tabBtnX(b, n, i);
    const int y = tabTop() + (TAB_H - b[i].h) / 2;
    const bool on = (b[i].key == tab);

    if (b[i].file[0]) {                  // 서버에서 받은 그림
      ArtFile a;
      a.base = 0;
      strlcpy(a.file, on ? b[i].file : b[i].fileOff, sizeof(a.file));
      a.w = b[i].w; a.h = b[i].h;
      if (drawArtFile(a, x, y, true)) continue;   // 둥근 모서리 바깥은 비침
      // 파일을 읽다 실패하면 아래 글자 칸으로 내려간다
    }

    // 아직 그림을 못 받았을 때. 고른 쪽만 방향색으로 채우고 나머지는 눌러 둔다.
    const uint16_t fill = on ? inkDir(b[i].key == TAB_EARN) : bgShade(12);
    tft.fillRoundRect(x, y, b[i].w, b[i].h, 12, fill);
    useFont(20);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? TFT_WHITE : (lightBg() ? C_INK2 : TFT_LIGHTGREY), fill);
    tft.drawString(b[i].key == TAB_EARN ? "지급" : "사용",
                   x + b[i].w / 2, y + b[i].h / 2);
    useFont(0);
  }
}

// 터치 좌표가 어느 탭 버튼인지. 버튼이 아니면 false.
static bool tabHit(uint16_t tx, uint16_t ty, Tab* out) {
  if (tab == TAB_HISTORY) return false;              // 내역에는 탭 버튼이 없다
  if (ty < (uint16_t)tabTop() || ty >= (uint16_t)(tabTop() + TAB_H)) return false;
  TabBtn b[2];
  const uint8_t n = tabBtns(b);
  for (uint8_t i = 0; i < n; i++) {
    const int x = tabBtnX(b, n, i);
    const int y = tabTop() + (TAB_H - b[i].h) / 2;
    if ((int)tx >= x && (int)tx < x + b[i].w
     && (int)ty >= y && (int)ty < y + b[i].h) { *out = b[i].key; return true; }
  }
  return false;
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
static uint16_t tabBg(Tab t) { (void)t; return hexToColor(cfg.bgColor); }

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

static void clearContent() {
  const int x = BORDER, y = contentTop();
  const int w = tft.width() - BORDER * 2, h = contentH();

  const int idx = (tab == TAB_SPEND) ? 1 : 0;
  contentHasBg = false;
  C_BG = tabBg(tab);

  // 내역 탭은 배경 그림을 쓰지 않는다 — 목록을 정확히 읽어야 하는 자리다.
  // (바탕색은 다른 탭과 같다. 그림만 안 깐다)
  // 화면을 돌린 기기도 쓰지 않는다(배경은 세로 232x192 로만 만들어 둔다).
  if (tab != TAB_HISTORY && bgHas[idx] && bgFile[idx].w == w && bgFile[idx].h == h) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", ART_DIR, bgFile[idx].file);
    fs::File f = LittleFS.open(path, "r");
    if (f) {
      static uint16_t line[240];
      bool ok = true;
      for (int j = 0; j < h && ok; j++) {
        if (f.read((uint8_t*)line, w * 2) != w * 2) ok = false;
        else tft.pushImage(x, y + j, w, 1, line);
      }
      f.close();
      if (ok) { contentHasBg = true; return; }
      // 읽다 실패하면 아래에서 바탕색으로 덮는다
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
// 같은 자리라도 바탕이 흰색이냐 검정이냐(또는 어두운 배경 사진이냐)에 따라
// 읽히는 색이 반대다. 그릴 때마다 고르지 않게 역할로 부른다.
//   inkMain  제목·이름처럼 가장 먼저 읽혀야 하는 것
//   inkSub   안내문
//   inkMuted 곁들이는 값(경과 시간 등)
// 배경 사진 위에는 늘 흰 글자(사진은 어두운 편이고 무슨 색이 올지 모른다).
// 사진이 없으면 바탕색의 밝기를 보고 고른다.
static bool lightBg() { return !contentHasBg && isLightColor(cfg.bgColor); }
static uint16_t inkMain()  { return lightBg() ? C_INK  : TFT_WHITE; }
static uint16_t inkSub()   { return lightBg() ? C_INK2 : TFT_LIGHTGREY; }
static uint16_t inkMuted() { return lightBg() ? C_INK3 : TFT_DARKGREY; }
// 방향색(초록/빨강)도 흰 바탕에서는 한 단계 진한 쪽을 쓴다.
static uint16_t inkEarn()  { return lightBg() ? C_EARN_D  : C_EARN; }
static uint16_t inkSpend() { return lightBg() ? C_SPEND_D : C_SPEND; }
static uint16_t inkDir(bool up) { return up ? inkEarn() : inkSpend(); }
static uint16_t inkTab()   { return (tab == TAB_SPEND) ? inkSpend()
                                  : (tab == TAB_EARN)  ? inkEarn() : C_NEUTRAL; }

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
  static uint16_t line[240];
  if (a.w > 240) { f.close(); return false; }
  for (int j = 0; j < a.h; j++) {
    if (f.read((uint8_t*)line, a.w * 2) != a.w * 2) { f.close(); return false; }
    if (transp) tft.pushImage(x, y + j, a.w, 1, line, ART_TRANSPARENT);
    else        tft.pushImage(x, y + j, a.w, 1, line);
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

  // 띠 위 글자도 바탕 밝기를 따른다 — 밝은 바탕이면 어두운 글자.
  const uint16_t fg = on ? (lightBg() ? C_INK : TFT_WHITE)
                         : (lightBg() ? C_INK3 : C_NAVOFF);
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
//  진행 화면 — 키링 → 확인 → 카드 → 확인 → 완료
// ══════════════════════════════════════════════════════════════════
// 글자를 되도록 줄이고 그림으로 말한다. 대상이 어린이라 읽는 것보다 보는 것이 빠르고,
// 그림은 서버에서 갈아 끼울 수 있어 현장에서 말이 바뀌어도 펌웨어를 다시 굽지 않는다.

// 아래쪽 확인 단추. 화면 폭을 다 쓰는 띠라 손가락으로 누르기 쉽다.
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

// 한 사람의 최근 내역 몇 줄. 사람 화면과 완료 화면이 같은 규칙으로 쓴다.
static void drawMiniFeed(int top, uint8_t n) {
  const int ROW = 18, pad = 22;
  const uint32_t elapsed = (millis() - whoFetchMs) / 1000;
  useFont(14);
  for (uint8_t i = 0; i < n && i < whoCount; i++) {
    const int y = top + i * ROW;
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

// ── 1단계: 누구인가 ───────────────────────────────────────────────
// 이름 · 남은 포인트 · 최근 내역. 여기서 확인을 눌러야 다음으로 간다 —
// 잘못 댄 키링을 되돌릴 수 있는 마지막 자리다.
static void drawPerson() {
  clearContent();
  const int top = contentTop();

  // 탭 줄이 커지면서 본문이 140px 로 줄었다. 이름 20 + 잔액 48 + 안내 14 + 두 줄(36)
  // 을 그 안에 넣으려고 자리를 촘촘히 잡았다 — 잔액은 크게 두는 쪽을 지켰다.
  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(curName[0] ? curName : curUid, tft.width() / 2, top + 14);
  useFont(0);

  char b[12];
  snprintf(b, sizeof(b), "%ld", (long)curBalance);
  contentText(inkMain());
  tft.drawString(b, tft.width() / 2, top + 52, 6);

  useFont(14);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMuted());
  tft.drawString("남은 포인트", tft.width() / 2, top + 88);
  useFont(0);

  if (whoCount) {
    tft.fillRect(BORDER + 20, top + 99, tft.width() - (BORDER + 20) * 2, 1,
                 lightBg() ? C_LINE : C_TABBG);
    drawMiniFeed(top + 102, 2);
  }

  drawBigButton(cfg.attendanceMode ? (tab == TAB_SPEND ? "사용하기" : "지급하기") : "확인",
                inkTab());
}

// ── 2단계: 무엇을 (카드를 기다린다) ───────────────────────────────
// 등록된 카드를 목록으로 함께 보여준다. 어떤 카드가 있는지 모르면 아무 카드나
// 대 보게 되고, 그때마다 "이 기기에서 쓸 수 없는 카드" 가 떠서 답답해진다.
static void drawWaitCard() {
  clearContent();
  const int top = contentTop();

  useFont(20);
  tft.setTextDatum(MC_DATUM);
  contentText(inkMain());
  tft.drawString(tab == TAB_SPEND ? "사용 카드를 대주세요" : "지급 카드를 대주세요",
                 tft.width() / 2, top + 20);
  useFont(14);
  contentText(inkMuted());
  tft.drawString(curName[0] ? curName : curUid, tft.width() / 2, top + 44);
  useFont(0);

  tft.fillRect(BORDER + 20, top + 58, tft.width() - (BORDER + 20) * 2, 1,
               lightBg() ? C_LINE : C_TABBG);

  // 이 기기에서 쓸 수 있는 카드만
  const int ROW = 24, pad = 22;
  int y = top + 66;
  uint8_t shown = 0;
  useFont(14);
  for (uint8_t i = 0; i < cardCount; i++) {
    if (!cardFitsTab(cards[i])) continue;
    if (y + ROW > contentTop() + bodyH()) break;
    tft.setTextDatum(ML_DATUM);
    contentText(inkSub());
    tft.drawString(cards[i].name, BORDER + pad, y + ROW / 2);

    char amt[12];
    snprintf(amt, sizeof(amt), "%ld", (long)cards[i].amount);
    tft.setTextDatum(MR_DATUM);
    contentText(inkTab());
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

  drawBigButton("취소", C_NEUTRAL);
}

// ── 3단계: 되었습니다 ─────────────────────────────────────────────
// 서버에서 올린 그림 한 장으로 말한다. 글자는 잔액과 증감 한 줄뿐이다.
static void drawDone() {
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
  whoCount = 0;
  lastUid[0] = '\0';                  // 같은 키링을 곧바로 다시 댈 수 있게 쿨다운을 푼다
}

// 현재 탭의 내용. 오버레이가 떠 있는 동안에는 부르지 않는다.
static void drawTabContent() {
  if (!nfcReady && tab != TAB_HISTORY) { drawNoNfc(); return; }
  if (tab == TAB_HISTORY) { drawHistory(); return; }
  switch (step) {
    case STEP_PERSON:  drawPerson();   break;
    case STEP_CARD:    drawWaitCard(); break;
    case STEP_DONE:    drawDone();     break;
    default:           drawTagScreen(); break;   // STEP_IDLE — "키링을 대주세요"
  }
}

// 화면 전체. 탭이 바뀌면 테두리 색도 바뀌므로 통째로 다시 그린다.
static void drawScreen() {
  drawFrame();
  drawHeader();
  if (tab != TAB_HISTORY) drawTabs();   // 내역은 탭 줄 자리까지 목록이 쓴다
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
//  태그 처리
// ══════════════════════════════════════════════════════════════════
static void uidToStr(const uint8_t* uid, uint8_t len, char* out, size_t cap) {
  size_t n = 0;
  for (uint8_t i = 0; i < len && n + 2 < cap; i++) n += snprintf(out + n, cap - n, "%02X", uid[i]);
  out[n] = '\0';
}

// 결과·오류를 몇 초 띄우고 원래 탭으로 돌아간다.
#define OVERLAY_MS 3000
// 내역 탭의 잔액 조회는 읽을 것이 여러 줄이라 조금 더 오래 둔다.
#define WHO_OVERLAY_MS 5000

// 키링이 아닌 것을 댔을 때의 안내. 쿨다운은 handleTag 와 같은 변수를 쓴다 —
// 카드를 리더 위에 올려 둔 채로 두면 안내가 끝없이 다시 뜨기 때문이다.
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

// 확인을 눌렀을 때 실제로 처리한다. 카드가 정해져 있어야 한다.
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
    if (i >= 0 && cardFitsTab(cards[i])) return i;
  }
  for (uint8_t i = 0; i < cardCount; i++)
    if (cardFitsTab(cards[i])) return (int8_t)i;
  return -1;
}

// 확인 단추를 눌렀을 때. 단계마다 뜻이 다르다.
static void onConfirm() {
  switch (step) {
    case STEP_PERSON: {
      // 출석모드는 여기서 곧바로 처리한다 — 줄이 길 때 태그 한 번으로 끝내려는 것이다
      if (cfg.attendanceMode) {
        const int8_t i = attendanceCardIndex();
        if (i < 0) {
          sndFail();
          drawErrorScreen("등록된 카드가 없습니다");
          overlayUntil = millis() + OVERLAY_MS;
          return;
        }
        curCard = i;
        commitCard();
        return;
      }
      sndMode();
      step = STEP_CARD;
      stepAt = millis();
      drawWaitCard();
      break;
    }
    case STEP_CARD:                    // '취소'
      sndMode();
      resetStep();
      drawScreen();
      break;
    default:                           // 완료 화면에는 단추가 없다(시간이 지나면 저절로 돌아간다)
      break;
  }
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
    if (ci < 0) {                      // 카드가 아니거나 등록되지 않았다
      sndFail();
      drawErrorScreen("등록되지 않은 카드입니다");
      overlayUntil = millis() + OVERLAY_MS;
      reportSeen(s);
      lastActivity = millis();
      return;
    }
    if (!cardFitsTab(cards[ci])) {
      sndFail();
      drawErrorScreen(cards[ci].spend ? "사용 카드입니다" : "지급 카드입니다");
      overlayUntil = millis() + OVERLAY_MS;
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

  // 키링이다. 누구인지 서버에 묻는다 — 등록되지 않았으면 아무것도 만들지 않는다.
  drawWorking();
  if (!whoFetch(s)) {
    sndFail();
    drawErrorScreen("잔액을 불러오지 못했습니다");
    overlayUntil = millis() + OVERLAY_MS;
    lastActivity = millis();
    return;
  }
  if (!whoKnown) {
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

  sndMode();
  strlcpy(curUid, s, sizeof(curUid));
  strlcpy(curName, whoName, sizeof(curName));
  curBalance = whoBalance;
  curCard = -1;
  step = STEP_PERSON;
  stepAt = millis();
  overlayUntil = 0;
  drawPerson();
  Serial.printf("[키링] %s(%s) 잔액 %ld\n", curUid, curName, (long)curBalance);
  lastActivity = millis();
}

// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== TalentNfcReader 시작 ===");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_TFT_BL, OUTPUT);
  analogWrite(PIN_TFT_BL, 0);
  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);   // 딥슬립 기상용(ext0). 평소에는 폴링만 쓴다.

  // 서버에서 받은 포인트 그림을 담아 두는 곳. 못 열어도 계속 간다 —
  // 그때는 펌웨어에 구워 넣은 그림으로 돈다.
  if (!LittleFS.begin(true)) Serial.println("LittleFS 를 열지 못했습니다 — 받은 그림은 쓰지 못합니다.");

  // 캐시된 설정을 먼저 읽는다. 서버 왕복을 기다리지 않고 바로 화면을 그리기 위함이다.
  cfgLoadCache();
  cfgApplyMode();

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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi 연결됨 %s\n", WiFi.localIP().toString().c_str());
    // 설정을 받아온다. 실패해도 캐시(또는 기본값)로 계속 간다.
    if (cfgFetch()) {
      cfgApplyMode();                    // 잠금/기본 모드가 바뀌었을 수 있다
      cardsFetch();                      // 카드 목록도 함께 받아 둔다
      analogWrite(PIN_TFT_BL, cfg.backlight);  // 밝기만 바로 반영(다시 페이드하면 깜빡인다)
    }
    // 이름표는 태깅 즉시 이름을 띄우기 위해 미리 받아 둔다.
    // 내역은 그 탭을 열 때 받는다 — 부팅을 그만큼 늦출 이유가 없다.
    rosterFetch();
    // 그림은 없는 것만 받는다. 이미 있으면 통신하지 않아 부팅이 늦어지지 않는다.
    artSync();
  } else {
    // 연결 못 해도 계속 진행한다. 태깅할 때 실패 사유를 화면에 보여준다.
    Serial.println("WiFi 연결 실패 — 태깅 시 서버 요청이 실패합니다.");
  }
  Serial.printf("[설정] 출처=%s step=%ld 잠금=%u 슬립=%s\n",
                cfgFromServer ? "서버" : "캐시/기본값", (long)cfg.talentStep, cfg.modeLock,
                cfg.sleepEnabled ? "켬" : "끔");

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

  drawScreen();
  lastActivity = millis();
}

void loop() {
  const uint32_t now = millis();

  // ── 화면 터치: 탭 전환 ──
  // 정전식 패드 대신 디스플레이의 XPT2046 을 읽는다. getTouch() 는 눌린 동안
  // 계속 true 라, 디바운스로 한 번만 받는다.
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty) && now - lastTouchMs > cfg.touchDebounceMs) {
    lastTouchMs = now;

    // ── 아래쪽 확인 단추 ──
    // 진행 중일 때만 있다. 화면 폭을 다 쓰는 띠라 다른 것과 겹치지 않는다.
    if (tab != TAB_HISTORY && !overlayUntil
        && (step == STEP_PERSON || step == STEP_CARD) && btnHit(tx, ty)) {
      onConfirm();
      return;
    }
    // 헤더의 내역 버튼 — 탭 줄 위에 있어 겹치지 않지만 순서를 정해 둔다.
    if (histHit(tx, ty)) {
      // 다시 누르면 보던 탭으로 돌아간다 — 띠 전체가 단추라 "나가는 문" 도 같아야
      // 헤맬 일이 없다.
      //
      // cfgApplyMode() 를 쓰지 않는다: 그 함수는 내역을 보고 있으면 탭을 건드리지
      // 않는다(설정 주기 갱신이 사람을 내역에서 쫓아내지 않게 하려고). 그래서
      // 여기서 부르면 아무 일도 일어나지 않았다.
      if (tab == TAB_HISTORY) {
        tab = prevTab;
        overlayUntil = 0;
        resetStep();
        sndMode();
        drawScreen();
        lastActivity = now;
        return;
      }
      if (tab != TAB_HISTORY) {
        prevTab = tab;                     // 돌아올 자리를 기억해 둔다
        tab = TAB_HISTORY;
        overlayUntil = 0;
        resetStep();
        sndMode();
        // 열 때는 늘 가장 최근 묶음부터 — 지난번에 넘겨 둔 쪽에서 시작하면
        // 방금 찍힌 것이 안 보여 "안 들어갔다" 로 읽힌다.
        if (feedFetchMs == 0 || now - feedFetchMs > 30000 || feedPage != 0) {
          drawFrame(); drawHeader();
          historyLoad(0);
        }
        drawScreen();
      }
      lastActivity = now;
      return;
    }

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

    Tab hit;
    if (tabHit(tx, ty, &hit) && hit != tab) {
      tab = hit;
      overlayUntil = 0;                  // 결과가 떠 있었다면 지운다
      resetStep();                       // 지급하려다 사용 탭으로 넘어가는 사고를 막는다
      sndMode();
      // 여기로 오는 것은 지급·사용 두 칸뿐이다(tabBtns). 내역은 헤더 띠로만 간다.
      drawScreen();
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
  if ((step == STEP_PERSON || step == STEP_CARD)
      && !overlayUntil && now - stepAt > 60000) {
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
        if (nfc.ntag2xx_ReadPage(3, cc) && cc[0] == 0xE1) {
          Serial.printf("[태그] %s (%u바이트) CC=%02X %02X %02X %02X\n",
                        dbg, uidLen, cc[0], cc[1], cc[2], cc[3]);
          handleTag(uid, uidLen);
        } else {
          Serial.printf("[태그] %s — 키링이 아니라 넘깁니다 (CC %02X %02X %02X %02X)\n",
                        dbg, cc[0], cc[1], cc[2], cc[3]);
          // 실패한 교환은 대상 선택 상태를 망가뜨려 **다음 읽기까지 전부 실패**한다.
          // (카드 한 번 댔더니 그 뒤로 키링이 안 먹던 증상이 이것이었다)
          nfc.SAMConfig();
          rejectTag(dbg);
        }
      } else {
        Serial.printf("[태그] 길이 %u — 버립니다(응답 프레임이 깨졌습니다)\n", uidLen);
      }
    }
  }

  // ── 내역 탭의 "n분 전" 갱신 ──
  // 열어 둔 채 두면 시간이 멈춘 것처럼 보인다. 30초에 한 번 다시 그린다.
  static uint32_t lastAgeRedraw = 0;
  if (tab == TAB_HISTORY && !overlayUntil && now - lastAgeRedraw > 30000) {
    lastAgeRedraw = now;
    drawHistory();
  }

  // ── 설정 주기 갱신 ──
  // 행사 중에 관리자가 값을 바꾸면 기기를 만지지 않고도 반영되게 한다.
  // 한 번에 1초 남짓 멈추므로 주기는 넉넉히(기본 5분) 잡는다.
  // rotation 은 화면을 다시 그려야 해서 여기서는 반영하지 않는다 — 다음 부팅에 적용된다.
  if (WiFi.status() == WL_CONNECTED && now - lastCfgFetch > cfg.ttlSec * 1000UL) {
    bool got = cfgFetch();
    lastCfgFetch = millis();             // 실패해도 매 루프 재시도하지 않게
    // talentStep·modeLock 이 바뀌면 탭 구성과 내용이 달라지므로 함께 다시 그린다
    if (got) {
      cfgApplyMode();
      rosterFetch();                     // 이름이 새로 붙었을 수 있다
      cardsFetch();                      // 카드가 늘거나 금액이 바뀌었을 수 있다
      artSync();                         // 그림이 바뀌었으면 이름이 달라져 다시 받는다
      if (!overlayUntil) drawScreen();
    }
  }

  // ── 무입력이면 잠든다 (서버에서 켠 기기만) ──
  if (cfg.sleepEnabled && now - lastActivity > cfg.sleepTimeoutMs) goToDeepSleep();

  delay(10);
}
