# ChurchDisplayRx — WiFi 실시간 수신 디스플레이

TTGO T-Display(ESP32)가 **WebSocket 서버를 열고**, 센서 보드가 밀어 넣는 JSON 을
받는 즉시 화면을 갱신한다. 한글 전체를 표시한다.

```
[ESP32-C3 + SHT41]  ──WebSocket──▶  [TTGO T-Display]
   SHT41MonitorC3        직접 접속        ChurchDisplayRx
        │                              ws://churchdisplay.local:81
        └──HTTPS POST──▶ jesusdream.kr
```

센서 보드가 디스플레이에 **직접** 붙으므로 중간 서버가 필요 없다. 디스플레이는
mDNS 로 `churchdisplay.local` 을 광고하고, 센서 보드는 그 이름으로 찾아온다.
IP 가 바뀌어도 되고, 조회에 실패하면 `DISPLAY_IP_FALLBACK` 으로 붙는다.

## 두 가지 수신 경로

| | 역할 | 기본값 | 용도 |
|---|---|---|---|
| `USE_WS_SERVER` | 서버 — 센서가 붙어 온다 | **1 (켜짐)** | ESP32-C3 직결 |
| `USE_WS_CLIENT` | 클라이언트 — 바깥 서버에 붙는다 | 0 (꺼짐) | 클라우드 푸시 |

둘 다 켤 수 있지만, RAM 이 빠듯하므로(한글 폰트가 135KB) 필요할 때만 켤 것.

## 왜 WebSocket 인가

| 방식 | 지연 | 부하 | 비고 |
|------|------|------|------|
| HTTP 폴링 | 주기만큼(예: 10초) | 주기마다 연결 재수립 | 구현은 제일 쉬움 |
| **WebSocket** | **즉시(<100ms)** | **연결 1개 유지** | 양방향, 재연결 내장 |
| MQTT | 즉시 | 가벼움 | 브로커 별도 필요 |
| ESP32가 서버 | 즉시 | — | 공유기 포트포워딩/고정IP 필요 |

기기가 공유기 안(NAT)에 있어도 **기기 쪽에서 나가는 연결**이라 포트포워딩이 필요 없다.

## 프로토콜 — 요청/응답

갱신 주기는 **디스플레이가 정한다.** 센서가 알아서 밀어 넣지 않는다.

```
디스플레이 ──{"cmd":"read"}──▶ 센서        5초마다 (REQUEST_MS), 접속 직후 1회
디스플레이 ◀──── 측정값 ──────  센서        요청받은 그 시점에 측정해서 응답
```

**요청** (디스플레이 → 센서)

```json
{"cmd":"read"}
```

**응답** (센서 → 디스플레이)

```json
{"title":"외부 센서","temp":24.6,"humi":51.2,"msg":"정상 측정"}
```

오류일 때는 값 없이 메시지만 보낸다:

```json
{"msg":"센서 오류 270"}
```

필드는 전부 선택이다. 들어온 필드만 갱신되므로 `{"msg":"hello"}` 처럼 일부만 보내도 된다.

이렇게 하면 화면 값이 항상 **5초 이내의 실측치**임이 보장된다. 센서가 자기 주기로
밀어 넣는 방식이면 마지막 값이 얼마나 묵은 것인지 알 수 없다.

화면 우측 하단에 `3초 전 42/43` 처럼 **응답/요청 횟수**가 나오므로 놓친 요청이
있는지 바로 보인다. 시리얼에는 왕복 시간도 찍힌다.

## 센서 보드와 함께 쓰기 (기본 구성)

1. 디스플레이를 굽는다 — 보드/파티션/포트는 `sketch.yaml` 에 있어 옵션이 필요 없다
   (포트가 다르면 `sketch.yaml` 의 `default_port` 를 고칠 것)

```bash
arduino-cli compile --upload /Users/kimyongmin/workspace/arduino/ChurchDisplayRx
```

2. 센서 보드(ESP32-C3)를 굽는다

```bash
arduino-cli compile --upload -p /dev/cu.usbmodem101 /Users/kimyongmin/workspace/arduino/SHT41MonitorC3
```

센서 보드도 `sketch.yaml` 에 Huge APP 이 들어 있어 옵션이 필요 없다. 기본 파티션으로는
94% 까지 차지만 huge_app 으로 39% 가 된다 — C3 Super Mini 는 4MB 플래시이고
huge_app 이 정확히 4MB 레이아웃이라 그대로 맞는다.

