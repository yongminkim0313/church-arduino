# ardoino — TTGO T-Display 교회 로고/애니메이션

LILYGO TTGO T-Display(ESP32)에 **교회 로고(디더링 RGB565)** + **비둘기가 날아드는 애니메이션**을 띄우는 Arduino 프로젝트.

```
ardoino/
├── ChurchLogoDisplay/          # Arduino 스케치 (이 폴더를 IDE로 열기)
│   ├── ChurchLogoDisplay.ino
│   └── logo.h                  # 로고 RGB565 헤더 (기본은 플레이스홀더)
├── tools/
│   └── img2rgb565_dither.py    # 이미지 → RGB565 헤더 (Floyd-Steinberg 디더링)
├── docs/
│   └── TFT_eSPI_setup.md       # TFT_eSPI 드라이버(Setup25) 설정법
└── README.md
```

## 현재 단계
T-Display에 교회 로고 + 비둘기 날아드는 애니메이션 표시. 위쪽 버튼(GPIO35)으로 인트로 재생.
로고는 기본 십자가 플레이스홀더이며, 아래 도구로 실제 로고를 넣으면 `pushImage` 경로로 표시됩니다.

## 클론 후 준비

한글 폰트 헤더는 **생성물이라 저장소에 없다**(11MB × 2). 아래를 한 번 돌리면 만들어진다.

```bash
./tools/make-fonts.sh
```

`ChurchDisplayRx` 계열만 음절 전체(11,172자)를 12px 로 쓰고, `TalentNfcReader`(14px)와
`GodlifeScheduleNext`(16px)는 KS X 1001 상용 2,350자로 줄였다 — 큰 글씨는 전체를 담으면
플래시에, 통신을 하는 쪽은 글리프 메트릭이 힙에 부담이라서다.
기본으로 `~/Library/Fonts/NanumGothicBold.ttf` 를 쓴다. 다른 폰트를 쓰려면 경로를 넘긴다:
`./tools/make-fonts.sh /경로/폰트.ttf`

WiFi 비밀번호 등 인증정보도 저장소 밖에 둔다. [secrets.example.h](secrets.example.h) 를
복사해 채운 뒤 아래 위치에 두면 모든 스케치가 `#include <ChurchSecrets.h>` 로 공유한다.

```
~/Documents/Arduino/libraries/ChurchSecrets/ChurchSecrets.h
```

필요한 라이브러리:

```bash
arduino-cli lib install "TFT_eSPI" "WebSockets" "ArduinoJson" "NimBLE-Arduino" "Sensirion I2C SHT4x"
```

TFT_eSPI 는 `Setup25_TTGO_T_Display.h` 활성화가 필요하다 — [docs/TFT_eSPI_setup.md](docs/TFT_eSPI_setup.md)

## RGB565 이미지 바이트 순서 (자주 틀리는 곳)

`uint16_t logo_data[] = { 0xF800, ... }` 같은 배열을 화면에 그릴 때, **어느 경로로 넣느냐에 따라
`setSwapBytes` 설정이 반대**다. 잘못 쓰면 빨강(`0xF800`)이 짙은 파랑(`0x00F8`)으로 뒤바뀐다.

| 경로 | 필요한 설정 | 쓰는 스케치 |
|------|-------------|-------------|
| `tft.pushImage(...)` — 화면에 직접 | `tft.setSwapBytes(true)` | ChurchLogoOnly |
| `spr.pushImage(...)` → `spr.pushSprite(...)` | `spr.setSwapBytes(true)` | ChurchLogoDisplay, ChurchWeatherStation |
| 스프라이트 도형/텍스트 (`fillRect`, `drawString` …) | 설정 불필요 | 전부 |

이유: `TFT_eSprite` 는 내부 버퍼를 **빅엔디언**으로 들고 있고(`pushSprite` 가 바이트를 그대로
SPI 로 흘려보낸다), ESP32 의 `uint16_t` 배열은 **리틀엔디언**이다. `setSwapBytes(true)` 가
복사·전송 과정에서 순서를 맞춰준다. 도형/텍스트 함수(`drawFastHLine` 등)는 내부에서 이미
`color>>8 | color<<8` 로 저장하므로 영향을 받지 않는다 — 그래서 배경은 멀쩡한데 로고만
색이 뒤집히는 증상이 나온다.

색 전체가 음화처럼 반전되는 것은 다른 문제이며 `tft.invertDisplay(1)` 로 잡는다.

## 스케치 목록

| 스케치 | 보드 | 하는 일 |
|--------|------|---------|
| `ChurchLogoOnly` | T-Display | 교회 로고 한 장만 전체화면(240×135)으로 표시 |
| `ChurchLogoDisplay` | T-Display | 교회 로고 + 비둘기 애니메이션 |
| `ChurchWeatherStation` | T-Display | 로고 인트로 → SHT41 온습도 표시 + 서버 POST |
| `SHT41Monitor` | T-Display | 온습도 측정 + 서버 POST |
| `SHT41MonitorC3` | ESP32-C3 | 온습도 측정 → 디스플레이 WebSocket 송신 + 서버 POST |
| `ChurchDisplayRx` | T-Display | WebSocket 서버. 센서 푸시를 한글로 표시, 온습도 그래프 |
| `SHT41MonitorC3BLE` | ESP32-C3 | 위와 같되 **BLE** 로 디스플레이에 직접 송신 |
| `ChurchDisplayRxBLE` | T-Display | **BLE** GATT 서버판 디스플레이 |
| `GodlifeScheduleNext` | T-Display | 하루동행 다음 일정 표시. 넘치는 글은 마퀴로 흐름 |
| `TalentNfcReader` | ESP32-S3 | NFC 키링 펀펀포인트 지급·사용 (PN532 + ILI9341 + XPT2046 터치) |

