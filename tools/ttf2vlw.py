#!/usr/bin/env python3
"""TTF/OTF → TFT_eSPI 스무스폰트(VLW) 변환기.

TFT_eSPI 의 안티에일리어싱 폰트(VLW)를 만든다. 내장 폰트와 달리 유니코드를
지원하므로 한글을 그릴 수 있다. 필요한 글자만 골라 담는 서브셋 방식이라
플래시를 아낀다.

  # UI 에 쓰는 글자만 담은 16px 한글 폰트 → C 헤더
  python3 tools/ttf2vlw.py /System/Library/Fonts/Supplemental/NanumGothic.ttf \
      --size 16 --var FontKR16 --preset ui \
      -o ChurchDisplayRx/FontKR16.h

  # 숫자 전용 큰 폰트
  python3 tools/ttf2vlw.py /System/Library/Fonts/Supplemental/NanumGothic.ttf \
      --size 36 --var FontNum36 --chars "0123456789.-% C" \
      -o ChurchDisplayRx/FontNum36.h

  # 한글 음절 전체 11,172자 (크다 → LittleFS 용 .vlw 로)
  python3 tools/ttf2vlw.py .../NanumGothic.ttf --size 16 --ks1001 -o full16.vlw

  # KS X 1001 상용 2,350자 — 전체의 1/5 크기로 실사용 한글은 거의 다 커버
  python3 tools/ttf2vlw.py .../NanumGothic.ttf --size 16 --ks-common \
      --var FontKR16 -o GodlifeScheduleNext/FontKR16.h

VLW 포맷 (모두 big-endian int32):
  헤더 24B : gCount, version, size, 0, ascent, descent
  글리프   : gCount × 28B = unicode, height, width, xAdvance, dY, dX, 0
  비트맵   : 글리프 순서대로 width*height 바이트(8비트 알파)
"""
import argparse, struct, sys, os
from PIL import ImageFont

# UI 에서 쓰는 한글 + 방 이름 후보. 필요하면 --chars 로 덧붙인다.
PRESET_UI = (
    "온도습기압실내외본당교육관사무청년유아부식로비지하"
    "실시간연결재중끊김대기수신전초분무선상태오류없음"
    "갱된값센서화면밝은어두운시작종료"
)

def ks1001_syllables():
    """한글 음절 영역 전체(11,172자)."""
    return "".join(chr(c) for c in range(0xAC00, 0xD7A4))

def ks_common_syllables():
    """KS X 1001 상용 한글 2,350자.

    EUC-KR 의 한글 영역(0xB0A1~0xC8FE)을 그대로 디코드해서 얻는다. 현대 국어
    표기에 쓰이는 음절은 사실상 다 들어 있고, 글리프 수가 전체(11,172자)의
    1/5 이라 플래시도 메트릭 RAM 도 그만큼 줄어든다.
    """
    out = []
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(bytes([hi, lo]).decode("euc-kr"))
            except UnicodeDecodeError:
                pass
    return "".join(out)

def build(ttf, size, chars, index=0):
    font = ImageFont.truetype(ttf, size, index=index)
    ascent, descent = font.getmetrics()
    glyphs = []
    for ch in chars:
        try:
            mask, off = font.getmask2(ch, mode="L")
        except Exception:
            continue
        w, h = mask.size
        ox, oy = off
        adv = int(round(font.getlength(ch)))
        data = bytes(mask) if w and h else b""
        if len(data) != w * h:
            data = (data + b"\x00" * (w * h))[: w * h]
        glyphs.append({
            "u": ord(ch), "w": w, "h": h, "adv": adv,
            "dx": ox, "dy": ascent - oy, "bmp": data,
        })
    glyphs.sort(key=lambda g: g["u"])

    i32 = lambda v: struct.pack(">i", int(v))
    out = i32(len(glyphs)) + i32(11) + i32(size) + i32(0) + i32(ascent) + i32(descent)
    for g in glyphs:
        out += i32(g["u"]) + i32(g["h"]) + i32(g["w"]) + i32(g["adv"]) + i32(g["dy"]) + i32(g["dx"]) + i32(0)
    for g in glyphs:
        out += g["bmp"]
    return out, glyphs, (ascent, descent)

def as_header(blob, var):
    lines = [
        "// 자동 생성 파일 — tools/ttf2vlw.py 로 만들어짐. 직접 고치지 말 것.",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
        f"const uint8_t {var}[] PROGMEM = {{",
    ]
    for i in range(0, len(blob), 16):
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in blob[i:i+16]) + ",")
    lines += ["};", ""]
    return "\n".join(lines)

def main():
    p = argparse.ArgumentParser(description="TTF → TFT_eSPI VLW 스무스폰트")
    p.add_argument("ttf")
    p.add_argument("--size", type=int, default=16, help="픽셀 크기 (기본 16)")
    p.add_argument("--index", type=int, default=0, help="TTC 내 폰트 인덱스")
    p.add_argument("--chars", default="", help="포함할 글자 (직접 지정)")
    p.add_argument("--preset", choices=["ui", "none"], default="none",
                   help="ui = UI/방이름 한글 세트 추가")
    p.add_argument("--ascii", action="store_true", default=None,
                   help="ASCII 0x20~0x7E 포함 (기본: 켜짐)")
    p.add_argument("--no-ascii", dest="ascii", action="store_false")
    p.add_argument("--ks1001", action="store_true", help="한글 음절 11,172자 전체")
    p.add_argument("--ks-common", action="store_true",
                   help="KS X 1001 상용 한글 2,350자 (전체의 1/5 크기)")
    p.add_argument("--var", default="", help="C 배열 이름 (주면 .h, 없으면 .vlw)")
    p.add_argument("-o", "--out", required=True)
    a = p.parse_args()

    chars = set()
    if a.ascii is not False:
        chars |= {chr(c) for c in range(0x20, 0x7F)}
    if a.preset == "ui":
        chars |= set(PRESET_UI)
    if a.ks1001:
        chars |= set(ks1001_syllables())
    if a.ks_common:
        chars |= set(ks_common_syllables())
    chars |= set(a.chars)
    chars = sorted(chars)

    blob, glyphs, (asc, desc) = build(a.ttf, a.size, chars, a.index)
    body = as_header(blob, a.var).encode() if a.var else blob
    with open(a.out, "wb") as f:
        f.write(body)

    kb = len(blob) / 1024
    print(f"글리프 {len(glyphs)}자  ascent={asc} descent={desc}")
    print(f"VLW 데이터 {len(blob):,} B ({kb:.1f} KB) → {a.out} ({len(body):,} B)")
    if len(glyphs) > 400:
        print("주의: 글리프가 많으면 TFT_eSPI 가 선형 탐색이라 느려진다. LittleFS 사용을 권장.")

if __name__ == "__main__":
    sys.exit(main())
