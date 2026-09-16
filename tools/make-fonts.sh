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
# 20px 는 이름·안내문처럼 멀리서 읽혀야 하는 줄에만 쓴다(916KB).
# VLW 는 구운 크기로만 그려져 setTextSize 로 키울 수 없어서 두 벌을 든다.
echo "▶ TalentNfcReader/FontKR20.h 생성 중..."
python3 tools/ttf2vlw.py "$TTF" --size 20 --ks2350 --preset ui \
    --var FontKR20 -o TalentNfcReader/FontKR20.h

# 달란트 리더(가이션 JC3248W535 판) — 화면이 320×480 이라 한 단계씩 크게 쓴다.
# 본문 20px · 이름과 안내문 24px, 그리고 큰 숫자 두 벌(원본의 내장 폰트 4·6번 자리).
# 앱 칸이 4MB 라 한글 폰트 두 벌(20+24 = 2.1MB)이 들어가는 한계에 가깝다 —
# 더 키우려면 파티션표(partitions.csv)부터 손볼 것.
# 가운뎃점(·)은 문구에 쓰이는데 preset 에 없어 따로 넣는다.
echo "▶ TalentNfcReader_JC3245/FontKR20.h · FontKR24.h 생성 중..."
python3 tools/ttf2vlw.py "$TTF" --size 20 --ks2350 --preset ui --chars "·" \
    --var FontKR20 -o TalentNfcReader_JC3245/FontKR20.h
python3 tools/ttf2vlw.py "$TTF" --size 24 --ks2350 --preset ui --chars "·" \
    --var FontKR24 -o TalentNfcReader_JC3245/FontKR24.h
echo "▶ TalentNfcReader_JC3245/FontNum34.h · FontNum64.h 생성 중..."
python3 tools/ttf2vlw.py "$TTF" --size 34 --no-ascii --chars "0123456789.-+P " \
    --var FontNum34 -o TalentNfcReader_JC3245/FontNum34.h
python3 tools/ttf2vlw.py "$TTF" --size 64 --no-ascii --chars "0123456789.-+P " \
    --var FontNum64 -o TalentNfcReader_JC3245/FontNum64.h

# 달란트 리더(LILYGO T-RGB 원형 판) — 쓰는 폰트가 JC3245 판과 같다. 그대로 복사한다.
echo "▶ TalentNfcReader_CST820/ 폰트 복사 중..."
for f in FontKR20.h FontKR24.h FontNum34.h FontNum64.h; do
  cp "TalentNfcReader_JC3245/$f" "TalentNfcReader_CST820/$f"
done

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
