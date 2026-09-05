# ChurchLogoOnly

TTGO T-Display(240×135)에 **교회 로고 한 장만** 띄우는 최소 스케치. 애니메이션·센서·통신 없음.

## 빌드 & 업로드

```bash
arduino-cli compile --upload ChurchLogoOnly
```

포트/보드는 `sketch.yaml` 에 있다. TFT_eSPI 는 `Setup25_TTGO_T_Display.h` 활성화가 필요하다
— [../docs/TFT_eSPI_setup.md](../docs/TFT_eSPI_setup.md)

## logo.h 다시 만들기

`logo.h` 는 **240×135 전체 화면**에 맞춰 생성돼 있다. 원본의 흰 여백을 잘라내고
비율을 유지한 채 화면을 꽉 채운 뒤, 언샤프 마스크 80% + Floyd-Steinberg 디더링을 적용했다.

```bash
python3 tools/img2rgb565_dither.py ChurchLogoDisplay/app_icon.png \
  --width 240 --height 135 --crop-content --fit contain --margin 4 --sharpen 80 \
  --var logo_data -o ChurchLogoOnly/logo.h --preview /tmp/logo_preview.png
```

`--preview` 로 나온 PNG 가 실제 화면에 뜰 그림과 같다. 플래시하기 전에 확인할 것.

| 옵션 | 뜻 |
|------|-----|
| `--crop-content` | 배경색과 같은 바깥 여백을 먼저 잘라낸다 (로고가 커진다) |
| `--fit contain` | 비율 유지하며 화면 안에 맞춤 (`cover` = 꽉 채우고 잘라냄, `stretch` = 늘림) |
| `--margin 4` | 가장자리 여백 px |
| `--sharpen 80` | 축소하며 뭉개진 획의 대비 복원 (0=끔, 60~120 권장) |
| `--bg '#FFFFFF'` | 투명/여백 채움색 |
| `--no-dither` | 디더링 없이 단순 양자화 |

웹에서 직접 다듬고 싶으면 jdServer 의 `/pixelArt` 페이지에서 픽셀 단위로 수정한 뒤
`C 배열 (uint16_t)` 형식으로 복사해 `logo.h` 의 배열 부분에 붙여넣어도 된다.

## 작은 화면에 로고를 넣을 때

- **글자를 이미지로 줄여 넣지 말 것.** 240×135 안에서 로고 카드를 100×100 정도로 작게 넣으면
  한글 글자 높이가 8px 밖에 안 나와서 획이 뭉개진다. 화면을 꽉 채우면 글자가 3배 커진다.
- **바이트 순서**: `tft.pushImage()` 로 직접 그릴 땐 `tft.setSwapBytes(true)` 가 **필요하다**.
  ESP32 메모리가 리틀엔디언이라 `0xF800`(빨강)이 `0x00F8` 로 나가기 때문이다.
  `ChurchLogoDisplay` 처럼 `TFT_eSprite` 를 거치는 코드는 스프라이트가 내부 버퍼에 이미 뒤집어
  저장하므로 이 설정이 없어야 정상이다 — **두 경로의 동작이 반대이니 복사해 쓸 때 주의.**
  이 스케치는 `.ino` 상단의 `#define SWAP_BYTES true` 로 켜 두었다.
- 색 전체가 음화처럼 반전되면 `tft.invertDisplay(1)` 을 추가한다.
- 배선·오프셋·바이트순서 확인은 `/pixelArt` 페이지의 **테스트 패턴**(컬러 바, 테두리+모서리 마커)이 빠르다.
