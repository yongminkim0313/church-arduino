#!/bin/bash
# TalentNfcReader 빌드/업로드
#
# TFT_eSPI 는 설정을 라이브러리 폴더(User_Setup_Select.h)에서 읽는다. 그대로 두면
# 이 PC 의 TTGO T-Display 스케치들(Setup25)과 설정이 충돌한다.
# 그래서 라이브러리를 건드리지 않고 -D 플래그로 이 스케치에만 설정을 주입한다.
# (TFT_eSPI 는 USER_SETUP_LOADED 가 정의돼 있으면 자체 설정 파일을 읽지 않는다)
#
#   ./build.sh            컴파일만
#   ./build.sh --upload   컴파일 + 업로드 (포트는 아래 PORT 또는 인자로)
set -e
cd "$(dirname "$0")/.."

PORT="${2:-/dev/cu.usbmodem101}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc"

# CLAUDE.md 핀맵 그대로
FLAGS="-DUSER_SETUP_LOADED=1 \
-DILI9341_DRIVER=1 \
-DTFT_WIDTH=240 -DTFT_HEIGHT=320 \
-DTFT_MISO=13 -DTFT_MOSI=11 -DTFT_SCLK=12 \
-DTFT_CS=10 -DTFT_DC=8 -DTFT_RST=9 \
-DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1 -DLOAD_FONT6=1 -DLOAD_FONT7=1 -DLOAD_FONT8=1 -DLOAD_GFXFF=1 \
-DSMOOTH_FONT=1 \
-DSPI_FREQUENCY=40000000 \
-DSPI_READ_FREQUENCY=20000000"

ARGS=(--fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=$FLAGS" TalentNfcReader)
[ "$1" = "--upload" ] && ARGS+=(--upload -p "$PORT")

echo "▶ FQBN: $FQBN"
arduino-cli compile "${ARGS[@]}"
