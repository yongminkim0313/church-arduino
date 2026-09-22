# pn532Test — PN532 단독 시험 (전원 문제 가려내기)

`nfcProjectClient` 에서 **와이파이·mDNS·WebSocket·HTTP(TLS)·부저를 모두 뺀** 스케치.
남은 건 I2C + PN532 폴링뿐이다. 소비 전류를 최소로 떨어뜨린 상태에서 PN532 가 버티는지 본다.

- 와이파이·블루투스 `off` (`WiFi.mode(WIFI_OFF)` + `esp_wifi_deinit` + `btStop`)
- CPU 80MHz (USB CDC 가 도는 최저)
- I2C 100kHz (400k 보다 선·풀업에 너그럽다)
- 부저 꺼짐 (`PIN_BUZZER -1` — 부저 전류까지 보고 싶으면 `7` 로 바꿔 다시 빌드)

배선은 본 프로젝트와 같다: `3V3→VCC · GPIO4→SDA · GPIO5→SCL · GPIO6→IRQ · GND→GND`.

## 빌드·업로드

```bash
cd ~/workspace/arduino
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
  --libraries ~/Documents/Arduino/libraries pn532Test
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
  -p /dev/cu.usbmodem* pn532Test
arduino-cli monitor -p /dev/cu.usbmodem* -c baudrate=115200
```

## 판정법

| 보이는 것 | 뜻 |
|---|---|
| 켤 때 `[리셋] *** Brownout(전압 강하) ***` | **전원이 범인.** 3V3 이 임계 아래로 꺼졌다 |
| `[감시] PN532 가 응답이 없다` 가 뜸 | 모듈이 혼자 죽었다 살아났다 → 전원/배선 |
| 카드를 **댈 때만** 죽음 | 태그 여자 전류 피크 → VCC·GND 옆 100µF + 0.1µF |
| 여기선 멀쩡, 본 스케치에선 죽음 | **와이파이 송신 피크.** 전원부(레귤레이터·USB 케이블·선 굵기)를 손봐야 한다 |
| 여기서도 안 읽힘 | 전원이 아니라 배선·모듈·카드 문제 → `scan` / `lines` |

30초마다 통계가 자동으로 찍힌다: 폴링·읽음 횟수, 감시 실패 횟수, 재초기화 횟수.
**감시 실패 0 으로 몇 분 버티면 PN532 쪽은 결백하다.**

## 시리얼 명령 (115200)

| 입력 | 하는 일 |
|---|---|
| `help` | 명령 목록 |
| `stats` | 읽기/실패/모듈리셋 통계, 가동 시간, 힙 |
| `ver` | PN532 버전 한 번 읽기 (살아 있나) |
| `scan` | I2C 훑기 (PN532 = `0x24`) |
| `lines` | SDA/SCL/IRQ 선이 살아 있나 |
| `probe [초]` | 그 시간 동안 쉬지 않고 폴링, 한 번 한 번의 걸린 시간까지 (기본 8초) |
| `rty <n>` | 카드찾기 재시도 횟수 (기본 `0x10`) |
| `i2c <hz>` | I2C 클럭 (`100000` / `400000`) |
| `cpu <mhz>` | CPU 클럭 (`160` / `80` — 80 아래는 USB 가 끊겨 거부) |
| `beep` | 부저 한 번 (켜 뒀을 때만) |
| `reinit` | PN532 를 처음부터 다시 잡기 |

`probe` 의 **느림** 카운트는 타임아웃(400ms)보다 오래 걸린 횟수다 —
PN532 가 클럭 스트레칭으로 SCL 을 붙잡고 있었다는 뜻이라 전원이 흔들릴 때 늘어난다.

## 전류를 더 줄여 보려면

`cpu 80` 은 기본이고, 그래도 죽으면 순서대로:
1. USB 케이블을 짧고 굵은 것으로 (데이터 전용 얇은 케이블이 흔한 범인)
2. PN532 VCC·GND 2cm 안에 100µF 전해 + 0.1µF 세라믹 **병렬**
3. PN532 를 C3 의 3V3 핀이 아니라 별도 3.3V 전원으로 (GND 공통)
4. `i2c 100000` 유지 + `rty 0x05` 로 낮춰 모듈이 버스를 짧게 잡게
