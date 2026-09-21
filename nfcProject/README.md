# nfcProject — WiFi WebSocket 디스플레이 (CST820 원형)

`TalentNfcReader_CST820_v1.0` 의 화면·와이파이 코드를 토대로 만든, **화면 전용** 장치입니다.
NFC 를 직접 읽지 않습니다 — 외부(NFC 시스템)가 WebSocket 으로 상태를 보내면 그 그림으로 바꿉니다.

## 하드웨어
- **LILYGO T-RGB 2.1" 원형** — ESP32-S3 · ST7701S RGB 480×480 · CST820 터치 (v1.0 과 같은 보드/모듈)

## 하는 일
1. `config.h` 의 와이파이에 붙는다 (붙을 때까지 화면은 계속 살아 있다)
2. `nfc-display.local` 로 자기 이름을 알린다 (mDNS)
3. 포트 **81** 에 WebSocket 서버를 연다
4. 대기 화면은 항상 **사용 활성(useEnable)** 그림이다 (비활성 그림은 더 쓰지 않는다)
5. 화면을 짧게 누르면(또는 시리얼 `n`) microSD 에 받아 둔 사진이 순서대로 넘어간다 —
   활성 → 사진1 → … → 사진N → 다시 활성. 길게 누르면 서버에서 사진을 다시 받는다
   (이름표의 `img` 해시를 `/talent/<UID>.ver` 에 적어 두고, 같으면 받지 않는다 — 바뀐 사진만 받는다)
6. 와이파이에 붙어 있으면 오른쪽 위에 연결 아이콘을 겹친다

## 설정 — `config.h`
```c
#define WIFI_SSID     "여기에_SSID"       // 2.4GHz 공유기
#define WIFI_PASSWORD "여기에_비밀번호"
#define MDNS_HOSTNAME "nfc-display"       // nfc-display.local
#define WS_PORT       81                  // ws://nfc-display.local:81/
```

## WebSocket 규약
접속: `ws://nfc-display.local:81/` (또는 시리얼에 찍히는 IP)

**보내는 쪽 → 보드** (아무 형식이나):
| 형식 | 예 |
|------|-----|
| JSON `state` | `{"state":"enable"}`(활성 화면으로) · `{"state":"next"}`·`{"state":"toggle"}`(다음 사진) · `{"state":"prev"}` |
| 그냥 글자 | `enable` · `next` · `toggle` · `prev` |
| NFC 알림 | `{"uid":"04A1B2C3","name":"홍길동","points":1200}` |

예전 규약의 `disable`·`{"enabled":false}` 는 활성 화면으로 돌아가는 것으로 받는다.

**보드 → 붙은 쪽** (상태가 바뀌거나 새로 붙을 때마다):
```json
{"mode":"idle","photo":"enable","index":0,"count":52,"online":true}
```
(`photo` 는 `enable` 또는 지금 보이는 사진의 UID)

## 시리얼 명령 (115200)
| 글자 | 하는 일 |
|------|---------|
| `n` | 다음 사진 (터치와 같다) |
| `p` | 이전 사진 |
| `h` | 활성 화면으로 |
| `s` | 넘겨 볼 사진 목록 |
| `l` | SD `/talent` 파일 목록 |
| `r` | 재부팅 |
| `?` | 도움말 |

브라우저에서 빠른 시험:
```js
const ws = new WebSocket("ws://nfc-display.local:81/");
ws.onmessage = e => console.log(e.data);
ws.onopen = () => ws.send('{"state":"enable"}');
```

## 빌드 (Arduino IDE 도구 메뉴)
- 보드: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM**
- Flash Size: **16MB**
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)** — 그림 두 장이 플래시에 들어간다
- USB CDC On Boot: **Enabled**
- 라이브러리: **GFX Library for Arduino** · **ArduinoJson** · **WebSockets** (by Markus Sattler / Links2004)

arduino-cli:
```bash
arduino-cli compile --fqbn \
  esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc \
  nfcProject
```

> LilyGo-T-RGB 라이브러리(`LV_Helper.h`·`LilyGo_RGBPanel.h`)는 **넣지 마세요** — lvgl/SensorLib
> 버전 충돌로 빌드가 깨집니다. 이 스케치는 Arduino_GFX 로 직접 그립니다(v1.0 과 같은 이유).

## 그림 바꾸기
`useEnable.h` 는 480×480 RGB565 이미지 헤더입니다(`useDisable.h` 는 더 쓰지 않아 빌드에 들어가지
않습니다). 자기 그림으로 바꾸려면 `tools/img2rgb565_dither.py` 로 다시 만들어 덮어쓰세요
(`--var` 이름은 `USEENABLE` 로 유지):
```bash
cd /Users/kimyongmin/workspace/arduino
python3 tools/img2rgb565_dither.py <활성그림> --width 480 --height 480 --fit cover \
    --var USEENABLE  -o nfcProject/useEnable.h
```

## 파일
| 파일 | 내용 |
|------|------|
| `nfcProject.ino` | 화면·와이파이·mDNS·WebSocket 서버 |
| `config.h` | 와이파이·mDNS·포트 설정 (여기만 고치면 된다) |
| `useEnable.h` | 480×480 활성 그림 (대기 화면 첫 장) |
| `useDisable.h` | 쓰지 않음 (포함하지 않는다) |
| `connected.h` | 50×35 연결 아이콘 (투명색 0xF81F) |
