#!/bin/bash
# TalentNfcReader 빌드/업로드
#
# TFT_eSPI 는 설정을 라이브러리 폴더(User_Setup_Select.h)에서 읽는다. 그대로 두면
# 이 PC 의 TTGO T-Display 스케치들(Setup25)과 설정이 충돌한다.
# 그래서 라이브러리를 건드리지 않고 -D 플래그로 이 스케치에만 설정을 주입한다.
# (TFT_eSPI 는 USER_SETUP_LOADED 가 정의돼 있으면 자체 설정 파일을 읽지 않는다)
#
#   ./build.sh                     컴파일만
#   ./build.sh --upload            컴파일 + 업로드 (포트는 아래 PORT 또는 인자로)
#   UART_LOG=1 ./build.sh --upload 로그를 UART 브리지 포트로 뺀다 (아래 CDCOnBoot 참고)
set -e
cd "$(dirname "$0")/.."

PORT="${2:-/dev/cu.usbmodem101}"
# 파티션표: 스케치 폴더의 partitions.csv (앱 4MB + LittleFS 11.875MB)
#
# 예전에는 huge_app 이었다. 한글 2350자 폰트(473KB) 때문에 기본 파티션(1.31MB)으로는
# 여유가 없어서였는데, huge_app 은 16MB 플래시를 쓰면서도 4MB 만 잡는 표라
# LittleFS 가 896KB 뿐이었다. 서버에서 받는 그림(포인트·배경·메뉴 항목)이 늘면
# 이 896KB 가 먼저 찬다 — 배경 한 장이 99KB 다.
#
# 코어가 들고 있는 표 중에 마땅한 것이 없어(사유는 partitions.csv 머리말) 직접 잡는다.
# PartitionScheme=custom 이면 코어가 스케치 폴더의 partitions.csv 를 쓴다.
# CDCOnBoot 는 Serial 이 어디로 나가는지를 정한다.
#   cdc(기본)  네이티브 USB 포트. 케이블 하나로 전원·업로드·콘솔이 다 된다.
#   default    UART0(GPIO43/44) → 보드의 UART 브리지 포트(CH9102 등).
#
# 이 보드에는 USB 포트가 둘이다. 브리지 포트에 꽂은 채로 스케치 로그를 보려면
# UART_LOG=1 로 굽는다 — 화면이나 PN532 를 아직 안 붙였을 때 특히 쓸모 있다.
# 브리지 포트는 굽기는 늘 되지만, cdc 로 구운 펌웨어의 Serial 출력은 그쪽으로 안 나온다.
CDC_ON_BOOT="${UART_LOG:+default}"; CDC_ON_BOOT="${CDC_ON_BOOT:-cdc}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=$CDC_ON_BOOT,PartitionScheme=custom"

# 핀은 배선 순서에 맞춰 고른 것이다 — 자세한 것은 .ino 의 핀 블록 주석을 볼 것.
# 요약하면 보드 왼쪽 헤더의 위에서 4~12번째 구멍이 TFT 헤더의 신호 순서와 그대로
# 맞아떨어지게 잡았다(GPIO4·5·6·7·15·16·17·18·8). 번호는 뒤죽박죽이지만 꽂는 자리는
# 나란하다 — 손으로 배선할 때 중요한 것은 번호가 아니라 구멍의 순서다.
#
# TOUCH_CS 는 디스플레이의 XPT2046 터치 패널이다 — 화면과 SPI 버스를 함께 쓰고
# CS 만 따로 받는다. 이 플래그가 있어야 tft.getTouch() 가 컴파일된다.
#
# USE_HSPI_PORT 는 없으면 첫 부팅에서 tft.init() 이 Guru Meditation 으로 죽는다.
# (StoreProhibited, EXCVADDR 0x10 — 리터럴 주소 0x10 에 s32i)
#
#   TFT_eSPI 2.5.43 의 S3 헤더는 기본값으로 `#define SPI_PORT FSPI` 를 쓴다.
#   그런데 Arduino 코어 3.x 에서 S3 의 FSPI 는 0 이고(esp32-hal-spi.h), IDF 의
#   REG_SPI_BASE 는 페리페럴 번호 2·3 을 기대한다:
#       #define REG_SPI_BASE(i) (((i)>=2) ? (DR_REG_SPI2_BASE + (i-2)*0x1000) : (0))
#   그래서 REG_SPI_BASE(0) 가 0 이 되고, SPI_USER_REG(0) 가 0x10 이 된다.
#   TFT_eSPI 에 쓸 만한 대체 정의가 있지만 `#ifndef REG_SPI_BASE` 라 IDF 것에 밀린다.
#
#   USE_HSPI_PORT 를 주면 SPI_PORT 가 3 이 되어 REG_SPI_BASE(3) = SPI3 이고,
#   같은 조건에서 만들어지는 SPIClass(HSPI) 도 코어에서 HSPI=1 → SPI3 라 둘이 맞는다.
#   USE_FSPI_PORT(→ SPI_PORT 2 · SPI2)도 성립하지만, 코어의 전역 SPI 객체가
#   S3 에서 FSPI 를 쓰므로 같은 버스를 두 객체가 잡는 일을 피해 HSPI 로 둔다.
#   PN532 는 I2C(16/17)라 SPI 버스를 다투지 않는다.
FLAGS="-DUSER_SETUP_LOADED=1 \
-DILI9341_DRIVER=1 \
-DTFT_WIDTH=240 -DTFT_HEIGHT=320 \
-DTFT_MISO=6 -DTFT_MOSI=16 -DTFT_SCLK=15 \
-DTFT_CS=8 -DTFT_DC=17 -DTFT_RST=18 \
-DTOUCH_CS=5 \
-DUSE_HSPI_PORT=1 \
-DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1 -DLOAD_FONT6=1 -DLOAD_FONT7=1 -DLOAD_FONT8=1 -DLOAD_GFXFF=1 \
-DSMOOTH_FONT=1 \
-DSPI_FREQUENCY=40000000 \
-DSPI_READ_FREQUENCY=20000000"

ARGS=(--fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=$FLAGS" TalentNfcReader)
[ "$1" = "--upload" ] && ARGS+=(--upload -p "$PORT")

echo "▶ FQBN: $FQBN"
arduino-cli compile "${ARGS[@]}"
