# nfcProject — WiFi WebSocket 디스플레이 (CST820 원형)

`TalentNfcReader_CST820_v1.0` 의 화면·와이파이 코드를 토대로 만든, **화면 전용** 장치입니다.
NFC 를 직접 읽지 않습니다 — 외부(NFC 시스템)가 상태를 보내면 그 그림으로 바꿉니다.

리더(`nfcProjectClient`)는 **두 갈래로 온다**: 옆면 4핀 커넥터에 물린 **선(I2C)** 이 1순위,
**WebSocket** 이 2순위다. 브라우저·폰은 예전처럼 WebSocket 으로 붙는다.
두 길에 흐르는 글자는 같아서 받는 곳도 `handleCommand` 하나다 — 아래 [리더와 잇는 선](#리더와-잇는-선-i2c) 참고.

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
7. 리더가 선(I2C)에 물려 있으면 `LINK_POLL_MS`(50ms)마다 "줄 것 있나" 하고 읽어 온다
8. 리더(`nfcProjectClient`)가 `{"uid":"…"}` 를 보내면 **서버에 이름·잔액을 물어** 그 아이 사진 위에 띄운다.
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

#define LINK_I2C_ENABLE  1                // 리더와 잇는 선 (0 = WebSocket 만)
#define LINK_I2C_ADDR 0x30                // 리더의 I2C 주소 (리더 config.h 와 같아야 한다)
#define LINK_POLL_MS    50                // 리더에게 "줄 것 있나" 를 묻는 주기
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

리더는 이 응답으로 소리를 고른다 — 서버와 말하지 않으므로 결과를 알 길이 이것뿐이다.

## 리더와 잇는 선 (I2C)

리더(`nfcProjectClient`)를 옆면 1.25mm **4핀 커넥터**에 물리면 공유기·mDNS 를 거치지 않는다 —
망이 죽어도 태깅이 되고, 왕복이 수백 ms 에서 수 ms 로 준다. **WebSocket 서버는 그대로 열려 있다**
(브라우저·폰은 계속 그쪽으로 붙는다).

| 4핀 커넥터 | 리더 (ESP32-C3) |
|------------|-----------------|
| GND | GND |
| GPIO8 (SDA) | GPIO20 |
| GPIO48 (SCL) | GPIO21 |
| 3V3 | **잇지 말 것** — 리더는 자기 USB 로 먹인다 |

리더는 이 버스의 **슬레이브(`0x30`)** 다. 터치(`0x15`)·화면 확장칩 XL9535(`0x20`)와 겹치지 않는다.
풀업은 이 보드에 이미 있다.

**UART 가 아닌 이유**: 이 보드가 밖으로 내주는 것은 그 커넥터 하나뿐이고, RGB 패널이
`2·3·5·6·7·9~18·21·41·42·45·47` 을, SD 가 `38·39·40` 을, 백라이트가 `46` 을, OPI PSRAM 이
`35~37` 을 먹는다. `43·44` 는 네이티브 USB 라 커넥터에 나오지 않고, `1` 은 터치 INT,
`4` 는 배터리 전압 측정이다 — UART 로 돌릴 핀이 남지 않는다.

**슬레이브는 먼저 말을 걸 수 없어** 이쪽이 마스터로 `LINK_POLL_MS`(50ms)마다 읽어 온다.
한 덩이는 `[0]=글자 길이(0 = 줄 것 없음) · [1..]=글자` 다. 결과는 이쪽이 써 넣는데,
I2C 한 덩이에 들어가야 해서 `mode`·`online` 을 뺀 짧은 꼴로 보낸다(`linkSendResult`).
리더는 `uid` 로 자기 답인지 가리므로 그래도 알아본다.

선이 없으면 `requestFrom` 이 빈손으로 돌아올 뿐이다 — 50ms 마다 주소를 한 번 두드리는
것뿐이라 터치·화면이 쓰는 같은 버스에 부담이 없다. 선을 안 쓰려면 `config.h` 의
`LINK_I2C_ENABLE` 을 `0` 으로 둔다.

상단 상태 점은 **선이 살아 있으면 와이파이가 없어도 초록**이다 — 망이 죽어도 태깅은 되기 때문이다.

## 시리얼 명령 (115200)
| 글자 | 하는 일 |
|------|---------|
| `n` | 다음 사진 (터치와 같다) |
| `p` | 이전 사진 |
| `h` | 활성 화면으로 |
| `s` | 넘겨 볼 사진 목록 |
| `l` | SD `/talent` 파일 목록 |
| `c` | 서버 설정(밝기·출석모드) 지금 다시 받기 |
| `i` | 기기 ID·밝기·출석모드·출석 카드 · 선(리더)·ws client·와이파이 상태 |
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
| `nfcProject.ino` | 화면·와이파이·mDNS·WebSocket 서버 · 리더와 잇는 선(I2C 마스터) |
| `config.h` | 와이파이·mDNS·포트·선(I2C) 설정 (여기만 고치면 된다) |
| `useEnable.h` | 480×480 활성 그림 (대기 화면 첫 장) |
| `useDisable.h` | 쓰지 않음 (포함하지 않는다) |
| `connected.h` | 50×35 연결 아이콘 (투명색 0xF81F) |
