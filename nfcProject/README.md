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
7. 리더(`nfcProjectClient`)가 `{"uid":"…"}` 를 보내면 **서버에 이름·잔액을 물어** 그 아이 사진 위에 띄운다.
   출석모드가 켜져 있으면 물어보는 대신 **바로 출석 포인트를 지급**한다

## 서버 설정 — 관리자 화면 '펀펀포인트 → NFC 관리 → 리더설정 → v2.0'

부팅해서 와이파이에 붙을 때, 그 뒤 `CONFIG_TTL_MS`(5분)마다 `GET /api/talent/config/v2?device=<TALENT_DEVICE_ID>` 로 받아 온다.
못 받으면 들고 있던 값으로 계속 돈다 — 와이파이가 흔들릴 때마다 밝기가 튀거나 출석모드가 꺼지면 현장이 더 혼란스럽다.

| 설정 | 하는 일 |
|------|---------|
| 화면 밝기 `backlight` | 1(어둡다)~16(밝다). 받는 즉시 백라이트에 적용 (펌웨어 기본값은 `BL_LEVEL`) |
| 출석모드 `attendanceMode` | 켜면 키링을 댄 그 자리에서 바로 출석 포인트를 지급한다 |
| 출석에 쓸 카드 `attendanceCard` | **얼마를 어떤 사유로** 줄지는 서버에 등록된 이 지급 카드가 정한다. 비우면 지급하지 않고 사진·잔액만 띄운다 |

금액을 기기가 보내지 않는 이유: 리더는 현장에 놓인 물건이라 요청을 흉내 내기 쉽다.
값이 서버에만 있으면 그래 봐야 관리자가 정한 범위를 벗어나지 못한다(v1.0 리더와 같은 규칙).

**서버와 이야기하는 쪽은 이 기기 하나다.** 리더는 읽은 UID 를 넘기기만 한다 —
한 태깅이 두 곳에서 처리되면 같은 출석이 두 번 올라간다.

같은 키링이 `ATTEND_COOLDOWN_MS`(10분) 안에 또 오면 `이미 출석했어요` 만 띄우고 주지 않는다.
기기에 시계(RTC)가 없어 "하루 한 번" 을 알 수 없어서 시간 간격으로 막는다.

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
| NFC 알림 | `{"uid":"04A1B2C3"}` — **UID 하나면 된다**(이름·잔액은 이 보드가 서버에 묻는다) |

예전 규약의 `disable`·`{"enabled":false}` 는 활성 화면으로 돌아가는 것으로 받는다.
`{"uid":…,"name":…,"points":…}` 처럼 이름·잔액이 함께 오면 묻지 않고 그대로 띄운다(옛 리더 호환).

**보드 → 붙은 쪽** (상태가 바뀌거나 새로 붙을 때마다):
```json
{"mode":"idle","photo":"enable","index":0,"count":52,"online":true}
{"mode":"nfc","uid":"04A1B2C3","name":"홍길동","points":1205,"known":true,"note":"출석 완료 +5P","online":true}
```
(`photo` 는 `enable` 또는 지금 보이는 사진의 UID · `note` 는 하단에 띄운 한 줄 안내)

리더는 이 `mode:"nfc"` 응답으로 소리를 고른다 — 서버와 말하지 않으므로 결과를 알 길이 이것뿐이다.

## 시리얼 명령 (115200)
| 글자 | 하는 일 |
|------|---------|
| `n` | 다음 사진 (터치와 같다) |
| `p` | 이전 사진 |
| `h` | 활성 화면으로 |
| `s` | 넘겨 볼 사진 목록 |
| `l` | SD `/talent` 파일 목록 |
| `c` | 서버 설정(밝기·출석모드) 지금 다시 받기 |
| `i` | 기기 ID·밝기·출석모드·출석 카드 상태 |
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
