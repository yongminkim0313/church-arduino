# nfcProjectClient

nfcProject 원형 디스플레이(WebSocket 서버, `ws://nfc-display.local:81/`)에 **NFC 정보를 보내는 클라이언트**.

실제 NFC 리더는 카드에서 **UID** 만 얻으므로, UID 만 주면 yvServer 달란트 API에서 이름·잔여 포인트를 조회해 채워 보낸다. 이름·포인트를 직접 주면 조회 없이 그대로 보낸다.

기기가 받으면 그 UID 의 사진(microSD `/talent/<UID>.565`)을 전체화면으로 띄우고 상단에 이름, 하단에 잔여 포인트를 겹쳐 그린다(몇 초 뒤 대기 화면 복귀).

## 설치

```bash
npm install          # ws 설치 (처음 한 번)
```

## 사용

```bash
node nfcClient.js 조은재                 # UID(이름키)로 서버 조회 후 전송
node nfcClient.js 04A1B2C3               # 카드 hex UID 도 동일
node nfcClient.js 04A1B2C3 홍길동 1200   # 이름·포인트 직접 지정(조회 안 함)
node nfcClient.js enable|disable|toggle  # 대기 화면 전환
node nfcClient.js                        # 대화형 — 한 줄에 하나씩 입력
```

대화형 예: `조은재` ↵ / `04A1B2C3 홍길동 1200` ↵ / `toggle` ↵ (Ctrl-C 종료)

## 설정 (환경변수)

| 변수 | 기본값 | 설명 |
|------|--------|------|
| `DISPLAY_WS` | `ws://nfc-display.local:81/` | 기기 WebSocket 주소. mDNS 안 되면 IP 로: `ws://192.168.0.30:81/` |
| `TALENT_SERVER` | `https://youthvision.co.kr` | 달란트 서버(이름·잔액 조회) |
| `TALENT_DEVICE_KEY` | (nfcProject config.h 와 동일) | `x-talent-key` 헤더 |

```bash
DISPLAY_WS=ws://192.168.0.30:81/ node nfcClient.js 조은재
```

## 보내는 형식 (기기 규약)

- NFC 태그: `{"uid":"조은재","name":"조은재","points":16}`
- 상태 전환: `{"state":"enable"}` / `disable` / `toggle`

기기는 붙자마자, 그리고 상태가 바뀔 때마다 `{"mode":"idle"|"nfc",...}` 를 되돌려 준다(로그에 `[기기]` 로 찍힘).

---

# ESP32-C3 펌웨어 (`nfcProjectClient.ino`)

C3 에는 화면이 없다 — **PN532 와 부저만** 단다. 카드를 대면 UID 를 읽어
디스플레이(`nfc-display.local:81`)에 **`{"uid":"…"}` 만** 보낸다.

**이 리더는 서버와 말하지 않는다.** 이름·잔액 조회도, 출석 지급도 디스플레이(`nfcProject`)가 한다 —
서버와 이야기하는 쪽을 하나로 모아야 한 태깅이 두 번 처리되지 않는다.
그래서 `config.h` 에 `TALENT_SERVER`·`TALENT_DEVICE_KEY` 가 없다.

결과는 디스플레이가 `{"mode":"nfc","name":…,"points":…,"known":…,"note":…}` 로 돌려주고,
리더는 그것으로 소리를 고른다. `ACK_WAIT_MS`(4초) 안에 답이 없으면 실패음을 낸다 —
소리가 없으면 자원봉사자는 못 읽은 줄 알고 카드를 계속 댄다.

## 배선 (PN532 **SPI** 모드)

![배선도](wiring-spi.svg)

| 신호 | ESP32-C3 | PN532 |
|------|----------|-------|
| 전원 | 3V3 | VCC (5V 금지) |
| SCK | GPIO4 | SCK |
| MISO | GPIO5 | MISO |
| MOSI | GPIO6 | MOSI |
| SS | GPIO7 | SS (NSS) |
| 접지 | GND | GND |
| 부저 | GPIO10 / GND | 부저 (+) / (−) |

**모듈 딥스위치를 SPI 로 놓아야 한다** — Elechouse V3 는 보통 `1:OFF 2:ON` 이지만
보드 뒷면 표로 확인하라. I2C 로 두면 MISO 가 조용해 아무것도 안 읽힌다.

**부저가 GPIO7 → GPIO10 으로 옮겨 왔다.** SPI 의 SS 가 GPIO7 을 쓰기 때문이다.

PN532 VCC·GND 2cm 안에 **100µF 전해 + 0.1µF 세라믹** 병렬. GPIO8·9(스트래핑 핀)는 쓰지 않는다.
핀·와이파이·서버는 `config.h` 에서 바꾼다. IRQ·RSTPDN 은 쓰지 않고 폴링으로 돈다.
배선 점검은 시리얼 `lines`(선 상태)·`raw`(MISO 에 뭐가 오나).

### 왜 I2C 가 아니라 SPI 인가
2026-09-24 실기에서 이 모듈의 **I2C 쪽(SCL)이 고장 난 것**을 확인했다. SCL 이 GND 로
물려 버스가 통째로 죽었고, 재납땜 뒤에도 `0x24` 가 안 잡혔다. 같은 모듈이 SPI 로는
`v1.6` 으로 멀쩡히 응답하고 145초 소크에서 **감시 실패 0 · 재초기화 0** 이었다.
처음 의심했던 전원(3V3 레귤레이터·USB 케이블)은 결백하다. 가려내는 과정은
[`pn532Test`](../pn532Test/)(I2C)와 [`pn532SpiTest`](../pn532SpiTest/)(SPI)에 남아 있다.

## 빌드·업로드
```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
  --library ~/Documents/Arduino/libraries/Adafruit_PN532 --library ~/Documents/Arduino/libraries/Adafruit_BusIO \
  nfcProjectClient
```

## 시리얼 명령 (115200, 한 줄씩)
| 입력 | 하는 일 |
|------|---------|
| `04CE1B53D12A81` | 카드를 댄 것처럼 처리 (카드 없이 시험) |
| `next` · `prev` · `enable` · `toggle` | 디스플레이 화면 넘기기 |
| `status` | 와이파이·디스플레이·PN532 상태 |
| `scan` | I2C 훑기 (PN532 = `0x24`) |
| `beep` | 부저 소리 세 가지 |

## 부저 소리
| 소리 | 뜻 |
|------|----|
| 삑 (높게 한 번) | 디스플레이가 아는 키링으로 처리했다(출석모드면 지급까지 됐다) |
| 삐-삐 (낮게 두 번) | 서버에 없는 카드 · 디스플레이에 못 보냄 · 4초 안에 답이 없음 |
| 삐리 (올라가는 두 음) | 켜져서 PN532 준비됨 |

수동(패시브) 부저가 기본이다. 능동(액티브) 부저면 `config.h` 의 `BUZZER_ACTIVE 1`.

같은 카드를 대고 있는 동안은 3초(`SAME_TAG_COOLDOWN_MS`)에 한 번만 보낸다. 4바이트 UID(휴대폰·교통카드)는 넘긴다.
