#!/bin/bash
# 한글 스무스폰트 헤더를 생성한다. 클론 직후 한 번 돌리면 된다.
# 생성물은 .gitignore 되어 있다 — 11MB 짜리 파생 파일이라 커밋하지 않는다.
set -e
cd "$(dirname "$0")/.."

TTF="${1:-$HOME/Library/Fonts/NanumGothicBold.ttf}"
[ -f "$TTF" ] || { echo "폰트를 찾을 수 없다: $TTF"; echo "사용법: $0 [/경로/폰트.ttf]"; exit 1; }

echo "▶ 폰트 원본: $TTF"

# 한글 음절 11,172자 전체 + ASCII, 12px — Huge APP(3MB)에 들어가는 최대 크기
for d in ChurchDisplayRx ChurchDisplayRxBLE; do
  echo "▶ $d/FontKRFull12.h 생성 중..."
  python3 tools/ttf2vlw.py "$TTF" --size 12 --ks1001 --preset ui \
      --var FontKRFull12 -o "$d/FontKRFull12.h"
  echo "▶ $d/FontNum38.h 생성 중..."
  python3 tools/ttf2vlw.py "$TTF" --size 38 --no-ascii --chars "0123456789.-%C " \
      --var FontNum38 -o "$d/FontNum38.h"
done

echo "✅ 완료"
