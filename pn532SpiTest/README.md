# pn532SpiTest — PN532 SPI 시험 (모듈이 살아 있는지 가리기)

`pn532Test`(I2C)의 짝이다. I2C 쪽에서 SCL 이 GND 로 물려 버스가 통째로 죽었을 때,
**선도 핀도 프로토콜도 다른 SPI** 로 걸어 보면 모듈 자체의 생사가 갈린다.

| SPI 결과 | 뜻 |
|---|---|
| `[NFC] PN532 준비됨 v1.6` | **모듈은 멀쩡하다.** I2C 핀/납땜만 고장 난 것 |
| `raw` 가 전부 `FF` | MISO 가 떠 있다 — 선·모드 스위치(SPI)·모듈 전원 |
| `raw` 가 전부 `00` | MISO 가 LOW 로 물렸다 — 단락이거나 모듈이 죽었다 |
| `raw` 는 섞여 오는데 `ver` 실패 | 배선은 닿았고 프로토콜이 어긋났다 → `USE_SOFT_SPI 1` 로 빌드 |

와이파이·블루투스 끄고 CPU 80MHz 로 도는 것은 `pn532Test` 와 같다.

## 배선

![배선도](../nfcProjectClient/wiring-spi.svg)

```
3V3   → VCC          GND   → GND
GPIO4 → SCK          GPIO5 → MISO
GPIO6 → MOSI         GPIO7 → SS(NSS)
GPIO10 → RSTPDN      (선택 — 안 쓰면 PIN_RSTPDN 을 -1 로)
```

I2C 로 쓰던 **GPIO4·5 는 그대로 두고 두 가닥만 더 꽂으면 된다.**
다만 모듈 쪽은 **실크스크린 이름(SCK·MISO·MOSI·SS)에 맞춰** 꽂아야 한다 —
보드마다 이 이름들이 어느 헤더에 있는지가 다르니 SDA/SCL 자리에 그대로 꽂으면 안 된다.

## 모드 스위치

Elechouse V3(빨간 보드)는 딥스위치 두 개로 고른다. 보통 이렇지만
**보드 뒷면 인쇄된 표로 반드시 확인하라.**

| 모드 | 1 | 2 |
|---|---|---|
| HSU | OFF | OFF |
| I2C | ON | OFF |
| **SPI** | **OFF** | **ON** |

스위치가 SPI 가 아니면 배선이 맞아도 MISO 는 조용하다.

## 빌드·업로드

```bash
cd ~/workspace/arduino
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
  --libraries ~/Documents/Arduino/libraries pn532SpiTest
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app \
  -p /dev/cu.usbmodem* pn532SpiTest
```

## 시리얼 명령 (115200)

| 입력 | 하는 일 |
|---|---|
| `help` | 명령 목록 |
| `stats` | 읽기/실패/모듈리셋 통계, 가동 시간, 힙 |
| `ver` | PN532 버전 한 번 읽기 (살아 있나) |
| `raw [n]` | CS 내리고 바이트를 그대로 주고받아 **MISO 에 뭐가 오나** (기본 8) |
| `lines` | SCK/MISO/MOSI/SS 선 상태 |
| `probe [초]` | 그 시간 동안 쉬지 않고 폴링 (기본 8초) |
| `rty <n>` | 카드찾기 재시도 횟수 (기본 `0x10`) |
| `cpu <mhz>` | CPU 클럭 (`160`/`80` — 80 아래는 USB 가 끊겨 거부) |
| `rst` | RSTPDN 내렸다 올리기 (`PIN_RSTPDN` 잡아 뒀을 때만) |
| `reinit` | PN532 를 처음부터 다시 잡기 |

`raw` 는 라이브러리를 거치지 않고 **부팅 때 자동으로 한 번** 돌고, 언제든 다시 부를 수 있다.
PN532 를 못 잡아도 MISO 선의 생사는 이걸로 알 수 있다 — SPI 진단의 핵심이다.

## I2C 판과 다른 점

- 라이브러리가 SPI 를 **1MHz · LSB-first · MODE0 고정**으로 잡는다. 클럭을 못 바꾸니
  `i2c <hz>` 같은 명령이 없다. 대신 선이 길거나 하드웨어 SPI 가 수상하면
  `USE_SOFT_SPI` 를 `1` 로 바꿔 비트뱅잉으로 빌드한다.
- SPI 생성자는 `_reset` 을 -1 로 둬서 **라이브러리가 하드 리셋을 안 한다.**
  그래서 `PIN_RSTPDN` 을 직접 잡고 `nfcBegin()` 마다 펄스를 준다.
- SPI 선에는 풀업이 없으므로 `lines` 판독은 I2C 때만큼 결정적이지 않다.
  단락(둘 다 0)만 잡아 준다. 연결 판정은 `raw` 로 한다.
