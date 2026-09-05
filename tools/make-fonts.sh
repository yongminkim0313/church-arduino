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

# 달란트 리더 — 서버가 내려주는 문구가 무엇이든 나와야 해서 상용 2350자를 담는다.
# 전체 11,172자는 14px 에서 2.4MB 라 Huge APP(3MB)에 들어가지 않는다.
echo "▶ TalentNfcReader/FontKR14.h 생성 중..."
python3 tools/ttf2vlw.py "$TTF" --size 14 --ks2350 --preset ui \
    --var FontKR14 -o TalentNfcReader/FontKR14.h

# GodlifeScheduleNext — KS X 1001 상용 2,350자 16px + 큰 숫자 30px.
# 달란트 리더와 같은 이유다. 글리프 메트릭이 RAM 을 먹는데(글리프당 12B)
# 전체면 135KB 라 HTTPS 핸드셰이크가 쓸 힙이 남지 않는다. 상용 2,350자면 29KB.
echo "▶ GodlifeScheduleNext/FontKR16.h 생성 중..."
python3 tools/ttf2vlw.py "$TTF" --size 16 --ks2350 --chars "·▶●○◆…—" \
    --var FontKR16 -o GodlifeScheduleNext/FontKR16.h
echo "▶ GodlifeScheduleNext/FontNum30.h 생성 중..."
python3 tools/ttf2vlw.py "$TTF" --size 30 --no-ascii --chars "0123456789:.-DAY " \
    --var FontNum30 -o GodlifeScheduleNext/FontNum30.h

echo "✅ 완료"
