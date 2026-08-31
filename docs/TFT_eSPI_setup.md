# TFT_eSPI 설정 (TTGO T-Display)

TFT_eSPI 는 **라이브러리 폴더 안의 설정 파일**로 핀/드라이버를 고르는 구조라, 스케치 코드만으로는 바뀌지 않습니다. 아래 한 번만 맞춰두면 됩니다.

## 1. 라이브러리 설치
Arduino IDE → 라이브러리 매니저 → **"TFT_eSPI" (Bodmer)** 설치.

## 2. 드라이버 선택 (Setup25)
`TFT_eSPI/User_Setup_Select.h` 를 열어 다음 한 줄의 주석을 해제하고, 다른 `#include <User_Setup...>` / `Setup..` 은 주석 처리합니다.

```cpp
// User_Setup_Select.h
//#include <User_Setup.h>                           // ← 이건 주석 처리
#include <User_Setups/Setup25_TTGO_T_Display.h>     // ← 이 줄 활성화
```

`Setup25_TTGO_T_Display.h` 가 T-Display 의 ST7789V 핀을 정의합니다:

| 신호 | GPIO |
|------|------|
| MOSI | 19 |
| SCLK | 18 |
| CS   | 5  |
| DC   | 16 |
| RST  | 23 |
| BL(백라이트) | 4 |

> 라이브러리를 업데이트하면 이 선택이 초기화될 수 있으니, 업데이트 후 다시 확인하세요.

## 3. 보드/포트 설정 (Tools 메뉴)

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

## 4. 확인 포인트
- 화면이 **가로 240x135** 로 나와야 정상 → 스케치에서 `setRotation(1)`.
- 색이 반전돼 보이면 그때만 `tft.invertDisplay(1)`. 현재 구성에선 넣지 않는 게 정상.
- `LED_BUILTIN` 은 없습니다. 백라이트는 GPIO4 로 직접 제어(HIGH=켜짐).
- 하단에서 깜빡이는 LED 는 **충전 표시등**(배터리 미연결 시 깜빡임)이라 코드로 제어 불가.