3. 디스플레이 시리얼에 `[서버] 센서 접속` 이 뜨고 상단에 **"센서 1대 연결"** 이 나오면 된다.

## 보드 없이 화면만 시험하기

맥에서 센서 보드 흉내를 낸다 (`server/ws-push.js`):

```bash
node /Users/kimyongmin/workspace/arduino/ChurchDisplayRx/server/ws-push.js
```

터미널에 아무 문장이나 입력하고 Enter 를 치면 화면 하단 메시지가 즉시 바뀐다.
mDNS 가 안 되면 IP 를 직접 준다: `node ws-push.js 192.168.0.50`

`USE_WS_CLIENT` 를 켜서 바깥 서버 푸시를 받는 구성을 시험하려면 `server/ws-server.js` 를 쓴다.

```bash
arduino-cli compile --upload /Users/kimyongmin/workspace/arduino/ChurchDisplayRx
```

4. 시리얼 모니터로 수신 로그 확인

```bash
arduino-cli monitor -p /dev/cu.usbserial-5B34014705 -c baudrate=115200
```

서버를 띄운 터미널에 아무 문장이나 입력하고 Enter 를 치면 화면 하단 메시지가 즉시 바뀐다.
2초마다 오는 더미 온습도도 함께 갱신된다.

## 운영 서버(jesusdream.kr)에 붙이려면

스케치 상단을 이렇게 바꾼다:

```cpp
const char* WS_HOST = "jesusdream.kr";
const uint16_t WS_PORT = 443;
const char* WS_PATH = "/ws";
#define USE_TLS 1          // beginSSL() 사용
```

서버(jdServer, Express)에는 `ws` 로 업그레이드 핸들러를 붙이면 된다:

```js
const { WebSocketServer } = require('ws');
const wss = new WebSocketServer({ server, path: '/ws' });
// 센서 수신 라우트에서 wss.clients 로 broadcast
```

`server/ws-server.js` 의 `broadcast()` 를 그대로 옮겨 쓰면 된다.

## 화면 구성

```
┌────────────────────────────────┐
│ 실시간 연결          -52 dBm   │  상태바: WS 연결 / WiFi 세기
│ 본당 예배실                    │  title (서버가 보낸 한글 그대로)
│ 온도          습도             │
│ 24.6          51.2             │  38px 숫자 폰트
│ 정상 작동 중    3초 전   #42   │  msg / 마지막 수신 경과·수신 횟수
└────────────────────────────────┘
```

## 한글 전체 표시

TFT_eSPI 내장 폰트는 ASCII 전용이라 한글이 깨진다. **스무스폰트(VLW)** 로 바꾸고
`tools/ttf2vlw.py` 로 나눔고딕에서 **한글 음절 11,172자 전체**를 구워 넣었다.
서버가 어떤 한글을 보내도 그려진다 — 서브셋 걱정이 없다.

| 파일 | 내용 | 크기 |
|------|------|------|
| `FontKRFull12.h` | 나눔고딕볼드 12px — 한글 11,172자 + ASCII (총 11,267 글리프) | 1.82MB |
| `FontNum38.h` | 나눔고딕볼드 38px — 숫자 전용 15자 | 9KB |

재생성:

```bash
python3 tools/ttf2vlw.py ~/Library/Fonts/NanumGothicBold.ttf --size 12 --ks1001 --preset ui --var FontKRFull12 -o ChurchDisplayRx/FontKRFull12.h
```

### 왜 12px 인가

한글 전체를 넣으면 크기가 급격히 커진다. Huge APP 은 3MB 이고 코드가 1.17MB 를
쓰므로 폰트에 쓸 수 있는 건 약 1.8MB 뿐이다.

| 폰트 크기 | 폰트 데이터 | 코드 포함 총합 | 3MB 안에 들어가나 |
|---|---|---|---|
| 16px | 2.91MB | 4.08MB | 아니오 |
| 14px | 2.40MB | 3.57MB | 아니오 |
| **12px** | **1.82MB** | **3.01MB (95%)** | **예** |

14px 이상이 필요하면 폰트를 앱에 굽지 말고 파일시스템에 올려야 한다.
파티션을 `app3M_fat9M_16MB`(3MB APP + 9.9MB FATFS)로 바꾸고 `.vlw` 를 업로드한 뒤
`tft.loadFont("full16", FFat)` 로 읽는다. 보드가 16MB 플래시라 공간은 넉넉하다.