## 하드웨어

| 항목 | 값 |
|------|-----|
| 개발보드 | LILYGO TTGO T-Display (ESP32) |
| 칩 | ESP32-D0WDQ6 rev v1.1, 듀얼코어 240MHz, WiFi+BT |
| USB-시리얼 | CH9102 (VID 0x1A86) |
| Flash | 16MB |
| MAC | 38:18:2b:db:68:78 |
| 디스플레이 | 1.14" IPS LCD, ST7789V, 240×135 (가로) |

## Arduino IDE 보드 설정 (Tools)

| 항목 | 값 |
|------|-----|
| Board | ESP32 Dev Module (또는 TTGO T-Display) |
| Flash Size | 16MB (128Mb) |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Partition Scheme | Default |
| PSRAM | Disabled |
| Upload Speed | **115200** (921600 업로드 실패 시 낮춘 값) |
| Port | /dev/cu.usbserial-5B34014705 (CH9102) |

## 소프트웨어 / 라이브러리
- Arduino IDE 2.x (macOS)
- **TFT_eSPI (Bodmer)** — `User_Setup_Select.h` 에서 `Setup25_TTGO_T_Display.h` 활성화 (→ [docs/TFT_eSPI_setup.md](docs/TFT_eSPI_setup.md))
- 화면 방향: `setRotation(1)` (가로)

## 핀 정보 (다이어그램 기준)
- **LCD(SPI)**: MOSI=19, SCLK=18, CS=5, DC=16, RST=23, BL=4
- **버튼**: GPIO0(아래), GPIO35(위)
- **I2C**(온습도 센서용, 예정): SDA=21, SCL=22

## 디스플레이 메모
- 내장 `LED_BUILTIN` 없음 → LCD 백라이트는 **GPIO4**(HIGH=켜짐).
- 하단 깜빡이는 LED = **충전 표시등**(배터리 미연결 시 깜빡임), 코드 제어 불가.
- 색 반전 시에만 `tft.invertDisplay(1)`. 현재 구성에선 넣지 않는 게 정상.
- 스프라이트 `pushImage` 에 배열 넘길 때 **`(uint16_t *)` 캐스팅** 필요.
- 이미지 컬러는 **RGB565(16비트)**, 색감 개선 위해 **Floyd-Steinberg 디더링** 적용.

## 빌드/업로드
1. [docs/TFT_eSPI_setup.md](docs/TFT_eSPI_setup.md) 대로 TFT_eSPI 를 Setup25 로 맞춥니다.
2. Arduino IDE 에서 `ChurchLogoDisplay/ChurchLogoDisplay.ino` 를 엽니다.
3. 위 표대로 보드/포트/업로드 속도(115200)를 설정하고 업로드합니다.

## 실제 로고 넣기 (디더링)
```bash
# Pillow 필요: pip3 install pillow
python3 tools/img2rgb565_dither.py church_logo.png \
    --width 96 --height 96 --var logo_data \
    -o ChurchLogoDisplay/logo.h
```
생성되면 `logo.h` 의 `HAVE_LOGO_IMAGE` 가 1 이 되고, 스케치가 자동으로 `pushImage((uint16_t *)logo_data)` 로 로고를 그립니다. 카드 크기(코드의 100px)와 `--width/--height` 를 맞추세요.

## 앞으로 (보류/다음 단계)
- 온습도 센서: **BME280(실내) / SHT41·SHT45(실외)** — 논의만, T-Display 미연결. 연결 시 I2C(SDA=21, SCL=22).
- **실시간 수신**: `ChurchDisplayRx/` — WebSocket 서버를 열어 센서 보드의 푸시를 한글로 표시 (완료)
- **센서 → 디스플레이 직결**: `SHT41MonitorC3/` 가 mDNS 로 디스플레이를 찾아 WebSocket 으로 직접 송신 (완료)
- 인증정보는 `ChurchSecrets` 라이브러리로 분리 — [secrets.example.h](secrets.example.h) 참고
- 한글 폰트 생성: `tools/ttf2vlw.py` (TTF → TFT_eSPI VLW 스무스폰트)
- **NFC 펀펀포인트 리더**: `TalentNfcReader/` — ESP32-S3 + PN532 + ILI9341(XPT2046 터치).
  어린이 키링과 지급/사용 카드를 두 단계로 받고, 화면(시작·헤더·탭·배경·완료·카드 그림)은
  yvServer 에서 갈아 끼운다 (실기 동작 확인)
- **하루동행 일정 표시**: `GodlifeScheduleNext/` — jdServer `/api/godlife/schedule/next` 를 읽어
  다음 일정을 표시, 화면을 넘치는 글은 마퀴로 흘림 (보드에 구워 실제 응답 파싱까지 확인 —
  [README](GodlifeScheduleNext/README.md))
- **부팅 로고 → 온습도 화면 전환** 흐름 추가.
