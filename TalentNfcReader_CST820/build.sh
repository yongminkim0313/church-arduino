#!/bin/bash
# TalentNfcReader_CST820 빌드/업로드 (LILYGO T-RGB 2.1" 원형)
#
# 원본(TalentNfcReader)과 달리 TFT_eSPI 를 쓰지 않으므로 화면 설정을 빌드 플래그로
# 주입할 일이 없다 — build_opt.h 가 필요 없어졌다. 화면 핀은 PanelTFT.h 에 박혀 있다.
#
#   ./build.sh                     컴파일만
#   ./build.sh --upload            컴파일 + 업로드 (포트는 아래 PORT 또는 두 번째 인자로)
#   UART_LOG=1 ./build.sh --upload 로그를 UART 브리지 포트로 뺀다
#   NO_WIFI=1  ./build.sh --upload 와이파이를 일부러 못 붙게 구워 블루투스 설정 화면을 시험한다
#
# 필요 라이브러리: GFX Library for Arduino(1.5+ — XL9535·ST7701 RGB 지원), Adafruit PN532,
#                  Adafruit BusIO, ArduinoJson, NimBLE-Arduino 2.x
#   arduino-cli lib install "GFX Library for Arduino" "Adafruit PN532" "ArduinoJson" "NimBLE-Arduino"
set -e
cd "$(dirname "$0")/.."

PORT="${2:-/dev/cu.usbmodem101}"

# PSRAM 은 반드시 켜야 한다 — RGB 화면의 프레임버퍼 460KB(480×480×2)를 거기에 잡는다.
# 이 보드의 모듈은 ESP32-S3R8(옥탈 PSRAM 8MB · 플래시 16MB)이라 PSRAM=opi 다.
# 파티션은 원본과 같은 표를 쓴다(앱 4MB + LittleFS 11.875MB) — 폰트가 2.4MB 라
# 기본 표(1.31MB)나 huge_app(3MB/0.875MB)로는 들어가지 않는다.
#
# CDCOnBoot 는 Serial 이 어디로 나가는지를 정한다.
#   cdc(기본)  네이티브 USB 포트(이 보드의 Type-C 하나로 전원·업로드·콘솔이 다 된다)
#   default    UART0(GPIO43/44)
CDC_ON_BOOT="${UART_LOG:+default}"; CDC_ON_BOOT="${CDC_ON_BOOT:-cdc}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=$CDC_ON_BOOT,PartitionScheme=custom"

ARGS=(--fqbn "$FQBN" TalentNfcReader_CST820)
# 시험용 — 알고 있는 인증정보를 모두 건너뛰고 블루투스 설정 화면으로 들어간다.
[ -n "$NO_WIFI" ] && ARGS+=(--build-property "compiler.cpp.extra_flags=-DFORCE_WIFI_SETUP=1")
[ "$1" = "--upload" ] && ARGS+=(--upload -p "$PORT")

echo "▶ FQBN: $FQBN"
arduino-cli compile "${ARGS[@]}"
