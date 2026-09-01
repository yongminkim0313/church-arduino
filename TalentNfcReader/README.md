# TalentNfcReader — ESP32-S3 NFC 키링 리더 (달란트)

NTAG-213 키링을 PN532(I2C)로 읽어 2.8" ILI9341 TFT 에 잔액을 보여준다.
평소에는 딥슬립으로 대기하다가 터치로 깨어나고, 무입력 15초 뒤 다시 잠든다.

## 조작

| 동작 | 결과 |
|------|------|
| 터치(GPIO4) — 잠든 상태 | 깨어남 · 전원 ON 음 |
| 터치 — 깨어 있는 상태 | 적립 ↔ 소모 모드 전환 |
| 키링 태깅 | 현재 모드대로 적립/소모 후 결과 표시 |
| 15초 무입력 | 전원 OFF 음 → WiFi 끄고 백라이트 끄고 딥슬립 |

깨어날 때 WiFi 에 붙는다(최대 8초 대기). 접속 실패해도 화면은 뜨고, 태깅할 때
"처리하지 못했습니다 / 네트워크 없음" 으로 알린다.

## 배선

CLAUDE.md 핀맵 그대로다. 전원은 TFT·PN532·부저 모두 3.3V, 공통 GND.

| 기능 | GPIO |
|------|------|
| TFT CS / DC / RST | 10 / 8 / 9 |
| TFT MOSI / SCK / MISO | 11 / 12 / 13 |
| TFT 백라이트 (PWM) | 14 |
| PN532 SDA / SCL | 16 / 17 |
| 터치 전원버튼 | 4 (T4) |
| 부저 | 5 |

PN532 의 **IRQ·RESET 은 연결하지 않는다.** 스케치에서 `Adafruit_PN532(-1, -1, &Wire)`
로 만들어 I2C RDY 바이트로 준비 여부를 확인한다. 모듈의 모드 스위치는 **I2C** 로 둘 것.

## 빌드

```bash
./build.sh              # 컴파일
./build.sh --upload     # 컴파일 + 업로드
./build.sh --upload /dev/cu.usbmodem2101   # 포트 지정
```

`arduino-cli compile` 을 그냥 쓰면 안 된다. TFT_eSPI 는 설정을 라이브러리 폴더
(`User_Setup_Select.h`)에서 읽는데, 이 PC 에는 TTGO T-Display 용 Setup25 가 활성화돼
있어 그대로 쓰면 서로 덮어쓴다. `build.sh` 는 라이브러리를 건드리지 않고
`-DUSER_SETUP_LOADED` 와 핀 정의를 **이 스케치에만** 주입한다.

필요 라이브러리:

```bash
arduino-cli lib install "TFT_eSPI" "Adafruit PN532"
```

## 한글 표시

TFT_eSPI 내장 폰트는 ASCII 전용이라 한글이 깨진다. 화면에 쓰는 낱말만 골라
스무스폰트로 구웠다 — `FontKR22.h` (129자, 35KB).

글자를 늘리려면 `--chars` 에 추가해 다시 생성한다:

```bash
python3 ../tools/ttf2vlw.py ~/Library/Fonts/NanumGothicBold.ttf --size 22 --var FontKR22 \
  --chars "달란트적립소모키링을대주세요터치모드전환처리할수없습니다잔액이랍자NFC모듈없음+" \
  -o FontKR22.h
```

**서브셋에 없는 글자는 그냥 안 그려진다.** 문구를 바꿀 때는 폰트도 같이 다시 만들 것.
증감 숫자는 크게 보여야 해서 내장 폰트(6번)를 쓰는데, 스무스폰트가 올라가 있으면
폰트 번호가 무시되므로 그 구간만 `krFont(false)` 로 잠깐 내렸다가 올린다.

## 터치 임계값 보정 (처음 한 번 필요)

보드·패드마다 값이 다르다. 스케치 상단의 `CALIBRATE_TOUCH` 를 1 로 바꿔 굽고,
시리얼에 찍히는 값을 손 뗐을 때와 댔을 때 각각 확인한 뒤 중간값을 `TOUCH_THRESH` 에 넣는다.

```cpp
#define CALIBRATE_TOUCH 1
```

ESP32-S3 는 **터치하면 값이 커진다**(구형 ESP32 와 반대). 깨우기도 "임계값 이상"으로 건다.

## 소리

| 이벤트 | 패턴 |
|--------|------|
| 전원 ON | 1000 → 1500Hz (상승) |
| 전원 OFF | 1500 → 1000Hz (하강) |
| 적립 | 1200 → 1600 → 2000Hz (3연음, 위로) |
| 소모 | 1600 → 900Hz (아래로) |
| 실패 | 300Hz 300ms |
| 모드 전환 | 1400Hz 짧게 |

`beep()` 이 재생 시간만큼 붙잡아 두므로 딥슬립 직전 전원 OFF 음도 잘리지 않는다.

## 달란트 저장 — 서버 DB

잔액은 **jdServer(jesusdream.kr)의 MongoDB** 가 단일 출처다. 리더는 저장하지 않고
매번 서버에 묻는다. 리더를 여러 대 놓아도 잔액이 하나로 모이고, 적립·소모 내역이
서버에 남아 "왜 이 잔액인지" 설명할 수 있다.

| 메서드 | 경로 | 용도 |
|--------|------|------|
| POST | `/api/talent/earn` | 적립 (기기 키 필요) |
| POST | `/api/talent/spend` | 소모 (잔액 부족 시 409) |
| GET | `/api/talent/:uid` | 잔액 조회 (공개) |
| GET | `/api/talent` | 전체 목록 (관리자) |
| GET | `/api/talent/logs?uid=&limit=` | 증감 내역 (관리자) |

요청/응답:

```
POST /api/talent/earn
  헤더 : x-talent-key: <TALENT_DEVICE_KEY>
  본문 : { "uid": "04AABBCCDD", "amount": 1, "device": "nfc-reader-1" }
  응답 : { "success": true, "uid": "...", "balance": 7, "delta": 1 }
```

**기기 키**: 달란트는 포인트라 인증 없이 열면 누구나 적립할 수 있다. 그래서 쓰기는
`x-talent-key` 헤더를 서버의 `TALENT_DEVICE_KEY`(.env)와 대조한다. 기기 쪽 값은
`~/Documents/Arduino/libraries/ChurchSecrets/ChurchSecrets.h` 에 있다. 두 값이 같아야 한다.

**네트워크가 끊기면 처리하지 않는다.** 화면에 사유를 띄우고 실패음을 낸다.
잔액은 건드리지 않으므로 나중에 다시 태깅하면 된다. (오프라인 대기열은 넣지 않았다)

## 아직 확인하지 못한 것

실물 보드가 없어 **컴파일과 폰트 검증까지만** 했다. 다음은 기기에서 봐야 한다.

- 터치 임계값 (위 보정 절차 필요)
- PN532 인식 — 못 찾으면 화면에 "NFC 모듈 없음" 이 뜬다
- 딥슬립 복귀 후 TFT 재초기화
- 화면 레이아웃 (240x320 세로 기준으로 좌표를 잡았다)
- WiFi 접속 시간 — 깨어날 때마다 붙으므로 체감 지연이 얼마나 되는지
