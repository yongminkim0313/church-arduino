#!/bin/bash
# GodlifeScheduleNext_JC3248 빌드/업로드 (가이션 JC3248W535C_I_Y)
#
# 원본(GodlifeScheduleNext)과 달리 TFT_eSPI 를 쓰지 않으므로 Setup25 를 고르는
# 수고가 없다 — 화면 핀은 PanelTFT.h 에 박혀 있다. sketch.yaml 도 두지 않았다.
# FQBN 이 길어(PSRAM·파티션) 스크립트 한 줄로 부르는 편이 낫다.
#
#   ./build.sh                     컴파일만
#   ./build.sh --upload            컴파일 + 업로드 (포트는 아래 PORT 또는 두 번째 인자로)
#   UART_LOG=1 ./build.sh --upload 로그를 UART 브리지 포트로 뺀다
#
# 필요 라이브러리: GFX Library for Arduino(1.5+), ArduinoJson 7.x, ChurchSecrets
#   arduino-cli lib install "GFX Library for Arduino" "ArduinoJson"
#
# 폰트 헤더는 저장소에 없다(파생 파일). 클론 직후 한 번 만든다:
#   ./tools/make-fonts.sh
set -e
cd "$(dirname "$0")/.."

PORT="${2:-/dev/cu.usbmodem101}"

# PSRAM 은 반드시 켜야 한다 — 화면 버퍼 300KB(320×480×2)를 거기에 잡고,
# 한글 폰트 메트릭(2,452자 × 두 벌)도 거기로 간다.
# 이 보드의 모듈은 N16R8(옥탈 PSRAM 8MB · 플래시 16MB)이라 PSRAM=opi 다.
# 파티션은 TalentNfcReader_JC3245 와 같은 표(앱 4MB)를 쓴다 — 한글 폰트 두 벌이
# 2.2MB 라 기본 표(1.31MB)나 huge_app(3MB)로는 들어가지 않는다.
#
# CDCOnBoot 는 Serial 이 어디로 나가는지를 정한다.
#   cdc(기본)  네이티브 USB 포트(이 보드의 Type-C 하나로 전원·업로드·콘솔이 다 된다)
#   default    UART0(GPIO43/44)
CDC_ON_BOOT="${UART_LOG:+default}"; CDC_ON_BOOT="${CDC_ON_BOOT:-cdc}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=$CDC_ON_BOOT,PartitionScheme=custom"

ARGS=(--fqbn "$FQBN" GodlifeScheduleNext_JC3248)
[ "$1" = "--upload" ] && ARGS+=(--upload -p "$PORT")

echo "▶ FQBN: $FQBN"
arduino-cli compile "${ARGS[@]}"