```bash
python3 tools/ttf2vlw.py ~/Library/Fonts/NanumGothicBold.ttf --size 16 --ks1001 -o full16.vlw
```

### 파티션

한글 전체 폰트 때문에 기본 파티션(1.31MB APP)으로는 빌드가 안 들어간다.
`sketch.yaml` 에 **Huge APP(3MB)** 을 박아뒀으므로 arduino-cli 는 알아서 맞춘다.
Arduino IDE 를 쓸 때는 Tools → Partition Scheme → **Huge APP (3MB No OTA/1MB SPIFFS)** 로 직접 바꿔야 한다.

### RAM 을 아끼려고 바꾼 구조

TFT_eSPI 는 글리프 메트릭을 **RAM 에 올린다**. 11,267자 × 12B ≈ **135KB** 로,
ESP32 DRAM 320KB 중 상당량을 차지한다. 그래서 이전 버전과 달리:

- **전체화면 스프라이트(64KB)를 쓰지 않는다.** 화면에 직접 그리고, 깜빡임은
  `setTextPadding()` 으로 글자 영역만 덮어써서 없앤다.
- 큰 숫자는 **작은 스프라이트(112×44, 약 10KB)** 에 38px 폰트를 상주시켜 쓴다.
  큰 폰트를 매 프레임 load/unload 하면 메트릭을 다시 읽느라 화면이 멈춘다.
- 폰트는 **부팅 때 한 번만** 올리고 계속 유지한다. WiFi 연결 *전에* 올려서
  힙이 덜 조각난 상태의 큰 블록을 확보한다.

`TFT_eSPI::loadFont()` 는 **malloc 실패를 검사하지 않아** 힙이 모자라면 널 포인터에
써서 부팅 루프에 빠진다. 그래서 스케치에서 적재 전에 힙을 먼저 확인하고,
모자라면 한글 폰트를 건너뛰고 ASCII 내장 폰트로 동작한다.

부팅 시 시리얼에 힙 사용량이 찍히므로 실제 여유를 확인할 수 있다:

```
폰트 적재 전  여유 힙=xxxxxx  최대블록=xxxxxx
폰트 적재 완료 여유 힙=xxxxxx
숫자 스프라이트: 생성, 여유 힙=xxxxxx
무선 연결됨  ip=192.168.0.x  여유 힙=xxxxxx
```

## 메모

- 스무스폰트가 로드된 동안에는 **내장 폰트 번호가 무시된다**. 그래서 큰 숫자는
  별도 스프라이트(자체 폰트 상태를 가짐)에 그린다.

- 스무스폰트가 로드된 동안에는 **내장 폰트 번호가 무시된다**. 그래서 `render()` 에서
  한글 구간과 숫자 구간을 `loadFont()` / `unloadFont()` 로 갈아 끼운다.
- `loop()` 안에 `delay()` 를 넣지 말 것. `webSocket.loop()` 가 논블로킹으로 계속 돌아야 한다.
- 재연결은 `setReconnectInterval(3000)` 으로 자동. 서버를 껐다 켜도 3초 안에 다시 붙는다.
- `enableHeartbeat(15000, 3000, 2)` — 죽은 연결(공유기가 조용히 끊는 경우)을 ping/pong 으로 감지한다.
- 빌드 결과: 플래시 3,011,511 B (95% / Huge APP 3MB) / 전역 RAM 48,632 B (14%)
- JSON 문자열은 UTF-8 경계를 지켜 자른다(`copyUtf8`). 멀티바이트 한 글자를 반토막 내면 글리프가 깨진다.

## 인증정보

WiFi 비밀번호는 스케치에 넣지 않고 저장소 **밖** 공용 헤더에서 가져온다:

```
~/Documents/Arduino/libraries/ChurchSecrets/ChurchSecrets.h
```

`WIFI_SSID`, `WIFI_PASSWORD`, `SENSOR_POST_URL`, `WS_HOST_LOCAL` 을 정의하며
`#include <ChurchSecrets.h>` 한 줄로 모든 스케치가 공유한다. 저장소에는 템플릿인
[../secrets.example.h](../secrets.example.h) 만 남는다.

## 라이브러리

```bash
arduino-cli lib install "WebSockets"
arduino-cli lib install "ArduinoJson"
```

TFT_eSPI 는 `Setup25_TTGO_T_Display.h` 활성화 필요 — [../docs/TFT_eSPI_setup.md](../docs/TFT_eSPI_setup.md)
